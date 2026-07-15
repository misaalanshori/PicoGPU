// profiles.c — Display profile table + SYSTEM_CONFIG / SOFT_RESET handlers
// Spec §3.2–3.6, TIP §6.2.

#include "profiles.h"
#include "arena.h"
#include "../state/coprocessor_state.h"
#include "../graphics/effects.h"
#include "../graphics/framebuffer.h"
#include "../assets/vram.h"
#include "../hal/rp2350/display_rp2350.h"
#include "../transport/spi_slave.h"
#include "error_codes.h"
#include "opcodes.h"
#include "feature_flags.h"

#if FEATURE_NAMED_VRAM
  #include "../assets/vram_named.h"
#endif
#if FEATURE_DISPLAY_LIST
  #include "../assets/display_list.h"
#endif
#include "../graphics/scissor.h"
#include "../protocol/packets.h"

// =============================================================================
// Deferred queue globals (exposed via profiles.h)
// =============================================================================
#if FEATURE_DEFERRED_DRAW
uint8_t  *g_deferred_queue      = NULL;
uint32_t  g_deferred_queue_size = 0;
uint32_t  g_deferred_queue_write = 0;
#endif


// =============================================================================
// PLL parameters for HSTX clock (TIP §6.2)
// pll_usb: VCO = XOSC(12MHz) * FBDIV, output = VCO / (postdiv1 * postdiv2)
//   480p60: VCO=756MHz (63*12), /6/1 = 126 MHz  -> pixel clock = 126/5 = 25.2 MHz
//   720p60: VCO=744MHz (62*12), /2/1 = 372 MHz  -> pixel clock = 372/5 = 74.4 MHz
// =============================================================================
#define PLL_VCO_480P   (756u * 1000000u)
#define PLL_DIV1_480P  6
#define PLL_DIV2_480P  1
#define HSTX_HZ_480P   (126u * 1000000u)

#define PLL_VCO_720P   (744u * 1000000u)
#define PLL_DIV1_720P  2
#define PLL_DIV2_720P  1
#define HSTX_HZ_720P   (372u * 1000000u)

