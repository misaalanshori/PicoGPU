#include "nine_patch.h"
#include "effects.h"
#include "framebuffer.h"
#include "../assets/vram.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// DRAW_9PATCH (0x52)
// Payload (18 bytes):
//   bytes 0-3:  vram_offset (uint32 LE)
//   bytes 4-5:  sprite_w    (uint16 LE) — full source width
//   bytes 6-7:  sprite_h    (uint16 LE) — full source height
//   byte  8:    corner_w    (uint8) — corner margin width  (left = right = corner_w)
//   byte  9:    corner_h    (uint8) — corner margin height (top = bottom = corner_h)
//   bytes 10-11: dst_x      (int16 LE)
//   bytes 12-13: dst_y      (int16 LE)
//   bytes 14-15: dst_w      (uint16 LE)
//   bytes 16-17: dst_h      (uint16 LE)
//
// 9-region layout of source sprite:
//   [TL corner][T edge][TR corner]
//   [L edge   ][Centre][R edge   ]
//   [BL corner][B edge][BR corner]
//
// Corners are blitted 1:1; edges and centre are tiled (pixel-repeat).
// ---------------------------------------------------------------------------

void handle_draw_9patch(const uint8_t *payload, uint16_t len) {
    if (!g_fb_back || !g_vram) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 18) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    // Parse payload
    uint32_t voff   = (uint32_t)payload[0]|((uint32_t)payload[1]<<8)|((uint32_t)payload[2]<<16)|((uint32_t)payload[3]<<24);
    uint16_t sp_w   = (uint16_t)payload[4]|((uint16_t)payload[5]<<8);
    uint16_t sp_h   = (uint16_t)payload[6]|((uint16_t)payload[7]<<8);
    uint8_t  cw     = payload[8];   // corner width
    uint8_t  ch     = payload[9];   // corner height
    int16_t  dst_x  = (int16_t)((uint16_t)payload[10]|((uint16_t)payload[11]<<8));
    int16_t  dst_y  = (int16_t)((uint16_t)payload[12]|((uint16_t)payload[13]<<8));
    uint16_t dst_w  = (uint16_t)payload[14]|((uint16_t)payload[15]<<8);
    uint16_t dst_h  = (uint16_t)payload[16]|((uint16_t)payload[17]<<8);

    if (sp_w < 2u*cw || sp_h < 2u*ch || dst_w < 2u*cw || dst_h < 2u*ch) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    uint32_t bpp = (g_fb_bpp == 16) ? 2u : 1u;
    uint32_t sprite_bytes = (uint32_t)sp_w * sp_h * bpp;
    if (voff + sprite_bytes > g_vram_size) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    const uint8_t *spr = g_vram + voff;

    // Source region geometry
    uint16_t mid_sw = sp_w - 2u*cw;   // source centre width
    uint16_t mid_sh = sp_h - 2u*ch;   // source centre height
    uint16_t mid_dw = dst_w - 2u*cw;  // dest centre width
    uint16_t mid_dh = dst_h - 2u*ch;  // dest centre height

    // BLIT_REGION: copy a source rect to a dest rect, with optional tiling.
    // src_col0, src_row0: top-left in source image (pixel coords).
    // src_rw, src_rh: source region size.
    // dcol0, drow0: top-left in destination screen coords.
    // drw, drh: destination region size.
    // do_tile: if non-zero, tile the source to fill the destination.
    #define BLIT_REGION(src_col0, src_row0, src_rw, src_rh, dcol0, drow0, drw, drh, do_tile) \
        do { \
            for (uint16_t dy_ = 0; dy_ < (drh); dy_++) { \
                for (uint16_t dx_ = 0; dx_ < (drw); dx_++) { \
                    uint16_t sx_ = (do_tile) ? (dx_ % (src_rw)) : dx_; \
                    uint16_t sy_ = (do_tile) ? (dy_ % (src_rh)) : dy_; \
                    uint32_t si_ = ((uint32_t)(src_row0 + sy_) * sp_w + (src_col0 + sx_)) * bpp; \
                    uint16_t pix_; \
                    if (bpp == 2) { pix_ = (uint16_t)spr[si_]|((uint16_t)spr[si_+1]<<8); } \
                    else          { pix_ = spr[si_]; } \
                    effect_write_pixel((int16_t)((dcol0)+dx_), (int16_t)((drow0)+dy_), pix_); \
                } \
            } \
        } while(0)

    // --- 9 regions ---

    // Top-left corner (no tile)
    BLIT_REGION(0,          0,       cw,     ch,     dst_x,              dst_y,              cw,     ch,     false);
    // Top edge (tile horizontally)
    BLIT_REGION(cw,         0,       mid_sw, ch,     dst_x+cw,           dst_y,              mid_dw, ch,     true);
    // Top-right corner (no tile)
    BLIT_REGION(cw+mid_sw,  0,       cw,     ch,     dst_x+cw+mid_dw,    dst_y,              cw,     ch,     false);

    // Left edge (tile vertically)
    BLIT_REGION(0,          ch,      cw,     mid_sh, dst_x,              dst_y+ch,           cw,     mid_dh, true);
    // Centre (tile both axes)
    BLIT_REGION(cw,         ch,      mid_sw, mid_sh, dst_x+cw,           dst_y+ch,           mid_dw, mid_dh, true);
    // Right edge (tile vertically)
    BLIT_REGION(cw+mid_sw,  ch,      cw,     mid_sh, dst_x+cw+mid_dw,    dst_y+ch,           cw,     mid_dh, true);

    // Bottom-left corner (no tile)
    BLIT_REGION(0,          ch+mid_sh, cw,   ch,     dst_x,              dst_y+ch+mid_dh,    cw,     ch,     false);
    // Bottom edge (tile horizontally)
    BLIT_REGION(cw,         ch+mid_sh, mid_sw, ch,   dst_x+cw,           dst_y+ch+mid_dh,    mid_dw, ch,     true);
    // Bottom-right corner (no tile)
    BLIT_REGION(cw+mid_sw,  ch+mid_sh, cw,   ch,     dst_x+cw+mid_dw,    dst_y+ch+mid_dh,    cw,     ch,     false);

    #undef BLIT_REGION

    coprocessor_set_error(ERR_OK);
}
