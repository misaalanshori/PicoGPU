#pragma once
// framebuffer.h — Framebuffer pointers shared between graphics/ and hal/
// Managed by profiles.c during SYSTEM_CONFIG; read by all drawing code.

#include <stdint.h>

// In double-buffer mode: g_fb_back = draw target, g_fb_front = scanout source.
// In single-buffer mode: both point to the same allocation.
extern uint8_t  *g_fb_back;     // Core 0 draws here
extern uint8_t  *g_fb_front;    // Core 1 (pico_hdmi) scans from here

extern uint16_t  g_fb_width;    // logical framebuffer width in pixels
extern uint16_t  g_fb_height;   // logical framebuffer height in pixels
extern uint32_t  g_fb_stride;   // bytes per row (width * bytes_per_pixel)
extern uint8_t   g_fb_bpp;      // bits per pixel (8 or 16 in Phase 0/1)
extern uint32_t  g_fb_size;     // bytes per single framebuffer
