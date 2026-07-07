#pragma once
// effects.h — Phase 1 pixel-write helper (chroma key + direct write)
// Phase 2 will add: dithering, blend modes, scissor clipping.

#include <stdint.h>
#include "framebuffer.h"
#include "../state/coprocessor_state.h"

// Write a single pixel to the back framebuffer.
// 'color' is always 16-bit (RGB565 storage); for 8bpp only the lower byte is used.
// Bounds checked. Chroma key applied if g_state.chroma_key_enabled.
void effect_write_pixel(int16_t x, int16_t y, uint16_t color);

// Fill a horizontal span [x0, x1] inclusive on row y.
// Faster than calling effect_write_pixel() per pixel for solid fills.
// Chroma key NOT applied for fill operations (fills overwrite unconditionally).
void effect_fill_hspan(int16_t x0, int16_t x1, int16_t y, uint16_t color);

// Framebuffer globals — defined in profiles.c, declared here for graphics code.
extern uint8_t  *g_fb_back;
extern uint8_t  *g_fb_front;
extern uint16_t  g_fb_width;
extern uint16_t  g_fb_height;
extern uint32_t  g_fb_stride;
extern uint8_t   g_fb_bpp;
extern uint32_t  g_fb_size;