// =============================================================================
// Profile Table (spec §3.4)
// =============================================================================
static const profile_t s_profiles[] = {
    // 0x01: 320×240, 8bpp, 2× scale, double-buffer, 640×480@60
    {
        .profile_id    = 0x01,
        .logical_w     = 320,   .logical_h    = 240,
        .scale_x       = 2,     .scale_y      = 2,
        .bpp_class     = 8,     .buffering    = BUFFERING_DOUBLE,
        .timing_id     = TIMING_640x480_60,
        .fb_size_bytes = 320*240,
        .sprite_vm_on  = 250*1024,
        .sprite_vm_off = 314*1024,
        .vm_heap_bytes = 64*1024,
        .pll_vco_hz    = PLL_VCO_480P, .pll_postdiv1 = PLL_DIV1_480P, .pll_postdiv2 = PLL_DIV2_480P,
        .clk_hstx_hz   = HSTX_HZ_480P,
    },
    // 0x02: 640×480, 8bpp, 1× scale, single-buffer, 640×480@60
    {
        .profile_id    = 0x02,
        .logical_w     = 640,   .logical_h    = 480,
        .scale_x       = 1,     .scale_y      = 1,
        .bpp_class     = 8,     .buffering    = BUFFERING_SINGLE,
        .timing_id     = TIMING_640x480_60,
        .fb_size_bytes = 640*480,
        .sprite_vm_on  = 100*1024,
        .sprite_vm_off = 164*1024,
        .vm_heap_bytes = 64*1024,
        .pll_vco_hz    = PLL_VCO_480P, .pll_postdiv1 = PLL_DIV1_480P, .pll_postdiv2 = PLL_DIV2_480P,
        .clk_hstx_hz   = HSTX_HZ_480P,
    },
    // 0x03: 320×180, 8bpp, 4× scale, double-buffer, 1280×720@60
    {
        .profile_id    = 0x03,
        .logical_w     = 320,   .logical_h    = 180,
        .scale_x       = 4,     .scale_y      = 4,
        .bpp_class     = 8,     .buffering    = BUFFERING_DOUBLE,
        .timing_id     = TIMING_1280x720_60,
        .fb_size_bytes = 320*180,
        .sprite_vm_on  = 223*1024 + 512,
        .sprite_vm_off = 351*1024 + 512,
        .vm_heap_bytes = 128*1024,
        .pll_vco_hz    = PLL_VCO_720P, .pll_postdiv1 = PLL_DIV1_720P, .pll_postdiv2 = PLL_DIV2_720P,
        .clk_hstx_hz   = HSTX_HZ_720P,
    },
    // 0x04: 640×360, 8bpp, 2× scale, single-buffer, 1280×720@60
    {
        .profile_id    = 0x04,
        .logical_w     = 640,   .logical_h    = 360,
        .scale_x       = 2,     .scale_y      = 2,
        .bpp_class     = 8,     .buffering    = BUFFERING_SINGLE,
        .timing_id     = TIMING_1280x720_60,
        .fb_size_bytes = 640*360,
        .sprite_vm_on  = 175*1024,
        .sprite_vm_off = 239*1024,
        .vm_heap_bytes = 64*1024,
        .pll_vco_hz    = PLL_VCO_720P, .pll_postdiv1 = PLL_DIV1_720P, .pll_postdiv2 = PLL_DIV2_720P,
        .clk_hstx_hz   = HSTX_HZ_720P,
    },
    // 0x05: 640×360, 8bpp, 2× scale, double-buffer, 1280×720@60 (VM unavailable)
    {
        .profile_id    = 0x05,
        .logical_w     = 640,   .logical_h    = 360,
        .scale_x       = 2,     .scale_y      = 2,
        .bpp_class     = 8,     .buffering    = BUFFERING_DOUBLE,
        .timing_id     = TIMING_1280x720_60,
        .fb_size_bytes = 640*360,
        .sprite_vm_on  = 18*1024,
        .sprite_vm_off = 18*1024,
        .vm_heap_bytes = 0,
        .pll_vco_hz    = PLL_VCO_720P, .pll_postdiv1 = PLL_DIV1_720P, .pll_postdiv2 = PLL_DIV2_720P,
        .clk_hstx_hz   = HSTX_HZ_720P,
    },
    // 0x11: 320×240, 16bpp, 2× scale, double-buffer, 640×480@60
    {
        .profile_id    = 0x11,
        .logical_w     = 320,   .logical_h    = 240,
        .scale_x       = 2,     .scale_y      = 2,
        .bpp_class     = 16,    .buffering    = BUFFERING_DOUBLE,
        .timing_id     = TIMING_640x480_60,
        .fb_size_bytes = 320*240*2,
        .sprite_vm_on  = 100*1024,
        .sprite_vm_off = 164*1024,
        .vm_heap_bytes = 64*1024,
        .pll_vco_hz    = PLL_VCO_480P, .pll_postdiv1 = PLL_DIV1_480P, .pll_postdiv2 = PLL_DIV2_480P,
        .clk_hstx_hz   = HSTX_HZ_480P,
    },
    // 0x12: 320×180, 16bpp, 4× scale, double-buffer, 1280×720@60
    {
        .profile_id    = 0x12,
        .logical_w     = 320,   .logical_h    = 180,
        .scale_x       = 4,     .scale_y      = 4,
        .bpp_class     = 16,    .buffering    = BUFFERING_DOUBLE,
        .timing_id     = TIMING_1280x720_60,
        .fb_size_bytes = 320*180*2,
        .sprite_vm_on  = 175*1024,
        .sprite_vm_off = 239*1024,
        .vm_heap_bytes = 64*1024,
        .pll_vco_hz    = PLL_VCO_720P, .pll_postdiv1 = PLL_DIV1_720P, .pll_postdiv2 = PLL_DIV2_720P,
        .clk_hstx_hz   = HSTX_HZ_720P,
    },
    // 0x13: 640×360, 16bpp, 2× scale, single-buffer, 1280×720@60 (VM unavailable)
    {
        .profile_id    = 0x13,
        .logical_w     = 640,   .logical_h    = 360,
        .scale_x       = 2,     .scale_y      = 2,
        .bpp_class     = 16,    .buffering    = BUFFERING_SINGLE,
        .timing_id     = TIMING_1280x720_60,
        .fb_size_bytes = 640*360*2,
        .sprite_vm_on  = 18*1024,
        .sprite_vm_off = 18*1024,
        .vm_heap_bytes = 0,
        .pll_vco_hz    = PLL_VCO_720P, .pll_postdiv1 = PLL_DIV1_720P, .pll_postdiv2 = PLL_DIV2_720P,
        .clk_hstx_hz   = HSTX_HZ_720P,
    },
};

#define NUM_PROFILES (sizeof(s_profiles) / sizeof(s_profiles[0]))

const profile_t *profile_lookup(uint8_t profile_id) {
    for (uint32_t i = 0; i < NUM_PROFILES; i++) {
        if (s_profiles[i].profile_id == profile_id) return &s_profiles[i];
    }
    return NULL;
}

