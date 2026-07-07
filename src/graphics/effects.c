// effects.c — Phase 1 pixel-write implementation
// Spec §6: chroma key applied to all pixel writes. Dithering and blend modes in Phase 2.

#include "effects.h"
#include <string.h>

// Framebuffer globals — defined here, declared extern everywhere else.
uint8_t  *g_fb_back   = NULL;
uint8_t  *g_fb_front  = NULL;
uint16_t  g_fb_width  = 0;
uint16_t  g_fb_height = 0;
uint32_t  g_fb_stride = 0;
uint8_t   g_fb_bpp    = 8;
uint32_t  g_fb_size   = 0;

void effect_write_pixel(int16_t x, int16_t y, uint16_t color) {
    // Bounds check
    if ((uint16_t)x >= g_fb_width || (uint16_t)y >= g_fb_height) return;

    // Chroma key check (spec §6.5)
    if (g_state.chroma_key_enabled) {
        if (g_fb_bpp == 8  && (uint8_t)(color & 0xFF) == (uint8_t)(g_state.chroma_key_color & 0xFF)) return;
        if (g_fb_bpp == 16 && color == g_state.chroma_key_color) return;
    }

    // Write pixel
    if (g_fb_bpp == 8) {
        g_fb_back[(uint32_t)y * g_fb_stride + (uint32_t)x] = (uint8_t)(color & 0xFF);
    } else { // 16bpp
        uint16_t *fb16 = (uint16_t *)g_fb_back;
        fb16[(uint32_t)y * g_fb_width + (uint32_t)x] = color;
    }
}

void effect_fill_hspan(int16_t x0, int16_t x1, int16_t y, uint16_t color) {
    // Clip to framebuffer
    if (y < 0 || (uint16_t)y >= g_fb_height) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int16_t)g_fb_width) x1 = (int16_t)g_fb_width - 1;
    if (x0 > x1) return;

    uint32_t count = (uint32_t)(x1 - x0 + 1);
    if (g_fb_bpp == 8) {
        memset(g_fb_back + (uint32_t)y * g_fb_stride + (uint32_t)x0,
               (uint8_t)(color & 0xFF), count);
    } else { // 16bpp
        uint16_t *row = (uint16_t *)g_fb_back + (uint32_t)y * g_fb_width + (uint32_t)x0;
        for (uint32_t i = 0; i < count; i++) row[i] = color;
    }
}
