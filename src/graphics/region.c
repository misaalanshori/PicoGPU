#include "region.h"
#include "effects.h"
#include "framebuffer.h"
#include "../assets/vram.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline int16_t  r16s(const uint8_t *p) { return (int16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8)); }
static inline uint16_t r16u(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8); }
static inline uint32_t r32u(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }

// ---------------------------------------------------------------------------
// COPY_REGION (0x32)
// Payload: src_x(2), src_y(2), w(2), h(2), dst_x(2), dst_y(2), flags(1) = 13 bytes
// ---------------------------------------------------------------------------

void handle_copy_region(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 13)   { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t src_x = r16s(payload + 0);
    int16_t src_y = r16s(payload + 2);
    int16_t w     = r16s(payload + 4);
    int16_t h     = r16s(payload + 6);
    int16_t dst_x = r16s(payload + 8);
    int16_t dst_y = r16s(payload + 10);
    // uint8_t flags = payload[12]; // reserved — bit0=wrap, not yet implemented
    if (w <= 0 || h <= 0) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint32_t bpp = (g_fb_bpp == 16) ? 2u : 1u;
    for (int16_t row = 0; row < h; row++) {
        int16_t sy = src_y + row;
        int16_t dy = dst_y + row;
        if (sy < 0 || sy >= (int16_t)g_fb_height) continue;
        if (dy < 0 || dy >= (int16_t)g_fb_height) continue;
        // Clamp horizontal
        int16_t col_start = 0;
        int16_t col_end   = w;
        if (src_x + col_start < 0) col_start = -src_x;
        if (dst_x + col_start < 0) col_start = (col_start > -dst_x) ? col_start : -dst_x;
        if (src_x + col_end > (int16_t)g_fb_width) col_end = (int16_t)g_fb_width - src_x;
        if (dst_x + col_end > (int16_t)g_fb_width) col_end = (int16_t)g_fb_width - dst_x;
        if (col_end <= col_start) continue;
        uint8_t *src_row = g_fb_back + (uint32_t)sy * g_fb_stride + (uint32_t)(src_x + col_start) * bpp;
        uint8_t *dst_row = g_fb_back + (uint32_t)dy * g_fb_stride + (uint32_t)(dst_x + col_start) * bpp;
        memmove(dst_row, src_row, (uint32_t)(col_end - col_start) * bpp);
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// REPLACE_COLOR (0x33)
// Payload: old_color(2B LE), new_color(2B LE) = 4 bytes
// ---------------------------------------------------------------------------

void handle_replace_color(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 4)    { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint16_t old_c = r16u(payload + 0);
    uint16_t new_c = r16u(payload + 2);
    uint32_t total_pixels = (uint32_t)g_fb_width * g_fb_height;
    if (g_fb_bpp == 16) {
        uint16_t *fb = (uint16_t *)g_fb_back;
        for (uint32_t i = 0; i < total_pixels; i++) {
            if (fb[i] == old_c) fb[i] = new_c;
        }
    } else {
        uint8_t oc = (uint8_t)(old_c & 0xFF);
        uint8_t nc = (uint8_t)(new_c & 0xFF);
        for (uint32_t i = 0; i < total_pixels; i++) {
            if (g_fb_back[i] == oc) g_fb_back[i] = nc;
        }
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// SCROLL_SCREEN (0x35)
// Payload: dx(2B LE signed), dy(2B LE signed), wrap(1B), fill_color(2B LE) = 7 bytes
// ---------------------------------------------------------------------------

void handle_scroll_screen(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 7)    { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t  dx         = r16s(payload + 0);
    int16_t  dy         = r16s(payload + 2);
    // uint8_t wrap     = payload[4]; // not yet implemented
    uint16_t fill_color = r16u(payload + 5);

    int16_t W = (int16_t)g_fb_width;
    int16_t H = (int16_t)g_fb_height;

    // Copy existing pixels to scrolled position via COPY_REGION
    int16_t copy_sx = (dx > 0) ? 0 : -dx;
    int16_t copy_sy = (dy > 0) ? 0 : -dy;
    int16_t copy_w  = W - (int16_t)(dx < 0 ? -dx : dx);
    int16_t copy_h  = H - (int16_t)(dy < 0 ? -dy : dy);
    int16_t copy_dx = (dx > 0) ? dx : 0;
    int16_t copy_dy = (dy > 0) ? dy : 0;
    if (copy_w > 0 && copy_h > 0) {
        uint8_t cr_payload[13];
        cr_payload[0]  = (uint8_t)(copy_sx);       cr_payload[1]  = (uint8_t)(copy_sx >> 8);
        cr_payload[2]  = (uint8_t)(copy_sy);       cr_payload[3]  = (uint8_t)(copy_sy >> 8);
        cr_payload[4]  = (uint8_t)(copy_w);        cr_payload[5]  = (uint8_t)(copy_w  >> 8);
        cr_payload[6]  = (uint8_t)(copy_h);        cr_payload[7]  = (uint8_t)(copy_h  >> 8);
        cr_payload[8]  = (uint8_t)(copy_dx);       cr_payload[9]  = (uint8_t)(copy_dx >> 8);
        cr_payload[10] = (uint8_t)(copy_dy);       cr_payload[11] = (uint8_t)(copy_dy >> 8);
        cr_payload[12] = 0; // flags
        handle_copy_region(cr_payload, 13);
    }

    // Fill exposed horizontal strip (top or bottom)
    if (dy > 0) {
        for (int16_t y = 0; y < dy && y < H; y++)
            effect_fill_hspan(0, W - 1, y, fill_color);
    } else if (dy < 0) {
        for (int16_t y = H + dy; y < H; y++)
            effect_fill_hspan(0, W - 1, y, fill_color);
    }
    // Fill exposed vertical strip (left or right)
    if (dx > 0) {
        for (int16_t y = 0; y < H; y++)
            effect_fill_hspan(0, dx - 1, y, fill_color);
    } else if (dx < 0) {
        for (int16_t y = 0; y < H; y++)
            effect_fill_hspan(W + dx, W - 1, y, fill_color);
    }
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// DRAW_TILEMAP (0x34)
// Payload (18 bytes):
//   bytes 0-3:  tile_vram_offset (uint32 LE)
//   bytes 4-7:  map_vram_offset  (uint32 LE)
//   byte  8:    tile_w  (uint8)
//   byte  9:    tile_h  (uint8)
//   bytes 10-11: map_cols (uint16 LE)
//   bytes 12-13: map_rows (uint16 LE)
//   bytes 14-15: scroll_x (int16 LE)
//   bytes 16-17: scroll_y (int16 LE)
// ---------------------------------------------------------------------------

void handle_draw_tilemap(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back || !g_vram) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 18) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    uint32_t tile_off  = r32u(payload + 0);
    uint32_t map_off   = r32u(payload + 4);
    uint8_t  tile_w    = payload[8];
    uint8_t  tile_h    = payload[9];
    uint16_t map_cols  = r16u(payload + 10);
    uint16_t map_rows  = r16u(payload + 12);
    int16_t  scroll_x  = r16s(payload + 14);
    int16_t  scroll_y  = r16s(payload + 16);

    if (tile_w == 0 || tile_h == 0 || map_cols == 0 || map_rows == 0) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    uint32_t bpp = (g_fb_bpp == 16) ? 2u : 1u;
    uint32_t tile_bytes = (uint32_t)tile_w * tile_h * bpp;

    // Validate map grid bounds
    if (map_off + (uint32_t)map_cols * map_rows > g_vram_size) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    // Handle negative scroll via modulo normalization
    while (scroll_x < 0) scroll_x += (int16_t)((uint16_t)map_cols * tile_w);
    while (scroll_y < 0) scroll_y += (int16_t)((uint16_t)map_rows * tile_h);

    int16_t sub_x     = scroll_x % tile_w;  // pixel offset within first visible tile column
    int16_t sub_y     = scroll_y % tile_h;  // pixel offset within first visible tile row
    int16_t start_col = (scroll_x / tile_w) % map_cols;
    int16_t start_row = (scroll_y / tile_h) % map_rows;

    // Iterate visible tile grid on screen
    int16_t screen_y = -sub_y;
    for (int16_t tr = 0; screen_y < (int16_t)g_fb_height; tr++) {
        int16_t map_row = (start_row + tr) % (int16_t)map_rows;
        int16_t screen_x = -sub_x;
        for (int16_t tc = 0; screen_x < (int16_t)g_fb_width; tc++) {
            int16_t map_col = (start_col + tc) % (int16_t)map_cols;
            uint8_t tile_idx = g_vram[map_off + (uint32_t)map_row * map_cols + (uint32_t)map_col];
            // Bounds-check tile data
            uint32_t t_data_off = tile_off + (uint32_t)tile_idx * tile_bytes;
            if (t_data_off + tile_bytes > g_vram_size) {
                screen_x += tile_w; continue; // skip out-of-bounds tile
            }
            const uint8_t *tile_data = g_vram + t_data_off;
            // Blit tile pixels
            for (uint8_t ty = 0; ty < tile_h; ty++) {
                int16_t dst_y = screen_y + ty;
                if (dst_y < 0 || dst_y >= (int16_t)g_fb_height) continue;
                for (uint8_t tx = 0; tx < tile_w; tx++) {
                    int16_t dst_x = screen_x + tx;
                    if (dst_x < 0 || dst_x >= (int16_t)g_fb_width) continue;
                    uint32_t src_idx = (uint32_t)ty * tile_w + tx;
                    uint16_t pix;
                    if (bpp == 2) {
                        pix = (uint16_t)tile_data[src_idx*2] | ((uint16_t)tile_data[src_idx*2+1] << 8);
                    } else {
                        pix = tile_data[src_idx];
                    }
                    effect_write_pixel(dst_x, dst_y, pix);
                }
            }
            screen_x += tile_w;
        }
        screen_y += tile_h;
    }
    coprocessor_set_error(ERR_OK);
}
