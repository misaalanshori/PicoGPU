#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   // for size_t (used by event drain)

// =============================================================================
// Coprocessor State (spec §3)
// =============================================================================
// Global GPU state machine and configuration registers.

typedef enum {
    GPU_STATE_UNINITIALIZED = 0,  // Before SYSTEM_CONFIG
    GPU_STATE_INITIALIZING  = 1,  // During SYSTEM_CONFIG processing
    GPU_STATE_ACTIVE        = 2,  // Ready to accept drawing commands
} gpu_state_e;

typedef struct {
    gpu_state_e state;
    uint8_t     active_profile_id;   // 0xFF = none (UNINITIALIZED)
    uint8_t     last_error;          // ERR_* code from last command
    uint8_t     active_pixel_format; // PIXEL_FORMAT_* enum
    uint8_t     bpp_class;           // 4, 8, 16, or 24
    uint8_t     dither_mode;         // DITHER_*
    uint8_t     blend_mode;          // BLEND_*
    bool        chroma_key_enabled;  // false at boot
    uint16_t    chroma_key_color;    // RGB565; default 0xF81F (magenta)
    bool        frame_stats_enabled; // false at boot
    bool        swap_pending;        // SWAP_BUFFERS deferred to VBLANK
    uint32_t    frame_count;         // incremented each END_FRAME
    uint16_t    last_render_ms;      // frame render time in ms (Phase 2)
    uint8_t     ring_peak_pct;       // peak ring-buffer fill % since last BEGIN_FRAME
    uint8_t     missed_frames;       // count of frames where swap was not consumed by VBLANK
    // Phase 2 frame timing
    uint64_t    frame_start_us;      // time_us_64() captured at BEGIN_FRAME
    bool        in_frame;            // true between BEGIN_FRAME and END_FRAME
} gpu_state_t;

extern gpu_state_t g_state;

// Initialize g_state to safe power-on defaults.
void coprocessor_state_init(void);

// Record an error code in g_state.last_error.
void coprocessor_set_error(uint8_t err);
