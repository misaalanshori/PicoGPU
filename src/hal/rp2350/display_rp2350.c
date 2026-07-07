// display_rp2350.c — RP2350 Display HAL using pico_hdmi API
// Adapts the GPU framebuffer to pico_hdmi's scanline callback API.
// Spec §10, TIP §6.2-6.3.
//
// pico_hdmi API (from video_output.h):
//   video_output_init(width, height)         — init HSTX+DMA
//   video_output_set_scanline_callback(cb)   — register scanline CB
//   video_output_set_vsync_callback(cb)      — register VSYNC CB
//   video_output_core1_run()                 — Core 1 loop (never returns)
//   video_output_stop() / video_output_start()  — TI§10 fork additions

#include "display_rp2350.h"
#include "graphics/effects.h"
#include "graphics/framebuffer.h"
#include "state/coprocessor_state.h"
#include "memory/profiles.h"
#include "feature_flags.h"

#include "pico_hdmi/video_output.h"
#include "pico_hdmi/video_output_rt.h"

#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

// pixel-duplication factors for the scanline callback
static uint8_t s_scale_x = 1;
static uint8_t s_scale_y = 1;

static bool s_dvi_started = false;
static uint16_t s_output_width  = 0;
static uint16_t s_output_height = 0;

// =============================================================================
// Scanline callback (called from Core 1 DMA ISR)
// Fills line_buffer with MODE_H_ACTIVE_PIXELS uint16_t words (packed as
// (MODE_H_ACTIVE_PIXELS/2) uint32_t words per pico_hdmi convention).
// =============================================================================
static void scanline_cb(uint32_t v_scanline, uint32_t active_line, uint32_t *line_buffer) {
    if (!g_fb_front || !g_fb_width) return;

    // Map DVI active line to logical framebuffer row
    uint32_t src_row = active_line / s_scale_y;
    if (src_row >= g_fb_height) src_row = g_fb_height - 1;

    uint16_t *out = (uint16_t *)line_buffer;
    uint16_t col = 0;
    uint16_t out_pixels = s_output_width;

    if (g_fb_bpp == 8) {
        const uint8_t *src = g_fb_front + src_row * g_fb_stride;
        for (uint16_t px = 0; px < g_fb_width && col < out_pixels; px++) {
            uint8_t r8 = src[px];
            // RGB332 → RGB565
            uint8_t r = (r8 >> 5) & 0x07;
            uint8_t g = (r8 >> 2) & 0x07;
            uint8_t b =  r8       & 0x03;
            uint16_t rgb565 = (uint16_t)(((uint16_t)(r << 2 | r >> 1) << 11) |
                                         ((uint16_t)(g << 3 | g)       <<  5) |
                                         ((uint16_t)(b << 3 | b << 1 | b >> 1)));
            for (uint8_t sx = 0; sx < s_scale_x && col < out_pixels; sx++) {
                out[col++] = rgb565;
            }
        }
    } else { // 16bpp
        const uint16_t *src = (const uint16_t *)g_fb_front + src_row * g_fb_width;
        for (uint16_t px = 0; px < g_fb_width && col < out_pixels; px++) {
            uint16_t pix = src[px];
            for (uint8_t sx = 0; sx < s_scale_x && col < out_pixels; sx++) {
                out[col++] = pix;
            }
        }
    }
}

// =============================================================================
// VSYNC callback — called by pico_hdmi at start of vertical sync
// =============================================================================
static void vsync_cb(void) {
    g_state.frame_count++;

    // TE pin: pulse HIGH for one frame boundary
    gpio_put(PIN_TE, 1);
    // (Note: pico_hdmi calls this from Core 1 IRQ context — just pulse quickly)
    gpio_put(PIN_TE, 0);

    // Deferred swap: flip front/back pointers at VBLANK
    if (g_state.swap_pending) {
        display_rp2350_swap_buffers();
        g_state.swap_pending = false;
    }
}

// =============================================================================
// display_rp2350_init — called once from main() in UNINITIALIZED state
// =============================================================================
void display_rp2350_init(void) {
    gpio_init(PIN_TE);
    gpio_set_dir(PIN_TE, GPIO_OUT);
    gpio_put(PIN_TE, 0);
    s_dvi_started = false;
}

// =============================================================================
// display_rp2350_apply_profile — full mode-switch sequence (TIP §6.2)
// =============================================================================
void display_rp2350_apply_profile(const profile_t *prof) {
    // 1. Stop output if running
    if (s_dvi_started) {
        video_output_stop();
    }

    // 2. Retune pll_usb for this profile's HSTX clock (TIP §6.2)
    //    clk_usb is sourced from pll_sys/8 (48 MHz) so pll_usb is free for HSTX.
    pll_init(pll_usb, 1, prof->pll_vco_hz, prof->pll_postdiv1, prof->pll_postdiv2);

    // 3. Update scale factors
    s_scale_x = prof->scale_x;
    s_scale_y = prof->scale_y;

    // 4. Determine output dimensions from timing_id
    if (prof->timing_id == TIMING_640x480_60) {
        s_output_width  = 640;
        s_output_height = 480;
    } else {
        s_output_width  = 1280;
        s_output_height = 720;
    }

    if (!s_dvi_started) {
        // First time: init pico_hdmi HSTX+DMA
        video_output_init((uint16_t)s_output_width, (uint16_t)s_output_height);
        video_output_set_scanline_callback(scanline_cb);
        video_output_set_vsync_callback(vsync_cb);
        s_dvi_started = true;
    }
    // (For profile switches after first init, pico_hdmi in this fork reuses
    // the same DMA setup; only the pll_usb retune above changes the timing.)

    // 5. Restart output
    video_output_start();
}

// =============================================================================
// display_rp2350_stop
// =============================================================================
void display_rp2350_stop(void) {
    if (s_dvi_started) {
        video_output_stop();
    }
}

// =============================================================================
// display_rp2350_swap_buffers — atomic front/back flip
// =============================================================================
void display_rp2350_swap_buffers(void) {
    uint8_t *tmp = g_fb_front;
    g_fb_front   = g_fb_back;
    g_fb_back    = tmp;
}

// =============================================================================
// Core 1 entry — runs pico_hdmi polling loop forever
// =============================================================================
void display_rp2350_core1_entry(void) {
    video_output_core1_run(); // never returns
}
