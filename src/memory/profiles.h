#pragma once
// profiles.h — Display profile table and SYSTEM_CONFIG handler

#include <stdint.h>
#include <stdbool.h>

// Buffering mode constants (spec §3.6)
#define BUFFERING_DOUBLE          2
#define BUFFERING_SINGLE          1
#define BUFFERING_SINGLE_DEFERRED 3

// Output timing IDs
#define TIMING_640x480_60   0  // 640×480 @ 60 Hz, clk_hstx via pll_usb
#define TIMING_1280x720_60  1  // 1280×720 @ 60 Hz, clk_hstx via pll_usb

// Profile table entry (spec §3.4)
typedef struct profile_s {
    uint8_t  profile_id;
    uint16_t logical_w;
    uint16_t logical_h;
    uint8_t  scale_x;
    uint8_t  scale_y;
    uint8_t  bpp_class;    // 8 or 16
    uint8_t  buffering;    // BUFFERING_* constant
    uint8_t  timing_id;   // TIMING_* constant
    uint32_t fb_size_bytes;
    uint32_t sprite_vm_on;
    uint32_t sprite_vm_off;
    uint32_t vm_heap_bytes;
    // pll_usb parameters for HSTX clock (TIP §6.2)
    uint32_t pll_vco_hz;
    uint8_t  pll_postdiv1;
    uint8_t  pll_postdiv2;
    uint32_t clk_hstx_hz;
} profile_t;

// Returns pointer to profile_t for the given profile_id, or NULL if unknown.
const profile_t *profile_lookup(uint8_t profile_id);

// SYSTEM_CONFIG command handler (spec §3.5)
// Payload: profile_id(1B), reserve_vm(1B)
void handle_system_config(const uint8_t *payload, uint16_t len);

// SOFT_RESET handler — return to UNINITIALIZED state, stop DVI output
void handle_soft_reset(void);
