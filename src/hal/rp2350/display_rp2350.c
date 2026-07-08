// display_rp2350.c — RP2350 Display HAL using pico_hdmi RT API
// Spec §10, TIP §6.2-6.3.
//
// KEY DESIGN: The scanline callback does ZERO color conversion.
// The HSTX peripheral's expand_tmds / expand_shift registers are set once
// during profile init (via video_output_set_pixel_format()) and handle all
// bit-rotation + TMDS encoding in hardware for each pixel format.
// The scanline callback's only job is to expose the raw framebuffer row
// pointer — or for scale_x > 1, to duplicate pixels with a tight memcpy loop.
//
// Pixel-format constants live in pico_hdmi/pixel_formats.h.
// Profile switch: retune pll_usb → video_output_set_mode() → wait g_mode_switch_complete
//               → video_output_set_pixel_format() → video_output_start().

#include "display_rp2350.h"
#include "memory/profiles.h"
#include "graphics/framebuffer.h"
#include "state/coprocessor_state.h"
#include "feature_flags.h"

#include "pico_hdmi/video_output_rt.h"
#include "pico_hdmi/pixel_formats.h"

#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

// =============================================================================
// Per-profile state set by display_rp2350_apply_profile()
// =============================================================================
static uint8_t  s_scale_x      = 1;
static uint8_t  s_bytes_per_px = 1;    // 1 for 8bpp, 2 for 16bpp
static uint16_t s_output_width = 640;  // H active pixels in the DVI mode

// =============================================================================
// Scanline callback — called from Core 1 DMA ISR context
// RULES:
//   - Do the absolute minimum. No branches on pixel values. No arithmetic.
//   - For scale_x == 1: just memcpy the raw framebuffer row into line_buffer.
//   - For scale_x >  1: duplicate each raw pixel s_scale_x times by a simple
//     loop; still no format conversion — HSTX does that in hardware.
//
// line_buffer is (MODE_H_ACTIVE_PIXELS / 2) uint32_t words = h_active uint16_t
// slots when 16bpp, or h_active uint8_t slots when 8bpp. We write as bytes.
// =============================================================================
static void scanline_cb(uint32_t v_scanline, uint32_t active_line, uint32_t *line_buffer) {
    if (!g_fb_front) return;

    // Map DVI active line → logical row (simple integer division by scale_y).
    // scale_y is folded into the pointer arithmetic below; we retrieve it
    // indirectly through the profile (stored as s_bytes_per_px + s_scale_x).
    // For simplicity store scale_y as well:
    extern uint8_t _disp_scale_y;   // defined just below this function
    const uint32_t src_row = active_line / _disp_scale_y;
    const uint8_t *src     = g_fb_front + src_row * g_fb_stride;

    if (s_scale_x == 1) {
        // Fast path: raw copy — no conversion
        __builtin_memcpy(line_buffer, src, (size_t)g_fb_stride);
    } else {
        // Scale-up path: duplicate each source pixel s_scale_x times
        uint8_t *out  = (uint8_t *)line_buffer;
        uint16_t npx  = g_fb_width;
        uint8_t  bpp  = s_bytes_per_px;
        uint8_t  sx   = s_scale_x;

        if (bpp == 1) {
            // 8 bpp: duplicate bytes
            for (uint16_t px = 0; px < npx; px++) {
                uint8_t v = src[px];
                for (uint8_t r = 0; r < sx; r++) *out++ = v;
            }
        } else {
            // 16 bpp: duplicate 16-bit pairs
            const uint16_t *src16 = (const uint16_t *)src;
            uint16_t       *out16 = (uint16_t *)out;
            for (uint16_t px = 0; px < npx; px++) {
                uint16_t v = src16[px];
                for (uint8_t r = 0; r < sx; r++) *out16++ = v;
            }
        }
    }
}

// scale_y stored at module level so scanline_cb can access without profile ptr
uint8_t _disp_scale_y = 1;

