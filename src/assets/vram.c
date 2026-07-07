#include "vram.h"
#include "error_codes.h"
#include "../state/coprocessor_state.h"

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

    // RLE deferred to Phase 3
    if (rle_flag != 0u) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Validate VRAM pointer initialised
    if (g_vram == NULL) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Bounds check
    if ((offset + count) > g_vram_size) {
        coprocessor_set_error(ERR_VRAM_FULL);
        return;
    }

    // Validate that the payload contains enough pixel data
    uint32_t data_available = len - UPLOAD_HEADER_SIZE;
    if (data_available < count) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Copy pixel data into VRAM
    memcpy(g_vram + offset, payload + UPLOAD_HEADER_SIZE, count);

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
