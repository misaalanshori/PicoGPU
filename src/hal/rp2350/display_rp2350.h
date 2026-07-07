#pragma once
// display_rp2350.h — RP2350 Display HAL (wraps pico_hdmi API)

#include <stdint.h>
#include <stdbool.h>
// Forward-declare profile_t to avoid circular include
typedef struct profile_s profile_t;

// Initialize pico_hdmi in UNINITIALIZED state (no DVI output yet).
// Configure TE GPIO as output.
void display_rp2350_init(void);

// Apply a profile: retune pll_usb, switch video mode (if first call: init),
// start DVI output. Blocks briefly for pll_usb to stabilize.
void display_rp2350_apply_profile(const profile_t *prof);

// Stop DVI output (video_output_stop). Used by SOFT_RESET.
void display_rp2350_stop(void);

// Swap framebuffer front/back pointers atomically.
// Can be called from VSYNC callback context (Core 1) or Core 0 for immediate swap.
void display_rp2350_swap_buffers(void);

// Core 1 entry point — runs pico_hdmi polling loop. Never returns.
void display_rp2350_core1_entry(void);