// =============================================================================
// SYSTEM_CONFIG handler — full INITIALIZING sequence (spec §3.5)
// =============================================================================
void handle_system_config(const uint8_t *payload, uint16_t len) {
    if (len < 2) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    uint8_t profile_id = payload[0];
    uint8_t reserve_vm = payload[1];

    const profile_t *prof = profile_lookup(profile_id);
    if (!prof) { coprocessor_set_error(ERR_UNSUPPORTED_PROFILE); return; }

    if (reserve_vm && prof->vm_heap_bytes == 0) {
        coprocessor_set_error(ERR_VM_UNAVAILABLE); return;
    }

    // Assert BUSY — blocks host during INITIALIZING
    spi_set_busy(true);
    g_state.state = GPU_STATE_INITIALIZING;

    // 1. Stop video output immediately (before touching any FB pointers)
    display_rp2350_stop();

    // 2. Reset arena and re-allocate all regions BEFORE restarting video,
    //    so the scanline callback always sees valid framebuffer pointers.
    //    (Spec §3.5: arena repartition → then video restart.)
    arena_reset();

    uint32_t fb_align = 4;
    g_fb_back = arena_alloc(prof->fb_size_bytes, fb_align);
    if (!g_fb_back) {
        coprocessor_set_error(ERR_OUT_OF_MEMORY);
        spi_set_busy(false);
        g_state.state = GPU_STATE_UNINITIALIZED;
        return;
    }

    if (prof->buffering == BUFFERING_DOUBLE) {
        g_fb_front = arena_alloc(prof->fb_size_bytes, fb_align);
        if (!g_fb_front) {
            coprocessor_set_error(ERR_OUT_OF_MEMORY);
            spi_set_busy(false);
            g_state.state = GPU_STATE_UNINITIALIZED;
            return;
        }
    } else {
        g_fb_front = g_fb_back;
    }

    // Update framebuffer geometry
    g_fb_width  = prof->logical_w;
    g_fb_height = prof->logical_h;
    g_fb_bpp    = prof->bpp_class;
    g_fb_stride = (prof->bpp_class == 8) ? prof->logical_w : (prof->logical_w * 2u);
    g_fb_size   = prof->fb_size_bytes;

    // Allocate VRAM sprite cache
    uint32_t sprite_bytes = reserve_vm ? prof->sprite_vm_on : prof->sprite_vm_off;
    uint8_t *vram_ptr     = arena_alloc(sprite_bytes, 4);
    if (!vram_ptr && sprite_bytes > 0) {
        coprocessor_set_error(ERR_OUT_OF_MEMORY);
        spi_set_busy(false);
        g_state.state = GPU_STATE_UNINITIALIZED;
        return;
    }
    vram_init(vram_ptr, sprite_bytes);

    // Reserve VM heap
    if (reserve_vm && prof->vm_heap_bytes > 0) {
        arena_alloc(prof->vm_heap_bytes, 4); // reserved, Phase 4+
    }

    // 3. Now restart video with correct FB pointers in place
    display_rp2350_apply_profile(prof);

#if FEATURE_DEFERRED_DRAW
    // Allocate deferred draw queue for single-deferred profiles
    if (prof->buffering == BUFFERING_SINGLE_DEFERRED) {
        g_deferred_queue      = arena_alloc(DEFERRED_QUEUE_SIZE_BYTES, 4);
        g_deferred_queue_size = DEFERRED_QUEUE_SIZE_BYTES;
    } else {
        g_deferred_queue      = NULL;
        g_deferred_queue_size = 0;
    }
    g_deferred_queue_write = 0;
#endif

#if FEATURE_NAMED_VRAM
    vram_named_clear();
#endif
#if FEATURE_DISPLAY_LIST
    display_list_reset();
#endif

    // Update state
    g_state.active_profile_id   = profile_id;
    g_state.bpp_class           = prof->bpp_class;
    g_state.active_pixel_format = (prof->bpp_class == 16) ? PIXEL_FORMAT_RGB565 : PIXEL_FORMAT_RGB332;
    g_state.chroma_key_enabled  = false;
    g_state.chroma_key_color    = 0xF81F;
    g_state.dither_mode         = DITHER_NONE;
    g_state.blend_mode          = BLEND_OVERWRITE;
    g_state.swap_pending        = false;
    g_state.frame_count         = 0;
    g_state.last_error          = ERR_OK;
    g_state.state               = GPU_STATE_ACTIVE;

    spi_set_busy(false);
}

// =============================================================================
// SOFT_RESET
// =============================================================================
void handle_soft_reset(void) {
    display_rp2350_stop();
    arena_reset();
    g_fb_back = g_fb_front = NULL;
    g_fb_width = g_fb_height = 0;
    g_fb_stride = g_fb_size = 0;
    vram_init(NULL, 0);
#if FEATURE_DEFERRED_DRAW
    g_deferred_queue      = NULL;
    g_deferred_queue_size = 0;
    g_deferred_queue_write = 0;
#endif
#if FEATURE_NAMED_VRAM
    vram_named_clear();
#endif
#if FEATURE_DISPLAY_LIST
    display_list_reset();
#endif
    // H1 fix: reset scissor stack (spec §4.1 requires RESET to clear clip state)
    scissor_init();
    // H6 fix: reset protocol parser (abandon any partially-received packet)
    packets_init();
    coprocessor_state_init();
}
