// effects.c — Phase 2 pixel-write implementation
// Gate order in effect_write_pixel (spec §6.3, §6.5, TIP §7 Phase 2):
//   1. Bounds check
//   2. Scissor test  ← Phase 2
//   3. Chroma key
//   4. Dithering     ← Phase 2  (H2 fix: per-channel, not raw packed-byte)
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
// Bayer ordered-dither matrices (spec §6.4)
// =============================================================================
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

// =============================================================================
// Dithering helpers — per-channel (H2 fix)
// =============================================================================
// Bias a single channel value, clamping to max_val.
static inline uint8_t _channel_bias(uint8_t ch_val, uint8_t max_val, uint8_t bias) {
    uint16_t v = (uint16_t)ch_val + (uint16_t)bias;
    return (v >= (uint16_t)max_val) ? max_val : (uint8_t)v;
}

// Apply Bayer dither to a packed RGB332 byte.
// RGB332: R[7:5]=3b, G[4:2]=3b, B[1:0]=2b
// bayer_val must be normalised to 0-15.
static inline uint8_t dither_rgb332(uint8_t val, uint8_t bayer_val) {
    uint8_t r = (val >> 5) & 0x07u;  // 3-bit [0..7]
    uint8_t g = (val >> 2) & 0x07u;  // 3-bit [0..7]
    uint8_t b =  val       & 0x03u;  // 2-bit [0..3]
    // Scale bayer_val (0-15) into per-channel biases:
    //   3-bit channel: bias = bayer_val >> 1  (0-7)
    //   2-bit channel: threshold at 8/16      (0 or 1)
    uint8_t bias3 = bayer_val >> 1;
    uint8_t bias2 = (bayer_val >= 8u) ? 1u : 0u;
    r = _channel_bias(r, 7u,  bias3);
    g = _channel_bias(g, 7u,  bias3);
    b = _channel_bias(b, 3u,  bias2);
    return (uint8_t)((r << 5) | (g << 2) | b);
}

// Apply Bayer dither to a packed RGB565 16-bit pixel.
// RGB565: R[15:11]=5b, G[10:5]=6b, B[4:0]=5b
// bayer_val must be normalised to 0-15.
static inline uint16_t dither_rgb565(uint16_t val, uint8_t bayer_val) {
    uint8_t r = (uint8_t)((val >> 11) & 0x1Fu); // 5-bit [0..31]
    uint8_t g = (uint8_t)((val >>  5) & 0x3Fu); // 6-bit [0..63]
    uint8_t b = (uint8_t)( val        & 0x1Fu); // 5-bit [0..31]
    // Scale bayer_val (0-15) into per-channel biases:
    //   5-bit: bayer >> 3 (0-1)
    //   6-bit: bayer >> 2 (0-3)
    uint8_t bias5 = bayer_val >> 3;
    uint8_t bias6 = bayer_val >> 2;
    r = _channel_bias(r, 31u, bias5);
    g = _channel_bias(g, 63u, bias6);
    b = _channel_bias(b, 31u, bias5);
    return (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | b);
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

    // 4. Dithering (Phase 2, spec §6.4) — per-channel to avoid cross-field bleed
    if (g_state.dither_mode != DITHER_NONE) {
        // Normalise bayer value to 0-15 for both matrix sizes.
        // BAYER2 values are 0-3; multiply ×4 to bring into 0-15 range.
        uint8_t bv;
        if (g_state.dither_mode == DITHER_BAYER2) {
            bv = (uint8_t)(s_bayer2[(uint8_t)y & 1u][(uint8_t)x & 1u] << 2u);
        } else { // DITHER_BAYER4
            bv = s_bayer4[(uint8_t)y & 3u][(uint8_t)x & 3u];
        }
        if (g_fb_bpp == 8) {
            color = (uint16_t)dither_rgb332((uint8_t)(color & 0xFF), bv);
        } else {
            color = dither_rgb565(color, bv);
        }
    }

    // 5. Blend mode (Phase 2, spec §6.5)
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
