// primitives.c — Integer drawing primitives (spec §6.1)
// Implements DRAW_PRIMITIVE sub-opcodes 0x01–0x0E, 0x10, 0x14.
// All algorithms are integer-only; no FPU required.
// Pixel writes go through effect_write_pixel() / effect_fill_hspan().

#include "primitives.h"
#include "effects.h"
#include "framebuffer.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"
#include "opcodes.h"
#include "feature_flags.h"

#include <stdlib.h>
#include <string.h>

// =============================================================================
// Payload parsing helpers
// =============================================================================
static inline int16_t  rd16s(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static inline uint16_t rd16u(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// Color is always 2 bytes LE in the packet payload; for 8bpp only lower byte used.
static inline uint16_t read_color(const uint8_t *p) { return rd16u(p); }

// =============================================================================
// Inline swap / abs helpers
// =============================================================================
static inline void swap_i16(int16_t *a, int16_t *b) { int16_t t = *a; *a = *b; *b = t; }
static inline int16_t abs16(int16_t v)               { return v < 0 ? -v : v; }
static inline int imax(int a, int b)                  { return a > b ? a : b; }
static inline int imin(int a, int b)                  { return a < b ? a : b; }

// =============================================================================
// 0x01 SET_PIXEL
// Payload: sub_op(1), x(2), y(2), color(2)
// =============================================================================
static void prim_set_pixel(const uint8_t *p, uint16_t len) {
    if (len < 7) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x     = rd16s(p + 1);
    int16_t  y     = rd16s(p + 3);
    uint16_t color = read_color(p + 5);
    effect_write_pixel(x, y, color);
}

// =============================================================================
// 0x02 LINE — Bresenham line, with thickness support
// Payload: sub_op(1), x0(2), y0(2), x1(2), y1(2), thickness(1), color(2)
// =============================================================================
static void bresenham_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t dx = abs16(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs16(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    while (1) {
        effect_write_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void prim_line(const uint8_t *p, uint16_t len) {
    if (len < 11) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0        = rd16s(p + 1);
    int16_t  y0        = rd16s(p + 3);
    int16_t  x1        = rd16s(p + 5);
    int16_t  y1        = rd16s(p + 7);
    uint8_t  thickness = p[9];
    uint16_t color     = read_color(p + 10);

    if (thickness <= 1) {
        bresenham_line(x0, y0, x1, y1, color);
    } else {
        // Fat line: draw parallel copies offset perpendicular to line direction
        int16_t dx = x1 - x0, dy = y1 - y0;
        int16_t half = thickness / 2;
        if (abs16(dx) > abs16(dy)) {
            // Mostly horizontal: offset in Y
            for (int16_t oy = -half; oy <= half; oy++) {
                bresenham_line(x0, y0 + oy, x1, y1 + oy, color);
            }
        } else {
            // Mostly vertical: offset in X
            for (int16_t ox = -half; ox <= half; ox++) {
                bresenham_line(x0 + ox, y0, x1 + ox, y1, color);
            }
        }
    }
}

// =============================================================================
// 0x03 LINE_DASHED — Bresenham with on/off counters
// Payload: sub_op(1), x0(2), y0(2), x1(2), y1(2), dash_on(1), dash_off(1), color(2)
// =============================================================================
static void prim_line_dashed(const uint8_t *p, uint16_t len) {
    if (len < 12) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0       = rd16s(p + 1);
    int16_t  y0       = rd16s(p + 3);
    int16_t  x1       = rd16s(p + 5);
    int16_t  y1       = rd16s(p + 7);
    uint8_t  dash_on  = p[9];
    uint8_t  dash_off = p[10];
    uint16_t color    = read_color(p + 11);
    if (dash_on  == 0) dash_on  = 4;
    if (dash_off == 0) dash_off = 4;

    int16_t dx = abs16(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs16(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    uint8_t count = 0, segment = 0; // segment 0=ON, 1=OFF
    uint8_t remaining = dash_on;
    while (1) {
        if (segment == 0) effect_write_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        if (--remaining == 0) {
            segment ^= 1;
            remaining = segment ? dash_off : dash_on;
        }
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        (void)count;
    }
}

// =============================================================================
// 0x04 RECT — outline rectangle with border_width
// Payload: sub_op(1), x(2), y(2), w(2), h(2), border_width(1), color(2)
// =============================================================================
static void prim_rect(const uint8_t *p, uint16_t len) {
    if (len < 12) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x  = rd16s(p + 1);
    int16_t  y  = rd16s(p + 3);
    int16_t  w  = rd16s(p + 5);
    int16_t  h  = rd16s(p + 7);
    uint8_t  bw = p[9];
    uint16_t c  = read_color(p + 10);
    if (bw == 0) bw = 1;
    for (uint8_t i = 0; i < bw; i++) {
        effect_fill_hspan(x + i, x + w - 1 - i, y + i, c);          // top
        effect_fill_hspan(x + i, x + w - 1 - i, y + h - 1 - i, c); // bottom
        for (int16_t row = y + i; row <= y + h - 1 - i; row++) {
            effect_write_pixel(x + i,         row, c); // left
            effect_write_pixel(x + w - 1 - i, row, c); // right
        }
    }
}

// =============================================================================
// 0x05 RECT_FILLED
// Payload: sub_op(1), x(2), y(2), w(2), h(2), color(2)
// =============================================================================
static void prim_rect_filled(const uint8_t *p, uint16_t len) {
    if (len < 11) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x = rd16s(p + 1);
    int16_t  y = rd16s(p + 3);
    int16_t  w = rd16s(p + 5);
    int16_t  h = rd16s(p + 7);
    uint16_t c = read_color(p + 9);
    for (int16_t row = y; row < y + h; row++) {
        effect_fill_hspan(x, x + w - 1, row, c);
    }
}

// =============================================================================
// Midpoint circle helper (octant symmetry, used by circle, arc, rounded rect)
// =============================================================================
static void circle_plot8(int16_t cx, int16_t cy, int16_t x, int16_t y, uint16_t c, uint8_t bw) {
    for (int16_t t = 0; t < bw; t++) {
        effect_write_pixel(cx + x - t, cy + y, c);
        effect_write_pixel(cx - x + t, cy + y, c);
        effect_write_pixel(cx + x - t, cy - y, c);
        effect_write_pixel(cx - x + t, cy - y, c);
        effect_write_pixel(cx + y, cy + x - t, c);
        effect_write_pixel(cx - y, cy + x - t, c);
        effect_write_pixel(cx + y, cy - x + t, c);
        effect_write_pixel(cx - y, cy - x + t, c);
    }
}

// =============================================================================
// 0x06 RECT_ROUNDED — outline; 0x07 RECT_ROUNDED_FILLED
// Payload 0x06: sub_op(1), x(2), y(2), w(2), h(2), radius(1), border_width(1), color(2)
// Payload 0x07: sub_op(1), x(2), y(2), w(2), h(2), radius(1), color(2)
// =============================================================================
static void draw_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint8_t bw, bool filled, uint16_t c) {
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    if (filled) {
        // Fill horizontal strips
        for (int16_t row = y + r; row <= y + h - 1 - r; row++) {
            effect_fill_hspan(x, x + w - 1, row, c);
        }
        // Fill rounded corners using circle spans
        int16_t px = r, py = 0;
        int16_t err2 = 1 - r;
        while (px >= py) {
            // top-left / top-right / bottom-left / bottom-right caps
            effect_fill_hspan(x + r - px, x + w - 1 - r + px, y + r - py, c);
            effect_fill_hspan(x + r - py, x + w - 1 - r + py, y + r - px, c);
            effect_fill_hspan(x + r - px, x + w - 1 - r + px, y + h - 1 - r + py, c);
            effect_fill_hspan(x + r - py, x + w - 1 - r + py, y + h - 1 - r + px, c);
            py++;
            if (err2 < 0) { err2 += 2 * py + 1; }
            else { px--; err2 += 2 * (py - px) + 1; }
        }
    } else {
        // Top and bottom straight edges
        for (uint8_t i = 0; i < bw; i++) {
            effect_fill_hspan(x + r, x + w - 1 - r, y + i, c);
            effect_fill_hspan(x + r, x + w - 1 - r, y + h - 1 - i, c);
        }
        // Left and right straight edges
        for (int16_t row = y + r; row <= y + h - 1 - r; row++) {
            for (uint8_t i = 0; i < bw; i++) {
                effect_write_pixel(x + i,             row, c);
                effect_write_pixel(x + w - 1 - i, row, c);
            }
        }
        // Corners via midpoint circle arc
        int16_t cx_tl = x + r,         cy_tl = y + r;
        int16_t cx_tr = x + w - 1 - r, cy_tr = y + r;
        int16_t cx_bl = x + r,         cy_bl = y + h - 1 - r;
        int16_t cx_br = x + w - 1 - r, cy_br = y + h - 1 - r;
        int16_t f = 1 - r, ddFx = 0, ddFy = -2 * r, px2 = 0, py2 = r;
        effect_write_pixel(cx_tl, cy_tl - r, c); effect_write_pixel(cx_tr, cy_tr - r, c);
        effect_write_pixel(cx_bl, cy_bl + r, c); effect_write_pixel(cx_br, cy_br + r, c);
        effect_write_pixel(cx_tl - r, cy_tl, c); effect_write_pixel(cx_tr + r, cy_tr, c);
        effect_write_pixel(cx_bl - r, cy_bl, c); effect_write_pixel(cx_br + r, cy_br, c);
        while (px2 < py2) {
            if (f >= 0) { py2--; ddFy += 2; f += ddFy; }
            px2++; ddFx += 2; f += ddFx + 1;
            for (uint8_t i = 0; i < bw; i++) {
                effect_write_pixel(cx_tr + px2 - i, cy_tr - py2, c);
                effect_write_pixel(cx_tr + py2 - i, cy_tr - px2, c);
                effect_write_pixel(cx_tl - px2 + i, cy_tl - py2, c);
                effect_write_pixel(cx_tl - py2 + i, cy_tl - px2, c);
                effect_write_pixel(cx_br + px2 - i, cy_br + py2, c);
                effect_write_pixel(cx_br + py2 - i, cy_br + px2, c);
                effect_write_pixel(cx_bl - px2 + i, cy_bl + py2, c);
                effect_write_pixel(cx_bl - py2 + i, cy_bl + px2, c);
            }
        }
    }
}

static void prim_rect_rounded(const uint8_t *p, uint16_t len) {
    if (len < 13) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t x = rd16s(p+1), y = rd16s(p+3), w = rd16s(p+5), h = rd16s(p+7);
    uint8_t  r = p[9], bw = p[10];
    uint16_t c = read_color(p + 11);
    draw_rounded_rect(x, y, w, h, r, bw, false, c);
}

static void prim_rect_rounded_filled(const uint8_t *p, uint16_t len) {
    if (len < 12) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t x = rd16s(p+1), y = rd16s(p+3), w = rd16s(p+5), h = rd16s(p+7);
    uint8_t  r = p[9];
    uint16_t c = read_color(p + 10);
    draw_rounded_rect(x, y, w, h, r, 1, true, c);
}

// =============================================================================
// 0x08 CIRCLE — outline via midpoint algorithm
// Payload: sub_op(1), cx(2), cy(2), r(2), border_width(1), color(2)
// =============================================================================
static void prim_circle(const uint8_t *p, uint16_t len) {
    if (len < 10) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  cx = rd16s(p+1), cy = rd16s(p+3), r = rd16s(p+5);
    uint8_t  bw = p[7];
    uint16_t c  = read_color(p + 8);
    if (bw == 0) bw = 1;

    int16_t x = 0, y = r, f = 1 - r, ddFx = 0, ddFy = -2 * r;
    circle_plot8(cx, cy, x, y, c, bw);
    effect_write_pixel(cx, cy + r, c); effect_write_pixel(cx, cy - r, c);
    effect_write_pixel(cx + r, cy, c); effect_write_pixel(cx - r, cy, c);
    while (x < y) {
        if (f >= 0) { y--; ddFy += 2; f += ddFy; }
        x++; ddFx += 2; f += ddFx + 1;
        circle_plot8(cx, cy, x, y, c, bw);
    }
}

// =============================================================================
// 0x09 CIRCLE_FILLED
// Payload: sub_op(1), cx(2), cy(2), r(2), color(2)
// =============================================================================
static void prim_circle_filled(const uint8_t *p, uint16_t len) {
    if (len < 9) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  cx = rd16s(p+1), cy = rd16s(p+3), r = rd16s(p+5);
    uint16_t c  = read_color(p + 7);

    int16_t x = 0, y = r, f = 1 - r, ddFx = 0, ddFy = -2 * r;
    effect_fill_hspan(cx - r, cx + r, cy, c);
    while (x < y) {
        if (f >= 0) { y--; ddFy += 2; f += ddFy; }
        x++; ddFx += 2; f += ddFx + 1;
        effect_fill_hspan(cx - x, cx + x, cy + y, c);
        effect_fill_hspan(cx - x, cx + x, cy - y, c);
        effect_fill_hspan(cx - y, cx + y, cy + x, c);
        effect_fill_hspan(cx - y, cx + y, cy - x, c);
    }
}

// =============================================================================
// 0x0A ELLIPSE — midpoint ellipse outline
// Payload: sub_op(1), cx(2), cy(2), rx(2), ry(2), border_width(1), color(2)
// =============================================================================
static void prim_ellipse(const uint8_t *p, uint16_t len) {
    if (len < 12) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  cx = rd16s(p+1), cy = rd16s(p+3);
    int16_t  rx = rd16s(p+5), ry = rd16s(p+7);
    uint8_t  bw = p[9];
    uint16_t c  = read_color(p + 10);
    if (bw == 0) bw = 1;
    if (rx <= 0 || ry <= 0) return;

    // Midpoint ellipse algorithm
    int32_t rx2 = (int32_t)rx * rx, ry2 = (int32_t)ry * ry;
    int32_t fx = 0, fy = rx2 * 2 * ry;
    int32_t x = 0, y = ry;
    int32_t p1 = (int32_t)ry2 - (int32_t)rx2 * ry + ((int32_t)rx2 + 2) / 4;

    while (fx < fy) {
        for (int16_t i = 0; i < bw; i++) {
            effect_write_pixel((int16_t)(cx + x - i), (int16_t)(cy + y), c);
            effect_write_pixel((int16_t)(cx - x + i), (int16_t)(cy + y), c);
            effect_write_pixel((int16_t)(cx + x - i), (int16_t)(cy - y), c);
            effect_write_pixel((int16_t)(cx - x + i), (int16_t)(cy - y), c);
        }
        if (p1 < 0) { x++; fx += 2 * ry2; p1 += fx + ry2; }
        else        { x++; y--; fx += 2*ry2; fy -= 2*rx2; p1 += fx - fy + ry2; }
    }
    int32_t p2 = (int32_t)ry2 * (2*x+1)*(2*x+1)/4 + (int32_t)rx2*(y-1)*(y-1) - (int32_t)rx2*ry2;
    while (y >= 0) {
        for (int16_t i = 0; i < bw; i++) {
            effect_write_pixel((int16_t)(cx + x), (int16_t)(cy + y - i), c);
            effect_write_pixel((int16_t)(cx - x), (int16_t)(cy + y - i), c);
            effect_write_pixel((int16_t)(cx + x), (int16_t)(cy - y + i), c);
            effect_write_pixel((int16_t)(cx - x), (int16_t)(cy - y + i), c);
        }
        if (p2 > 0) { y--; fy -= 2*rx2; p2 += rx2 - fy; }
        else        { y--; x++; fx += 2*ry2; fy -= 2*rx2; p2 += fx - fy + rx2; }
    }
}

// =============================================================================
// 0x0B ELLIPSE_FILLED
// Payload: sub_op(1), cx(2), cy(2), rx(2), ry(2), color(2)
// =============================================================================
static void prim_ellipse_filled(const uint8_t *p, uint16_t len) {
    if (len < 11) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  cx = rd16s(p+1), cy = rd16s(p+3);
    int16_t  rx = rd16s(p+5), ry = rd16s(p+7);
    uint16_t c  = read_color(p + 9);
    if (rx <= 0 || ry <= 0) return;

    int32_t rx2 = (int32_t)rx * rx, ry2 = (int32_t)ry * ry;
    int32_t fx = 0, fy = rx2 * 2 * ry;
    int32_t x = 0, y = ry;
    int32_t p1 = (int32_t)ry2 - (int32_t)rx2*ry + ((int32_t)rx2+2)/4;
    while (fx < fy) {
        effect_fill_hspan((int16_t)(cx - x), (int16_t)(cx + x), (int16_t)(cy + y), c);
        effect_fill_hspan((int16_t)(cx - x), (int16_t)(cx + x), (int16_t)(cy - y), c);
        if (p1 < 0) { x++; fx += 2*ry2; p1 += fx + ry2; }
        else        { x++; y--; fx += 2*ry2; fy -= 2*rx2; p1 += fx - fy + ry2; }
    }
    int32_t p2 = (int32_t)ry2*(2*x+1)*(2*x+1)/4 + (int32_t)rx2*(y-1)*(y-1) - (int32_t)rx2*ry2;
    while (y >= 0) {
        effect_fill_hspan((int16_t)(cx - x), (int16_t)(cx + x), (int16_t)(cy + y), c);
        effect_fill_hspan((int16_t)(cx - x), (int16_t)(cx + x), (int16_t)(cy - y), c);
        if (p2 > 0) { y--; fy -= 2*rx2; p2 += rx2 - fy; }
        else        { y--; x++; fx += 2*ry2; fy -= 2*rx2; p2 += fx - fy + rx2; }
    }
}

// =============================================================================
// 0x0C ARC — partial circle arc (integer degrees)
// Payload: sub_op(1), cx(2), cy(2), r(2), start_deg(2), end_deg(2), border_width(1), color(2)
// Uses precomputed sin/cos via a 360-entry integer lookup table.
// =============================================================================

// Sine table: 360 entries, scaled to 1000. sin_tbl[i] = round(sin(i*pi/180)*1000)
static const int16_t sin_tbl[360] = {
    0, 17, 35, 52, 70, 87, 105, 122, 139, 156, 174, 191, 208, 225, 242, 259,
    276, 292, 309, 326, 342, 358, 375, 391, 407, 423, 438, 454, 469, 485, 500, 515,
    530, 545, 559, 574, 588, 602, 616, 629, 643, 656, 669, 682, 695, 707, 719, 731,
    743, 755, 766, 777, 788, 799, 809, 819, 829, 839, 848, 857, 866, 875, 883, 891,
    899, 906, 914, 921, 927, 934, 940, 946, 951, 956, 961, 966, 970, 974, 978, 982,
    985, 988, 990, 993, 995, 997, 998, 999, 1000, 999, 999, 998, 997, 995, 993, 990,
    988, 985, 982, 978, 974, 970, 966, 961, 956, 951, 946, 940, 934, 927, 921, 914,
    906, 899, 891, 883, 875, 866, 857, 848, 839, 829, 819, 809, 799, 788, 777, 766,
    755, 743, 731, 719, 707, 695, 682, 669, 656, 643, 629, 616, 602, 588, 574, 559,
    545, 530, 515, 500, 485, 469, 454, 438, 423, 407, 391, 375, 358, 342, 326, 309,
    292, 276, 259, 242, 225, 208, 191, 174, 156, 139, 122, 105, 87, 70, 52, 35,
    17, 0,-17,-35,-52,-70,-87,-105,-122,-139,-156,-174,-191,-208,-225,-242,
    -259,-276,-292,-309,-326,-342,-358,-375,-391,-407,-423,-438,-454,-469,-485,-500,
    -515,-530,-545,-559,-574,-588,-602,-616,-629,-643,-656,-669,-682,-695,-707,-719,
    -731,-743,-755,-766,-777,-788,-799,-809,-819,-829,-839,-848,-857,-866,-875,-883,
    -891,-899,-906,-914,-921,-927,-934,-940,-946,-951,-956,-961,-966,-970,-974,-978,
    -982,-985,-988,-990,-993,-995,-997,-998,-999,-1000,-999,-999,-998,-997,-995,-993,
    -990,-988,-985,-982,-978,-974,-970,-966,-961,-956,-951,-946,-940,-934,-927,-921,
    -914,-906,-899,-891,-883,-875,-866,-857,-848,-839,-829,-819,-809,-799,-788,-777,
    -766,-755,-743,-731,-719,-707,-695,-682,-669,-656,-643,-629,-616,-602,-588,-574,
    -559,-545,-530,-515,-500,-485,-469,-454,-438,-423,-407,-391,-375,-358,-342,-326,
    -309,-292,-276,-259,-242,-225,-208,-191,-174,-156,-139,-122,-105,-87,-70,-52,
    -35,-17
};

static inline int16_t isin(int16_t deg) { return sin_tbl[((int32_t)deg % 360 + 360) % 360]; }
static inline int16_t icos(int16_t deg) { return sin_tbl[((int32_t)(deg + 90) % 360 + 360) % 360]; }

static void prim_arc(const uint8_t *p, uint16_t len) {
    if (len < 13) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  cx    = rd16s(p+1), cy = rd16s(p+3), r = rd16s(p+5);
    int16_t  start = rd16s(p+7), end = rd16s(p+9);
    uint8_t  bw    = p[11];
    uint16_t c     = read_color(p + 12);
    if (bw == 0) bw = 1;

    // Walk degrees around the arc
    int16_t span = end - start;
    if (span < 0) span += 360;
    if (span == 0) span = 360;
    for (int16_t deg = 0; deg <= span; deg++) {
        int16_t a = start + deg;
        for (uint8_t i = 0; i < bw; i++) {
            int16_t ri = r - i;
            int16_t px = cx + (int16_t)((int32_t)ri * icos(a) / 1000);
            int16_t py = cy - (int16_t)((int32_t)ri * isin(a) / 1000);
            effect_write_pixel(px, py, c);
        }
    }
}

// =============================================================================
// 0x0D TRIANGLE — three lines
// Payload: sub_op(1), x0(2), y0(2), x1(2), y1(2), x2(2), y2(2), color(2)
// =============================================================================
static void prim_triangle(const uint8_t *p, uint16_t len) {
    if (len < 15) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  x0 = rd16s(p+1), y0 = rd16s(p+3);
    int16_t  x1 = rd16s(p+5), y1 = rd16s(p+7);
    int16_t  x2 = rd16s(p+9), y2 = rd16s(p+11);
    uint16_t c  = read_color(p + 13);
    bresenham_line(x0, y0, x1, y1, c);
    bresenham_line(x1, y1, x2, y2, c);
    bresenham_line(x2, y2, x0, y0, c);
}

// =============================================================================
// 0x0E TRIANGLE_FILLED — scanline fill
// Payload: same as TRIANGLE
// =============================================================================
static void prim_triangle_filled(const uint8_t *p, uint16_t len) {
    if (len < 15) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t x0 = rd16s(p+1), y0 = rd16s(p+3);
    int16_t x1 = rd16s(p+5), y1 = rd16s(p+7);
    int16_t x2 = rd16s(p+9), y2 = rd16s(p+11);
    uint16_t c = read_color(p + 13);

    // Sort vertices by Y
    if (y0 > y1) { swap_i16(&x0,&x1); swap_i16(&y0,&y1); }
    if (y0 > y2) { swap_i16(&x0,&x2); swap_i16(&y0,&y2); }
    if (y1 > y2) { swap_i16(&x1,&x2); swap_i16(&y1,&y2); }

    // Fill flat-top / flat-bottom halves
    // Using fixed-point slope stepping
    if (y0 == y2) { // Degenerate: single horizontal line
        int16_t xa = x0, xb = x2;
        if (xa > xb) swap_i16(&xa, &xb);
        effect_fill_hspan(xa, xb, y0, c);
        return;
    }

    int32_t dx01 = (int32_t)(x1 - x0) * 0x10000 / (y1 - y0 != 0 ? y1 - y0 : 1);
    int32_t dx02 = (int32_t)(x2 - x0) * 0x10000 / (y2 - y0);
    int32_t dx12 = (y2 - y1 != 0) ? (int32_t)(x2 - x1) * 0x10000 / (y2 - y1) : 0;

    int32_t xa = (int32_t)x0 * 0x10000;
    int32_t xb = xa;

    // Upper half: y0 → y1
    for (int16_t y = y0; y <= y1; y++) {
        int16_t lx = (int16_t)(xa >> 16), rx = (int16_t)(xb >> 16);
        if (lx > rx) { int16_t t = lx; lx = rx; rx = t; }
        effect_fill_hspan(lx, rx, y, c);
        xa += dx01; xb += dx02;
    }
    // Lower half: y1 → y2
    xa = (int32_t)x1 * 0x10000;
    for (int16_t y = y1; y <= y2; y++) {
        int16_t lx = (int16_t)(xa >> 16), rx = (int16_t)(xb >> 16);
        if (lx > rx) { int16_t t = lx; lx = rx; rx = t; }
        effect_fill_hspan(lx, rx, y, c);
        xa += dx12; xb += dx02;
    }
}

// =============================================================================
// 0x10 POLYGON_FILLED — scanline fill with edge-intersection algorithm
// Payload: sub_op(1), n_points(1), color(2), then n*(x(2),y(2)) pairs
// Supports concave and convex polygons; n_points ≤ 64.
// =============================================================================
#define MAX_POLY_POINTS 64
#define MAX_POLY_INTERSECTIONS 64

static void prim_polygon_filled(const uint8_t *p, uint16_t len) {
    if (len < 4) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint8_t  n = p[1];
    uint16_t c = read_color(p + 2);
    if (n < 3 || n > MAX_POLY_POINTS) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    if ((uint16_t)(4 + n * 4) > len) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    int16_t vx[MAX_POLY_POINTS], vy[MAX_POLY_POINTS];
    const uint8_t *coords = p + 4;
    int16_t ymin = 32767, ymax = -32768;
    for (uint8_t i = 0; i < n; i++) {
        vx[i] = rd16s(coords + i * 4);
        vy[i] = rd16s(coords + i * 4 + 2);
        if (vy[i] < ymin) ymin = vy[i];
        if (vy[i] > ymax) ymax = vy[i];
    }

    // Clamp to framebuffer
    if (ymin < 0) ymin = 0;
    if (ymax >= (int16_t)g_fb_height) ymax = (int16_t)g_fb_height - 1;

    // Scanline fill: for each row, find X intersections with all edges
    int16_t xs[MAX_POLY_INTERSECTIONS];
    for (int16_t y = ymin; y <= ymax; y++) {
        uint8_t cnt = 0;
        for (uint8_t i = 0; i < n; i++) {
            uint8_t j = (i + 1) % n;
            int16_t yi = vy[i], yj = vy[j];
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                // Compute X intersection (integer)
                int16_t x_int = vx[i] + (int16_t)((int32_t)(y - yi) * (vx[j] - vx[i]) / (yj - yi));
                if (cnt < MAX_POLY_INTERSECTIONS) xs[cnt++] = x_int;
            }
        }
        // Sort intersections
        for (uint8_t a = 0; a < cnt - 1; a++) {
            for (uint8_t b = a + 1; b < cnt; b++) {
                if (xs[a] > xs[b]) { int16_t t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
            }
        }
        // Fill spans
        for (uint8_t a = 0; a + 1 < cnt; a += 2) {
            effect_fill_hspan(xs[a], xs[a+1], y, c);
        }
    }
}

// =============================================================================
// 0x14 FLOOD_FILL — stack-based scanline flood fill
// Payload: sub_op(1), x(2), y(2), fill_color(2)
// Replaces all pixels matching the color at (x,y) with fill_color.
// =============================================================================
#define FLOOD_STACK_MAX 2048

typedef struct { int16_t x, y; } flood_pt_t;
static flood_pt_t s_flood_stack[FLOOD_STACK_MAX]; // static scratch — no heap needed

static void prim_flood_fill(const uint8_t *p, uint16_t len) {
    if (len < 7) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  sx = rd16s(p+1), sy = rd16s(p+3);
    uint16_t fill_c = read_color(p + 5);

    if ((uint16_t)sx >= g_fb_width || (uint16_t)sy >= g_fb_height) return;

    // Read seed color
    uint16_t seed_c;
    if (g_fb_bpp == 8) {
        seed_c = g_fb_back[(uint32_t)sy * g_fb_stride + sx];
    } else {
        seed_c = ((uint16_t*)g_fb_back)[(uint32_t)sy * g_fb_width + sx];
    }
    if (seed_c == fill_c) return; // already filled

    int32_t head = 0, tail = 0;
    s_flood_stack[head++] = (flood_pt_t){sx, sy};

    while (head != tail) {
        flood_pt_t pt = s_flood_stack[tail++];
        if (tail >= FLOOD_STACK_MAX) tail = 0;
        int16_t x = pt.x, y = pt.y;
        if ((uint16_t)x >= g_fb_width || (uint16_t)y >= g_fb_height) continue;

        // Read current pixel
        uint16_t cur;
        if (g_fb_bpp == 8)  cur = g_fb_back[(uint32_t)y * g_fb_stride + x];
        else                 cur = ((uint16_t*)g_fb_back)[(uint32_t)y * g_fb_width + x];
        if (cur != seed_c) continue;

        // Fill this pixel
        effect_write_pixel(x, y, fill_c);

        // Push neighbors
        #define FLOOD_PUSH(nx, ny) \
            do { int32_t nh = (head + 1) % FLOOD_STACK_MAX; \
                 if (nh != tail) { s_flood_stack[head] = (flood_pt_t){(nx),(ny)}; head = nh; } \
            } while(0)
        FLOOD_PUSH(x - 1, y);
        FLOOD_PUSH(x + 1, y);
        FLOOD_PUSH(x, y - 1);
        FLOOD_PUSH(x, y + 1);
        #undef FLOOD_PUSH
    }
}

// =============================================================================
// FILL_SCREEN (opcode 0x31) — fast framebuffer clear
// =============================================================================
void handle_fill_screen(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    uint16_t color = (len >= 2) ? ((uint16_t)payload[0] | ((uint16_t)payload[1] << 8)) : 0;
    if (g_fb_bpp == 8) {
        // Fast memset for 8bpp
        memset(g_fb_back, color & 0xFF, g_fb_size);
    } else {
        // 16bpp: word fill
        uint32_t npx = g_fb_size / 2;
        uint16_t *fb16 = (uint16_t *)g_fb_back;
        for (uint32_t i = 0; i < npx; i++) fb16[i] = color;
    }
    coprocessor_set_error(ERR_OK);
}

// =============================================================================
// Master dispatch for DRAW_PRIMITIVE (opcode 0x30)
// =============================================================================
void handle_draw_primitive(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    uint8_t sub_op = payload[0];
    switch (sub_op) {
        case PRIM_SET_PIXEL:            prim_set_pixel(payload, len);            break;
        case PRIM_LINE:                 prim_line(payload, len);                 break;
        case PRIM_LINE_DASHED:          prim_line_dashed(payload, len);          break;
        case PRIM_RECT:                 prim_rect(payload, len);                 break;
        case PRIM_RECT_FILLED:          prim_rect_filled(payload, len);          break;
        case PRIM_RECT_ROUNDED:         prim_rect_rounded(payload, len);         break;
        case PRIM_RECT_ROUNDED_FILLED:  prim_rect_rounded_filled(payload, len);  break;
        case PRIM_CIRCLE:               prim_circle(payload, len);               break;
        case PRIM_CIRCLE_FILLED:        prim_circle_filled(payload, len);        break;
        case PRIM_ELLIPSE:              prim_ellipse(payload, len);              break;
        case PRIM_ELLIPSE_FILLED:       prim_ellipse_filled(payload, len);       break;
        case PRIM_ARC:                  prim_arc(payload, len);                  break;
        case PRIM_TRIANGLE:             prim_triangle(payload, len);             break;
        case PRIM_TRIANGLE_FILLED:      prim_triangle_filled(payload, len);      break;
        case PRIM_POLYGON_FILLED:       prim_polygon_filled(payload, len);       break;
        case PRIM_FLOOD_FILL:           prim_flood_fill(payload, len);           break;
        // FPU-only sub-opcodes — deferred to Phase 2
        case PRIM_TRIANGLE_GRADIENT:
        case PRIM_BEZIER_QUAD:
        case PRIM_BEZIER_CUBIC:
        case PRIM_GRADIENT_RECT:
            coprocessor_set_error(ERR_FEATURE_UNAVAILABLE);
            break;
        default:
            coprocessor_set_error(ERR_UNKNOWN_OPCODE);
            break;
    }
}
