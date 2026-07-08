// display_list.c — Display list recording and replay engine (spec §7.4)

#include "display_list.h"
#include "vram.h"
#include "../state/coprocessor_state.h"
#include "../protocol/dispatch.h"
#include "error_codes.h"
#include "opcodes.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if FEATURE_DISPLAY_LIST

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
#define DL_MAX_SLOTS      64u
#define DL_MAX_EXEC_DEPTH  4u

static uint32_t s_vram_base[DL_MAX_SLOTS];       // populated by BEGIN_DISPLAY_LIST
static uint32_t s_max_bytes[DL_MAX_SLOTS];        // populated by BEGIN_DISPLAY_LIST
static bool     s_slot_registered[DL_MAX_SLOTS];  // true once BEGIN was called

static bool     s_recording;    // true during recording
static uint8_t  s_active_slot;  // slot being recorded
static uint32_t s_cursor;       // bytes recorded so far (excluding 8-byte header)

static uint8_t  s_exec_depth;   // nesting depth for EXEC

// ---------------------------------------------------------------------------
// display_list_reset  — called on SOFT_RESET / SYSTEM_CONFIG
// ---------------------------------------------------------------------------
void display_list_reset(void) {
    memset(s_vram_base,       0, sizeof(s_vram_base));
    memset(s_max_bytes,       0, sizeof(s_max_bytes));
    memset(s_slot_registered, 0, sizeof(s_slot_registered));
    s_recording   = false;
    s_active_slot = 0;
    s_cursor      = 0;
    s_exec_depth  = 0;
}

// ---------------------------------------------------------------------------
// dl_is_recording
// ---------------------------------------------------------------------------
bool dl_is_recording(void) { return s_recording; }

// ---------------------------------------------------------------------------
// handle_begin_display_list  (opcode 0x84)
// Payload: slot_id(1B), vram_offset(4B LE), max_bytes(4B LE) = 9 bytes.
// ---------------------------------------------------------------------------
void handle_begin_display_list(const uint8_t *payload, uint16_t len) {
    if (len < 9) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    if (s_recording) { coprocessor_set_error(ERR_INVALID_PARAM); return; } // nested recording forbidden

    uint8_t  slot_id     = payload[0];
    uint32_t vram_offset = (uint32_t)payload[1]|((uint32_t)payload[2]<<8)|((uint32_t)payload[3]<<16)|((uint32_t)payload[4]<<24);
    uint32_t max_bytes   = (uint32_t)payload[5]|((uint32_t)payload[6]<<8)|((uint32_t)payload[7]<<16)|((uint32_t)payload[8]<<24);

    if (slot_id >= DL_MAX_SLOTS) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    // Validate VRAM region: header (8B) + max_bytes must fit
    if (g_vram == NULL || vram_offset + 8u + max_bytes > g_vram_size) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    s_vram_base[slot_id]       = vram_offset;
    s_max_bytes[slot_id]       = max_bytes;
    s_slot_registered[slot_id] = true;
    s_active_slot = slot_id;
    s_cursor      = 0;
    s_recording   = true;
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// handle_end_display_list  (opcode 0x85)
// No payload. Writes 8-byte header into VRAM and finalises the recording.
// ---------------------------------------------------------------------------
void handle_end_display_list(void) {
    if (!s_recording) { coprocessor_set_error(ERR_INVALID_PARAM); return; }

    // Write 8-byte header at vram_base[slot]
    uint32_t base = s_vram_base[s_active_slot];
    dl_header_t hdr;
    hdr.magic      = DL_MAGIC;
    hdr.slot_id    = s_active_slot;
    hdr._pad       = 0;
    hdr.byte_count = (uint16_t)(s_cursor & 0xFFFF);
    memcpy(g_vram + base, &hdr, sizeof(dl_header_t));

    s_recording = false;
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// dl_record_command
// Called by dispatch.c for every incoming command while s_recording == true.
// Returns true  → command consumed by recorder; caller must NOT execute it.
// Returns false → command bypasses recording; caller executes normally.
// ---------------------------------------------------------------------------
bool dl_record_command(uint8_t opcode, const uint8_t *payload, uint16_t len) {
    // Streaming commands cannot be recorded
    if (opcode == OP_BLIT_SPRITE || opcode == OP_UPLOAD_VRAM || opcode == OP_LOAD_PROCEDURE) {
        coprocessor_set_error(ERR_FEATURE_UNAVAILABLE);
        return true;  // consumed (drop it — do NOT execute during recording)
    }
    // Query and management opcodes bypass recording
    if (opcode >= 0xE0u || opcode == OP_SYSTEM_CONFIG || opcode == OP_SOFT_RESET) {
        return false;  // not consumed — caller executes normally
    }

    // Encode: [opcode(1B)][len_lo(1B)][len_hi(1B)][payload(len)]
    uint32_t needed = 3u + (uint32_t)len;
    uint32_t slot   = s_active_slot;
    if (s_cursor + needed > s_max_bytes[slot]) {
        coprocessor_set_error(ERR_DISPLAY_LIST_FULL);
        return true;  // consumed (dropped)
    }

    uint8_t *dst = g_vram + s_vram_base[slot] + 8u + s_cursor;
    dst[0] = opcode;
    dst[1] = (uint8_t)(len & 0xFF);
    dst[2] = (uint8_t)(len >> 8);
    if (len > 0 && payload != NULL) memcpy(dst + 3, payload, len);
    s_cursor += needed;
    // The command is NOT executed during recording — return true to suppress execution
    return true;
}

// ---------------------------------------------------------------------------
// handle_exec_display_list  (opcode 0x86)
// Payload: slot_id(1B) = 1 byte.
// ---------------------------------------------------------------------------
void handle_exec_display_list(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint8_t slot_id = payload[0];
    if (slot_id >= DL_MAX_SLOTS || !s_slot_registered[slot_id]) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }
    if (s_exec_depth >= DL_MAX_EXEC_DEPTH) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    // Verify header magic
    uint32_t base = s_vram_base[slot_id];
    if (base + sizeof(dl_header_t) > g_vram_size) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }
    dl_header_t hdr;
    memcpy(&hdr, g_vram + base, sizeof(dl_header_t));
    if (hdr.magic != DL_MAGIC || hdr.slot_id != slot_id) {
        coprocessor_set_error(ERR_INVALID_PARAM); return;
    }

    s_exec_depth++;

    // Replay command bytes
    uint32_t pos = 0;
    while (pos + 3u <= (uint32_t)hdr.byte_count) {
        uint8_t *p    = g_vram + base + 8u + pos;
        uint8_t  op   = p[0];
        uint16_t plen = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
        if (pos + 3u + plen > (uint32_t)hdr.byte_count) break;  // corrupt
        dispatch_command(op, p + 3, plen);
        pos += 3u + plen;
    }

    s_exec_depth--;
    coprocessor_set_error(ERR_OK);
}

#endif // FEATURE_DISPLAY_LIST