// =============================================================================
// VSYNC callback — called from Core 1 at start of vertical sync
// =============================================================================
static void vsync_cb(void) {
    g_state.frame_count++;

    // Deferred buffer swap: atomically flip front/back at VBLANK
    if (g_state.swap_pending) {
        display_rp2350_swap_buffers();
        g_state.swap_pending = false;
    }

    // TE pin: quick toggle for host tearing-effect synchronisation
    gpio_put(PIN_TE, 1);
    __asm volatile("nop\nnop\nnop");
    gpio_put(PIN_TE, 0);
}

// =============================================================================
// display_rp2350_init — call once from main(), before Core 1 launch
// =============================================================================

static bool s_core1_started = false;

void display_rp2350_init(void) {
    gpio_init(PIN_TE);
    gpio_set_dir(PIN_TE, GPIO_OUT);
    gpio_put(PIN_TE, 0);
}

// =============================================================================
// display_rp2350_apply_profile
// Full sequence per TIP §6.2:
//   1. Stop DVI (if started)
//   2. Retune pll_usb for this profile's HSTX target frequency
//   3. Configure clk_hstx from pll_usb (via video_output_reconfigure_clock)
//   4. Switch video mode (set_mode + wait g_mode_switch_complete)
//   5. Set HSTX pixel format expand registers for this bpp
//   6. Restart DVI
// =============================================================================
void display_rp2350_apply_profile(const profile_t *prof) {
    // 1. Stop video output (signals Core 1 to spin-idle)
    if (s_core1_started) {
        video_output_stop();
    }

    // 2. Retune pll_usb for this profile's HSTX clock
    //    (REFDIV=1, VCO=pll_vco_hz, POSTDIV1=pd1, POSTDIV2=pd2)
    pll_init(pll_usb, 1, prof->pll_vco_hz, prof->pll_postdiv1, prof->pll_postdiv2);

    // 3. Point clk_hstx at pll_usb output
    //    video_output_reconfigure_clock() reads clock_get_hz(clk_usb) to discover
    //    the pll_usb output. clk_usb is still on pll_sys/9 from hw_init_all(),
    //    so we configure clk_hstx directly here first, then let the library confirm.
    clock_configure(clk_hstx,
        0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        prof->clk_hstx_hz,
        prof->clk_hstx_hz);

    // 4. Request video mode switch (applies on Core 1 main loop)
    const video_mode_t *vmode = (prof->timing_id == TIMING_640x480_60)
                                ? &video_mode_480_p
                                : &video_mode_720_p;

    if (!s_core1_started) {
        // First time: full init (Core 1 not running yet; called before multicore_launch)
        video_output_init(prof->logical_w, prof->logical_h);
        video_output_set_scanline_callback(scanline_cb);
        video_output_set_vsync_callback(vsync_cb);
    } else {
        // Profile switch: ask Core 1 to apply the new mode
        g_mode_switch_complete = false;
        video_output_set_mode(vmode);
        // Spin until Core 1 confirms the mode is applied
        while (!g_mode_switch_complete) { tight_loop_contents(); }
    }

    // 5. Set HSTX pixel format expand registers (hardware does all colour conversion)
    uint32_t expand_tmds, expand_shift;
    if (prof->bpp_class == 16) {
        expand_tmds  = HSTX_EXPAND_TMDS_RGB565;
        expand_shift = HSTX_EXPAND_SHIFT_16BPP;
    } else { // 8bpp (RGB332 default; SET_PIXEL_FORMAT can override later)
        expand_tmds  = HSTX_EXPAND_TMDS_RGB332;
        expand_shift = HSTX_EXPAND_SHIFT_8BPP;
    }
    video_output_set_pixel_format(expand_tmds, expand_shift);

    // 6. Cache scale factors for the scanline callback
    s_scale_x      = prof->scale_x;
    _disp_scale_y  = prof->scale_y;
    s_bytes_per_px = (prof->bpp_class == 16) ? 2u : 1u;
    s_output_width = (prof->timing_id == TIMING_640x480_60) ? 640u : 1280u;

    // 7. Restart DVI output
    if (s_core1_started) {
        video_output_start();
    }
    // (If not yet started, Core 1 launch itself will begin output)
}

// =============================================================================
// display_rp2350_mark_core1_started — call after multicore_launch_core1()
// =============================================================================
void display_rp2350_mark_core1_started(void) {
    s_core1_started = true;
}

