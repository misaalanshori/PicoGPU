// effects.c — Phase 2 pixel-write implementation
// Gate order in effect_write_pixel (spec §6.3, §6.5, TIP §7 Phase 2):
//   1. Bounds check
//   2. Scissor test  ← Phase 2
//   3. Chroma key
//   4. Dithering     ← Phase 2
//   5. Blend mode    ← Phase 2
//   6. Framebuffer write

#include "effects.h"
#include "scissor.h"
#include "../../include/opcodes.h"
#include <string.h>

// Framebuffer globals — defined here, declared extern in effects.h / framebuffer.h
uint8_t  *g_fb_back   = NULL;
uint8_t  *g_fb_front  = NULL;
uint16_t  g_fb_width  = 0;
uint16_t  g_fb_height = 0;
uint32_t  g_fb_stride = 0;
uint8_t   g_fb_bpp    = 8;
uint32_t  g_fb_size   = 0;

// =============================================================================
// Bayer ordered-dither matrices (spec §6.4 — matrix sizes specified, values canonical)
// =============================================================================
// Values are 0-based thresholds; applied as a small bias to the raw pixel value
// before writing, distributing quantisation error spatially.

// Standard 2×2 Bayer matrix (values 0–3)
static const uint8_t s_bayer2[2][2] = {
    {0, 2},
    {3, 1},
};

// Standard 4×4 Bayer matrix (values 0–15)
static const uint8_t s_bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// Apply Bayer bias to an 8-bit packed pixel value.
// bias_step: 4 for BAYER2 (range 0-12), 1 for BAYER4 (range 0-15).
static inline uint8_t dither8(uint8_t val, uint8_t bayer_val, uint8_t bias_step) {
    uint16_t biased = (uint16_t)val + (uint16_t)(bayer_val * bias_step);
    return (biased > 255u) ? 255u : (uint8_t)biased;
}

// Apply Bayer bias to a 16-bit pixel (applied to each byte independently).
static inline uint16_t dither16(uint16_t val, uint8_t bayer_val, uint8_t bias_step) {
    uint8_t lo = dither8((uint8_t)(val & 0xFF),         bayer_val, bias_step);
    uint8_t hi = dither8((uint8_t)((val >> 8) & 0xFF),  bayer_val, bias_step);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

// =============================================================================
// effect_write_pixel — single central pixel-write path for all rendering
// =============================================================================
void effect_write_pixel(int16_t x, int16_t y, uint16_t color) {
    // 1. Bounds check
    if ((uint16_t)x >= g_fb_width || (uint16_t)y >= g_fb_height) return;

    // 2. Scissor test (Phase 2)
    if (!scissor_test(x, y)) return;

    // 3. Chroma key (spec §6.5)
    if (g_state.chroma_key_enabled) {
        if (g_fb_bpp == 8  && (uint8_t)(color & 0xFF) == (uint8_t)(g_state.chroma_key_color & 0xFF)) return;
        if (g_fb_bpp == 16 && color == g_state.chroma_key_color) return;
    }

    // 4. Dithering (Phase 2, spec §6.4)
    if (g_state.dither_mode != DITHER_NONE) {
        uint8_t bv;
        uint8_t step;
        if (g_state.dither_mode == DITHER_BAYER2) {
            bv   = s_bayer2[(uint8_t)y & 1u][(uint8_t)x & 1u];
            step = 4u;  // range 0–12 (gentle bias over 8-bit range)
        } else {        // DITHER_BAYER4
            bv   = s_bayer4[(uint8_t)y & 3u][(uint8_t)x & 3u];
            step = 1u;  // range 0–15 (fine grain)
        }
        if (g_fb_bpp == 8) {
            color = (uint16_t)dither8((uint8_t)(color & 0xFF), bv, step);
        } else {
            color = dither16(color, bv, step);
        }
    }

    // 5. Blend mode (Phase 2, spec §6.5 architectural note — raw packed-pixel operation)
    if (g_fb_bpp == 8) {
        uint32_t off = (uint32_t)y * g_fb_stride + (uint32_t)x;
        uint8_t  raw = (uint8_t)(color & 0xFF);
        if (g_state.blend_mode != BLEND_OVERWRITE) {
            uint8_t existing = g_fb_back[off];
            switch (g_state.blend_mode) {
                case BLEND_XOR: raw ^= existing; break;
                case BLEND_OR:  raw |= existing; break;
                case BLEND_AND: raw &= existing; break;
                default: break;
            }
        }
        // 6. Write
        g_fb_back[off] = raw;
    } else { // 16bpp
        uint32_t  off  = (uint32_t)y * g_fb_width + (uint32_t)x;
        uint16_t *fb16 = (uint16_t *)g_fb_back;
        uint16_t  raw  = color;
        if (g_state.blend_mode != BLEND_OVERWRITE) {
            uint16_t existing = fb16[off];
            switch (g_state.blend_mode) {
                case BLEND_XOR: raw ^= existing; break;
                case BLEND_OR:  raw |= existing; break;
                case BLEND_AND: raw &= existing; break;
                default: break;
            }
        }
        // 6. Write
        fb16[off] = raw;
    }
}

// =============================================================================
// effect_fill_hspan — fast horizontal span fill
// Scissor-clips the span first; no dithering or blend (fills are unconditional
// overwrite — consistent with the spec's §6.5 note that fill ops overwrite).
// =============================================================================
void effect_fill_hspan(int16_t x0, int16_t x1, int16_t y, uint16_t color) {
    // Framebuffer bounds
    if (y < 0 || (uint16_t)y >= g_fb_height) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int16_t)g_fb_width) x1 = (int16_t)g_fb_width - 1;
    if (x0 > x1) return;

    // Scissor clip (Phase 2)
    if (!scissor_clip_hspan(&x0, &x1, y)) return;

    uint32_t count = (uint32_t)(x1 - x0 + 1);
    if (g_fb_bpp == 8) {
        memset(g_fb_back + (uint32_t)y * g_fb_stride + (uint32_t)x0,
               (uint8_t)(color & 0xFF), count);
    } else { // 16bpp
        uint16_t *row = (uint16_t *)g_fb_back + (uint32_t)y * g_fb_width + (uint32_t)x0;
        for (uint32_t i = 0; i < count; i++) row[i] = color;
    }
}
