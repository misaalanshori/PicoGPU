#pragma once
// display_rp2350.h — RP2350 Display HAL (wraps pico_hdmi RT API)

#include <stdint.h>
#include <stdbool.h>
// Forward-declare profile_t to avoid circular include
typedef struct profile_s profile_t;

// Initialize GPIO for TE pin. Call once from main() before Core 1 launch.
void display_rp2350_init(void);

// Apply a profile: retune pll_usb, configure clk_hstx, switch video mode,
// set HSTX pixel format expand registers, restart DVI.
// Core 1 must NOT be running yet on the first call (called before multicore_launch).
// For subsequent calls (profile switches), Core 1 must be running.
void display_rp2350_apply_profile(const profile_t *prof);

// Call this immediately after multicore_launch_core1() so that subsequent
// display_rp2350_apply_profile() calls know Core 1 is live.
void display_rp2350_mark_core1_started(void);

// Stop DVI output (video_output_stop). Used by SOFT_RESET.
void display_rp2350_stop(void);

// Atomically swap framebuffer front/back pointers.
// Safe to call from VSYNC callback context (Core 1) or Core 0.
void display_rp2350_swap_buffers(void);

// Core 1 entry point — runs pico_hdmi polling loop. Never returns.
void display_rp2350_core1_entry(void);