// =============================================================================
// display_rp2350_stop
// =============================================================================
void display_rp2350_stop(void) {
    if (s_core1_started) {
        video_output_stop();
    }
}

// =============================================================================
// display_rp2350_swap_buffers — atomic front/back flip (Core 0 or VSYNC CB)
// =============================================================================
void display_rp2350_swap_buffers(void) {
    uint8_t *tmp = g_fb_front;
    g_fb_front   = g_fb_back;
    g_fb_back    = tmp;
}

// =============================================================================
// Core 1 entry — runs pico_hdmi polling loop, never returns
// =============================================================================
void display_rp2350_core1_entry(void) {
    video_output_core1_run();
}

// =============================================================================
// display_rp2350_set_pixel_format — live pixel format switch (SET_PIXEL_FORMAT 0x14)
// Maps format_enum (PIXEL_FORMAT_* from opcodes.h) to HSTX expand_tmds /
// expand_shift register pairs defined in pico_hdmi/pixel_formats.h.
// Only expand_tmds and expand_shift change; PLL, framebuffer, and mode are untouched.
// =============================================================================
bool display_rp2350_set_pixel_format(uint8_t format_enum) {
    uint32_t expand_tmds, expand_shift;

    switch (format_enum) {
        // ── 8bpp ──────────────────────────────────────────────────────────
        case 0x00: // PIXEL_FORMAT_RGB332
            expand_tmds  = HSTX_EXPAND_TMDS_RGB332;
            expand_shift = HSTX_EXPAND_SHIFT_8BPP;
            break;
        case 0x01: // PIXEL_FORMAT_MONO8 (greyscale)
            expand_tmds  = HSTX_EXPAND_TMDS_MONO8;
            expand_shift = HSTX_EXPAND_SHIFT_8BPP;
            break;
        case 0x02: // PIXEL_FORMAT_INDEX8 (palette; framebuffer stores palette index)
            // No separate HSTX constant — rendered identically to RGB332 for now.
            // The palette lookup is a Phase 3 concern (LUT in scanline callback).
            expand_tmds  = HSTX_EXPAND_TMDS_RGB332;
            expand_shift = HSTX_EXPAND_SHIFT_8BPP;
            break;
        // ── 16bpp ─────────────────────────────────────────────────────────
        case 0x10: // PIXEL_FORMAT_RGB565
            expand_tmds  = HSTX_EXPAND_TMDS_RGB565;
            expand_shift = HSTX_EXPAND_SHIFT_16BPP;
            break;
        // ── 4bpp ──────────────────────────────────────────────────────────
        case 0x20: // PIXEL_FORMAT_RGB121
            expand_tmds  = HSTX_EXPAND_TMDS_RGB121;
            expand_shift = HSTX_EXPAND_SHIFT_4BPP;
            break;
        case 0x21: // PIXEL_FORMAT_MONO4 — greyscale nibble; reuse MONO8 expand + 4bpp shift
            expand_tmds  = HSTX_EXPAND_TMDS_MONO8;
            expand_shift = HSTX_EXPAND_SHIFT_4BPP;
            break;
        case 0x22: // PIXEL_FORMAT_INDEX4 — same HSTX layout as RGB121 (Phase 3 palette)
            expand_tmds  = HSTX_EXPAND_TMDS_RGB121;
            expand_shift = HSTX_EXPAND_SHIFT_4BPP;
            break;
        // ── 24bpp ─────────────────────────────────────────────────────────
        case 0x30: // PIXEL_FORMAT_RGB888
            expand_tmds  = HSTX_EXPAND_TMDS_RGB888;
            expand_shift = HSTX_EXPAND_SHIFT_24BPP;
            break;
        default:
            return false;  // unknown format; caller sets ERR_INVALID_PARAM
    }

    video_output_set_pixel_format(expand_tmds, expand_shift);
    // Update scanline bytes-per-pixel if format size changed
    s_bytes_per_px = (format_enum == 0x10) ? 2u : 1u;   // 16bpp=2, everything else=1
    return true;
}
