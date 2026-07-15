#include "vram.h"
#include "error_codes.h"
#include "../state/coprocessor_state.h"
#include "../graphics/framebuffer.h"

#include <string.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// VRAM globals
// ---------------------------------------------------------------------------
uint8_t  *g_vram      = NULL;
uint32_t  g_vram_size = 0u;
uint32_t  g_vram_used = 0u;

// ---------------------------------------------------------------------------
// vram_init  — called by profiles.c after arena allocation
// ---------------------------------------------------------------------------
void vram_init(uint8_t *ptr, uint32_t size)
{
    g_vram      = ptr;
    g_vram_size = size;
    g_vram_used = 0u;
}

// ---------------------------------------------------------------------------
// handle_upload_vram  (opcode 0x80)
//
// Payload layout (minimum 9 bytes before pixel data):
//   [0..3]  byte_offset  (uint32_t LE)
//   [4..7]  byte_count   (uint32_t LE)
//   [8]     rle_flag     (uint8_t; 0 = raw, 1 = RLE — deferred to Phase 3)
//   [9..]   pixel_data   (byte_count bytes)
// ---------------------------------------------------------------------------
#define UPLOAD_HEADER_SIZE  9u

void handle_upload_vram(const uint8_t *payload, uint32_t len)
{
    // Need at least the 9-byte header
    if (len < UPLOAD_HEADER_SIZE) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Decode header (little-endian)
    uint32_t offset =
        (uint32_t)payload[0]         |
        ((uint32_t)payload[1] <<  8u) |
        ((uint32_t)payload[2] << 16u) |
        ((uint32_t)payload[3] << 24u);

    uint32_t count =
        (uint32_t)payload[4]         |
        ((uint32_t)payload[5] <<  8u) |
        ((uint32_t)payload[6] << 16u) |
        ((uint32_t)payload[7] << 24u);

    uint8_t rle_flag = payload[8];

    // Validate VRAM pointer initialised
    if (g_vram == NULL) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Bounds check: decoded region must fit in VRAM
    if ((offset + count) > g_vram_size) {
        coprocessor_set_error(ERR_VRAM_FULL);
        return;
    }

    const uint8_t *src_data = payload + UPLOAD_HEADER_SIZE;
    uint32_t data_available = len - UPLOAD_HEADER_SIZE;

    if (rle_flag == 0u) {
        // Raw copy
        if (data_available < count) {
            coprocessor_set_error(ERR_INVALID_PARAM);
            return;
        }
        memcpy(g_vram + offset, src_data, count);
    } else {
        // H4 fix: RLE decode into VRAM (same format as blit.c §5.7)
        // Format: run-mode   [count:1B][byte:1B] → count copies of byte
        //         literal     [0x00:1B][n:1B][n raw bytes]
        uint32_t src_pos  = 0;
        uint32_t dst_pos  = 0;
        while (dst_pos < count && src_pos < data_available) {
            uint8_t token = src_data[src_pos++];
            if (token == 0x00u) {
                // Literal run
                if (src_pos >= data_available) break;
                uint8_t n = src_data[src_pos++];
                for (uint8_t i = 0; i < n && dst_pos < count && src_pos < data_available; i++) {
                    g_vram[offset + dst_pos++] = src_data[src_pos++];
                }
            } else {
                // Repeat run
                if (src_pos >= data_available) break;
                uint8_t color = src_data[src_pos++];
                for (uint8_t i = 0; i < token && dst_pos < count; i++) {
                    g_vram[offset + dst_pos++] = color;
                }
            }
        }
        if (dst_pos < count) {
            // RLE stream underrun — partial write
            coprocessor_set_error(ERR_INVALID_PARAM);
            return;
        }
    }

    // Update high-water mark
    uint32_t new_hwm = offset + count;
    if (new_hwm > g_vram_used) {
        g_vram_used = new_hwm;
    }

    g_state.last_error = ERR_OK;
}

// ---------------------------------------------------------------------------
// Query helpers
// ---------------------------------------------------------------------------
uint32_t vram_get_free(void)
{
    return g_vram_size - g_vram_used;
}

uint32_t vram_get_used(void)
{
    return g_vram_used;
}

// ---------------------------------------------------------------------------
// handle_capture_region  (opcode 0x53)
//
// Copies a rectangle from the back framebuffer into VRAM.
// Payload (12 bytes):
//   [0..1]  src_x       (int16 LE)
//   [2..3]  src_y       (int16 LE)
//   [4..5]  w           (uint16 LE)
//   [6..7]  h           (uint16 LE)
//   [8..11] vram_offset (uint32 LE)
// ---------------------------------------------------------------------------
#if FEATURE_CAPTURE_REGION
void handle_capture_region(const uint8_t *payload, uint32_t len)
{
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    if (len < 12)   { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    if (g_vram == NULL) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    int16_t  src_x  = (int16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    int16_t  src_y  = (int16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8));
    uint16_t w      = (uint16_t)payload[4] | ((uint16_t)payload[5] << 8);
    uint16_t h      = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8);
    uint32_t voff   = (uint32_t)payload[8]  | ((uint32_t)payload[9]  << 8)
                    | ((uint32_t)payload[10] << 16) | ((uint32_t)payload[11] << 24);

    if (w == 0 || h == 0) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    uint32_t bpp        = (g_fb_bpp == 16) ? 2u : 1u;
    uint32_t row_bytes  = (uint32_t)w * bpp;
    uint32_t total_size = row_bytes * h;

    if (voff + total_size > g_vram_size) { coprocessor_set_error(ERR_VRAM_FULL); return; }

    for (uint16_t row = 0; row < h; row++) {
        int16_t fy = src_y + (int16_t)row;
        if (fy < 0 || fy >= (int16_t)g_fb_height) {
            // Fill with zeros for out-of-bounds rows
            memset(g_vram + voff + (uint32_t)row * row_bytes, 0, row_bytes);
            continue;
        }
        // Clamp columns
        uint16_t col_start = 0;
        int16_t  fx_start  = src_x;
        if (fx_start < 0) { col_start = (uint16_t)(-fx_start); }
        uint16_t copy_cols = w;
        if (fx_start + (int16_t)copy_cols > (int16_t)g_fb_width) {
            copy_cols = (uint16_t)(g_fb_width - (uint16_t)(fx_start > 0 ? fx_start : 0));
        }

        uint8_t *dst_row = g_vram + voff + (uint32_t)row * row_bytes;
        // Leading zeros if src_x < 0
        if (col_start > 0) memset(dst_row, 0, (uint32_t)col_start * bpp);
        // Copy valid pixels
        uint32_t src_col = (fx_start < 0) ? 0u : (uint32_t)fx_start;
        uint8_t *src_ptr = g_fb_back + (uint32_t)(uint16_t)fy * g_fb_stride + src_col * bpp;
        uint32_t valid   = (col_start < copy_cols) ? (uint32_t)(copy_cols - col_start) : 0u;
        if (valid > 0) memcpy(dst_row + (uint32_t)col_start * bpp, src_ptr, valid * bpp);
        // Trailing zeros if needed
        uint32_t written = (uint32_t)col_start + valid;
        if (written < w) memset(dst_row + written * bpp, 0, (w - written) * bpp);
    }

    // Update high-water mark
    uint32_t high = voff + total_size;
    if (high > g_vram_used) g_vram_used = high;

    coprocessor_set_error(ERR_OK);
}
#endif // FEATURE_CAPTURE_REGION
