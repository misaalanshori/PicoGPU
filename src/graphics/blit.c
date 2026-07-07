// blit.c — Sprite blit handlers (spec §7.1, §7.2)
// BLIT_SPRITE: streaming blit from ring buffer (buffered in Phase 0/1)
// DRAW_VRAM_SPRITE: blit from VRAM cache with 0/90/180/270° rotation,
//                   hflip, vflip, and palette_override.

#include "blit.h"
#include "effects.h"
#include "framebuffer.h"
#include "../assets/vram.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"

#include <string.h>

// transform_flags bit layout (spec §7.2):
//   bits [1:0] = rotation: 00=0°, 01=90° CW, 10=180°, 11=270° CW
//   bit 2      = hflip
//   bit 3      = vflip
//   bit 4      = apply_palette_override (replace all non-chroma-key pixels)
#define TRANSFORM_ROT_MASK   0x03
#define TRANSFORM_HFLIP      0x04
#define TRANSFORM_VFLIP      0x08
#define TRANSFORM_PALETTE    0x10
#define ROT_0    0
#define ROT_90   1
#define ROT_180  2
#define ROT_270  3

static inline int16_t rd16s_bl(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static inline uint16_t rd16u_bl(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t rd32u_bl(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

// =============================================================================
// BLIT_SPRITE (0x50) — stream blit from packet payload (Phase 0/1: fully buffered)
// Payload: x(2LE), y(2LE), w(1), h(1), rle_flag(1), [pixel_data...]
// =============================================================================
void handle_blit_sprite(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 7) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    int16_t  dst_x   = rd16s_bl(payload + 0);
    int16_t  dst_y   = rd16s_bl(payload + 2);
    uint8_t  w       = payload[4];
    uint8_t  h       = payload[5];
    uint8_t  rle_flg = payload[6];

    if (rle_flg) {
        // RLE not yet implemented (Phase 3)
        coprocessor_set_error(ERR_FEATURE_UNAVAILABLE);
        return;
    }

    const uint8_t *src = payload + 7;
    uint32_t expected = (uint32_t)w * h * (g_fb_bpp == 16 ? 2 : 1);
    if (len < 7 + expected) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    // Blit row by row, clipping to framebuffer bounds
    for (uint8_t row = 0; row < h; row++) {
        int16_t dst_row = dst_y + row;
        if (dst_row < 0 || dst_row >= (int16_t)g_fb_height) {
            src += w * (g_fb_bpp == 16 ? 2 : 1);
            continue;
        }
        for (uint8_t col = 0; col < w; col++) {
            int16_t dst_col = dst_x + col;
            uint16_t pix;
            if (g_fb_bpp == 16) {
                pix = rd16u_bl(src); src += 2;
            } else {
                pix = *src++;
            }
            effect_write_pixel(dst_col, dst_row, pix);
        }
    }
    coprocessor_set_error(ERR_OK);
}

// =============================================================================
// DRAW_VRAM_SPRITE (0x51) — blit from VRAM with transforms
// Payload: x(2LE), y(2LE), w(2LE), h(2LE), vram_offset(4LE), transform_flags(1), palette_color(2LE)
// =============================================================================
void handle_draw_vram_sprite(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 14) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    if (!g_vram)  { coprocessor_set_error(ERR_VRAM_FULL); return; }  // VRAM not allocated

    int16_t  dst_x   = rd16s_bl(payload + 0);
    int16_t  dst_y   = rd16s_bl(payload + 2);
    uint16_t src_w   = rd16u_bl(payload + 4);
    uint16_t src_h   = rd16u_bl(payload + 6);
    uint32_t offset  = rd32u_bl(payload + 8);
    uint8_t  flags   = payload[12];
    uint16_t pal_col = rd16u_bl(payload + 13);

    uint8_t  rot     = flags & TRANSFORM_ROT_MASK;
    bool     hflip   = (flags & TRANSFORM_HFLIP)   != 0;
    bool     vflip   = (flags & TRANSFORM_VFLIP)   != 0;
    bool     use_pal = (flags & TRANSFORM_PALETTE)  != 0;

    uint32_t bytes_per_px = (g_fb_bpp == 16) ? 2 : 1;
    uint32_t sprite_size  = (uint32_t)src_w * src_h * bytes_per_px;

    if (offset + sprite_size > g_vram_size) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    const uint8_t *sprite = g_vram + offset;

    // Iterate over destination pixels, computing source pixel index for each transform
    for (uint16_t dy = 0; dy < src_h; dy++) {
        for (uint16_t dx = 0; dx < src_w; dx++) {
            // Source pixel coordinates before rotation
            uint16_t sx_raw = dx;
            uint16_t sy_raw = dy;

            // Apply hflip / vflip to source coordinates
            uint16_t sx = hflip ? (src_w - 1 - sx_raw) : sx_raw;
            uint16_t sy = vflip ? (src_h - 1 - sy_raw) : sy_raw;

            // Apply rotation (pure index arithmetic — no trig)
            uint16_t final_sx, final_sy;
            switch (rot) {
                case ROT_0:   final_sx = sx;           final_sy = sy;           break;
                case ROT_90:  final_sx = sy;            final_sy = src_w-1-sx;  break;
                case ROT_180: final_sx = src_w-1-sx;   final_sy = src_h-1-sy;  break;
                case ROT_270: final_sx = src_h-1-sy;   final_sy = sx;           break;
                default:      final_sx = sx;           final_sy = sy;           break;
            }

            // Read source pixel from VRAM
            uint32_t src_idx = (uint32_t)final_sy * src_w + final_sx;
            uint16_t pix;
            if (bytes_per_px == 2) {
                pix = (uint16_t)sprite[src_idx*2] | ((uint16_t)sprite[src_idx*2+1] << 8);
            } else {
                pix = sprite[src_idx];
            }

            // Apply palette override if requested
            if (use_pal) {
                // Skip if chroma key matches (chroma key acts as transparency in palette mode)
                bool is_key = g_state.chroma_key_enabled &&
                    ((g_fb_bpp == 16 && pix == g_state.chroma_key_color) ||
                     (g_fb_bpp == 8  && (pix & 0xFF) == (g_state.chroma_key_color & 0xFF)));
                if (!is_key) pix = pal_col;
            }

            // Compute destination position
            int16_t out_x, out_y;
            if (rot == ROT_90 || rot == ROT_270) {
                // After 90/270 rotation, sprite width and height swap
                out_x = dst_x + (int16_t)dy;
                out_y = dst_y + (int16_t)dx;
            } else {
                out_x = dst_x + (int16_t)dx;
                out_y = dst_y + (int16_t)dy;
            }

            effect_write_pixel(out_x, out_y, pix);
        }
    }
    coprocessor_set_error(ERR_OK);
}
