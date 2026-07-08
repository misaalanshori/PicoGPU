// primitives_fpu.c — FPU-only DRAW_PRIMITIVE sub-opcodes (spec §6.1)
// Compiled only when FEATURE_FPU_PRIMITIVES=1 (RP2350 target).
// Sub-opcodes: 0x0F TRIANGLE_GRADIENT, 0x11 BEZIER_QUAD,
//              0x12 BEZIER_CUBIC,      0x13 GRADIENT_RECT

#include "primitives_fpu.h"
#include "effects.h"
#include "framebuffer.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"
#include "feature_flags.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

#if FEATURE_FPU_PRIMITIVES

// Sub-opcode constants
#define PRIM_TRIANGLE_GRADIENT 0x0F
#define PRIM_BEZIER_QUAD       0x11
#define PRIM_BEZIER_CUBIC      0x12
#define PRIM_GRADIENT_RECT     0x13

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline int16_t  rd16s_fpu(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static inline uint16_t rd16u_fpu(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// ---------------------------------------------------------------------------
// PRIM_BEZIER_QUAD (0x11)
// ---------------------------------------------------------------------------

void prim_bezier_quad(const uint8_t *payload, uint16_t len) {
    if (len < 16) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0    = rd16s_fpu(payload + 1);
    int16_t  y0    = rd16s_fpu(payload + 3);
    int16_t  cx    = rd16s_fpu(payload + 5);
    int16_t  cy    = rd16s_fpu(payload + 7);
    int16_t  x1    = rd16s_fpu(payload + 9);
    int16_t  y1    = rd16s_fpu(payload + 11);
    uint8_t  steps = payload[13];
    uint16_t color = rd16u_fpu(payload + 14);
    if (steps == 0) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    for (uint8_t i = 0; i <= steps; i++) {
        float t  = (float)i / (float)steps;
        float mt = 1.0f - t;
        float px = mt*mt*(float)x0 + 2.0f*mt*t*(float)cx + t*t*(float)x1;
        float py = mt*mt*(float)y0 + 2.0f*mt*t*(float)cy + t*t*(float)y1;
        effect_write_pixel((int16_t)px, (int16_t)py, color);
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// PRIM_BEZIER_CUBIC (0x12)
// ---------------------------------------------------------------------------

void prim_bezier_cubic(const uint8_t *payload, uint16_t len) {
    if (len < 20) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0    = rd16s_fpu(payload + 1);
    int16_t  y0    = rd16s_fpu(payload + 3);
    int16_t  cx0   = rd16s_fpu(payload + 5);
    int16_t  cy0   = rd16s_fpu(payload + 7);
    int16_t  cx1   = rd16s_fpu(payload + 9);
    int16_t  cy1   = rd16s_fpu(payload + 11);
    int16_t  x1    = rd16s_fpu(payload + 13);
    int16_t  y1    = rd16s_fpu(payload + 15);
    uint8_t  steps = payload[17];
    uint16_t color = rd16u_fpu(payload + 18);
    if (steps == 0) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    for (uint8_t i = 0; i <= steps; i++) {
        float t   = (float)i / (float)steps;
        float mt  = 1.0f - t;
        float mt2 = mt * mt;
        float mt3 = mt2 * mt;
        float t2  = t * t;
        float t3  = t2 * t;
        float px = mt3*(float)x0 + 3.0f*mt2*t*(float)cx0 + 3.0f*mt*t2*(float)cx1 + t3*(float)x1;
        float py = mt3*(float)y0 + 3.0f*mt2*t*(float)cy0 + 3.0f*mt*t2*(float)cy1 + t3*(float)y1;
        effect_write_pixel((int16_t)px, (int16_t)py, color);
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// PRIM_GRADIENT_RECT (0x13)
// ---------------------------------------------------------------------------

// Lerp a 16-bit color component-wise (RGB565)
static inline uint16_t lerp_color_rgb565(uint16_t c0, uint16_t c1, float t) {
    uint8_t r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r  = (uint8_t)((float)r0 + ((float)r1 - (float)r0) * t + 0.5f);
    uint8_t g  = (uint8_t)((float)g0 + ((float)g1 - (float)g0) * t + 0.5f);
    uint8_t b  = (uint8_t)((float)b0 + ((float)b1 - (float)b0) * t + 0.5f);
    return (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | b);
}

void prim_gradient_rect(const uint8_t *payload, uint16_t len) {
    if (len < 14) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  rx  = rd16s_fpu(payload + 1);
    int16_t  ry  = rd16s_fpu(payload + 3);
    int16_t  rw  = rd16s_fpu(payload + 5);
    int16_t  rh  = rd16s_fpu(payload + 7);
    uint16_t c0  = rd16u_fpu(payload + 9);
    uint16_t c1  = rd16u_fpu(payload + 11);
    uint8_t  dir = payload[13];
    if (rw <= 0 || rh <= 0) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    if (dir == 1) {
        // Vertical: each row gets a uniform color
        for (int16_t dy = 0; dy < rh; dy++) {
            float t = (rh > 1) ? (float)dy / (float)(rh - 1) : 0.0f;
            uint16_t col = lerp_color_rgb565(c0, c1, t);
            effect_fill_hspan(rx, rx + rw - 1, ry + dy, col);
        }
    } else {
        // Horizontal: each column gets a uniform color, iterate rows per column
        for (int16_t dx = 0; dx < rw; dx++) {
            float t = (rw > 1) ? (float)dx / (float)(rw - 1) : 0.0f;
            uint16_t col = lerp_color_rgb565(c0, c1, t);
            for (int16_t dy = 0; dy < rh; dy++) {
                effect_write_pixel(rx + dx, ry + dy, col);
            }
        }
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// PRIM_TRIANGLE_GRADIENT (0x0F)
// ---------------------------------------------------------------------------

void prim_triangle_gradient(const uint8_t *payload, uint16_t len) {
    if (len < 19) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0 = rd16s_fpu(payload + 1);
    int16_t  y0 = rd16s_fpu(payload + 3);
    uint16_t c0 = rd16u_fpu(payload + 5);
    int16_t  x1 = rd16s_fpu(payload + 7);
    int16_t  y1 = rd16s_fpu(payload + 9);
    uint16_t c1 = rd16u_fpu(payload + 11);
    int16_t  x2 = rd16s_fpu(payload + 13);
    int16_t  y2 = rd16s_fpu(payload + 15);
    uint16_t c2 = rd16u_fpu(payload + 17);

    // Compute signed area denominator for barycentric coords
    float denom = (float)((y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2));
    if (denom == 0.0f) { coprocessor_set_error(ERR_OK); return; } // degenerate triangle

    // Decompose vertex colors into RGB565 channels
    float r0 = (float)((c0 >> 11) & 0x1F);
    float g0 = (float)((c0 >>  5) & 0x3F);
    float b0 = (float)( c0        & 0x1F);
    float r1 = (float)((c1 >> 11) & 0x1F);
    float g1 = (float)((c1 >>  5) & 0x3F);
    float b1 = (float)( c1        & 0x1F);
    float r2 = (float)((c2 >> 11) & 0x1F);
    float g2 = (float)((c2 >>  5) & 0x3F);
    float b2 = (float)( c2        & 0x1F);

    // Compute bounding box
    int16_t xmin = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int16_t xmax = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int16_t ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int16_t ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

    for (int16_t py = ymin; py <= ymax; py++) {
        for (int16_t px = xmin; px <= xmax; px++) {
            float w0 = (float)((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / denom;
            float w1 = (float)((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / denom;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
            uint8_t r = (uint8_t)(w0 * r0 + w1 * r1 + w2 * r2 + 0.5f);
            uint8_t g = (uint8_t)(w0 * g0 + w1 * g1 + w2 * g2 + 0.5f);
            uint8_t b = (uint8_t)(w0 * b0 + w1 * b1 + w2 * b2 + 0.5f);
            uint16_t col = (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | b);
            effect_write_pixel(px, py, col);
        }
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void handle_draw_primitive_fpu(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    switch (payload[0]) {
        case PRIM_BEZIER_QUAD:       prim_bezier_quad(payload, len);       break;
        case PRIM_BEZIER_CUBIC:      prim_bezier_cubic(payload, len);      break;
        case PRIM_GRADIENT_RECT:     prim_gradient_rect(payload, len);     break;
        case PRIM_TRIANGLE_GRADIENT: prim_triangle_gradient(payload, len); break;
        default: coprocessor_set_error(ERR_UNKNOWN_OPCODE); break;
    }
}

#endif // FEATURE_FPU_PRIMITIVES
