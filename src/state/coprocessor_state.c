// coprocessor_state.c — Global GPU state initialization and helpers

#include "coprocessor_state.h"
#include "opcodes.h"
#include "error_codes.h"

gpu_state_t g_state;

void coprocessor_state_init(void) {
    g_state.state               = GPU_STATE_UNINITIALIZED;
    g_state.active_profile_id   = 0xFF;   // sentinel: no profile active
    g_state.last_error          = ERR_OK;
    g_state.active_pixel_format = PIXEL_FORMAT_RGB332; // 8bpp default
    g_state.bpp_class           = 8;
    g_state.dither_mode         = DITHER_NONE;
    g_state.blend_mode          = BLEND_OVERWRITE;
    g_state.chroma_key_enabled  = false;
    g_state.chroma_key_color    = 0xF81F;  // magenta (spec §6.5 default)
    g_state.frame_stats_enabled = false;
    g_state.swap_pending        = false;
    g_state.frame_count         = 0;
    g_state.last_render_ms      = 0;
    g_state.ring_peak_pct       = 0;
    g_state.missed_frames       = 0;
}

void coprocessor_set_error(uint8_t err) {
    g_state.last_error = err;
}
