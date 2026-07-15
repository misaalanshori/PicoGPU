// vram_named.c — Named VRAM slot management (spec §7.3)
// 64-entry FNV-1a hash → VRAM byte offset table.
// 512 bytes of static SRAM outside the arena.
// Cleared on every SYSTEM_CONFIG profile switch and SOFT_RESET.
//
// DESIGN NOTE (M2 — KNOWN LIMITATION):
// Named-VRAM allocation uses g_vram_used as a bump pointer (assigned_offset =
// g_vram_used; g_vram_used += count). This is the SAME g_vram_used that
// vram.c advances when UPLOAD_VRAM is sent at a host-chosen raw offset.
// Mixing both strategies risks overlap:
//   - Named alloc advances g_vram_used forward.
//   - UPLOAD_VRAM at a host-picked offset < g_vram_used overwrites existing data.
//   - UPLOAD_VRAM at offset > g_vram_used does NOT advance g_vram_used to that
//     position, so named alloc may later hand out the same region.
// The safe usage contract is: use EITHER named allocation OR raw UPLOAD_VRAM
// for a given VRAM region, not both interchangeably within the same profile.
// Additionally, handle_vram_free_named() only marks the table slot free but
// does NOT reclaim the VRAM bytes (g_vram_used is never decremented). Repeated
// alloc/free cycles will monotonically consume VRAM until exhaustion. A
// compacting allocator or a per-allocation free-list would be needed to fix this.

#include "vram_named.h"
#include "vram.h"
#include "../state/coprocessor_state.h"
#include "../protocol/dispatch.h"
#include "error_codes.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if FEATURE_NAMED_VRAM

// ---------------------------------------------------------------------------
// Internal table
// ---------------------------------------------------------------------------
#define NAMED_VRAM_SLOTS 64u

typedef struct {
    uint32_t hash;        // FNV-1a hash of the asset name
    uint32_t offset;      // byte offset into g_vram
    uint32_t byte_count;  // size of allocation
    bool     in_use;      // slot is occupied
} vram_named_slot_t;

static vram_named_slot_t s_named_slots[NAMED_VRAM_SLOTS];

// ---------------------------------------------------------------------------
// vram_named_clear
// ---------------------------------------------------------------------------
void vram_named_clear(void) {
    memset(s_named_slots, 0, sizeof(s_named_slots));
}

// ---------------------------------------------------------------------------
// Internal helper: little-endian uint32 serialise
// ---------------------------------------------------------------------------
static void put_u32le_nv(uint8_t *buf, uint32_t v) {
    buf[0]=(uint8_t)v; buf[1]=(uint8_t)(v>>8); buf[2]=(uint8_t)(v>>16); buf[3]=(uint8_t)(v>>24);
}

// ---------------------------------------------------------------------------
// handle_vram_alloc_named  (opcode 0x81)
// Payload: name_hash(4B LE), byte_count(4B LE) = 8 bytes minimum.
// ---------------------------------------------------------------------------
void handle_vram_alloc_named(const uint8_t *payload, uint16_t len) {
    if (len < 8) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint32_t hash  = (uint32_t)payload[0]|((uint32_t)payload[1]<<8)|((uint32_t)payload[2]<<16)|((uint32_t)payload[3]<<24);
    uint32_t count = (uint32_t)payload[4]|((uint32_t)payload[5]<<8)|((uint32_t)payload[6]<<16)|((uint32_t)payload[7]<<24);

    // Idempotent: if hash already allocated, return existing offset
    for (uint32_t i = 0; i < NAMED_VRAM_SLOTS; i++) {
        if (s_named_slots[i].in_use && s_named_slots[i].hash == hash) {
            uint8_t buf[4]; put_u32le_nv(buf, s_named_slots[i].offset);
            dispatch_set_response(buf, 4);
            coprocessor_set_error(ERR_OK);
            return;
        }
    }

    // Find free slot
    int32_t free_idx = -1;
    for (uint32_t i = 0; i < NAMED_VRAM_SLOTS; i++) {
        if (!s_named_slots[i].in_use) { free_idx = (int32_t)i; break; }
    }
    if (free_idx < 0) {
        coprocessor_set_error(ERR_VRAM_NAME_TABLE_FULL);
        uint8_t fail[4] = {0xFF,0xFF,0xFF,0xFF};
        dispatch_set_response(fail, 4);
        return;
    }

    // Allocate VRAM
    if (g_vram_used + count > g_vram_size) {
        coprocessor_set_error(ERR_VRAM_FULL);
        uint8_t fail[4] = {0xFF,0xFF,0xFF,0xFF};
        dispatch_set_response(fail, 4);
        return;
    }
    uint32_t assigned_offset = g_vram_used;
    g_vram_used += count;

    s_named_slots[free_idx].hash       = hash;
    s_named_slots[free_idx].offset     = assigned_offset;
    s_named_slots[free_idx].byte_count = count;
    s_named_slots[free_idx].in_use     = true;

    uint8_t buf[4]; put_u32le_nv(buf, assigned_offset);
    dispatch_set_response(buf, 4);
    coprocessor_set_error(ERR_OK);
}

// ---------------------------------------------------------------------------
// handle_vram_lookup  (opcode 0x82)
// Payload: name_hash(4B LE) = 4 bytes minimum.
// ---------------------------------------------------------------------------
void handle_vram_lookup(const uint8_t *payload, uint16_t len) {
    if (len < 4) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint32_t hash = (uint32_t)payload[0]|((uint32_t)payload[1]<<8)|((uint32_t)payload[2]<<16)|((uint32_t)payload[3]<<24);
    for (uint32_t i = 0; i < NAMED_VRAM_SLOTS; i++) {
        if (s_named_slots[i].in_use && s_named_slots[i].hash == hash) {
            uint8_t buf[4]; put_u32le_nv(buf, s_named_slots[i].offset);
            dispatch_set_response(buf, 4);
            coprocessor_set_error(ERR_OK);
            return;
        }
    }
    uint8_t fail[4] = {0xFF,0xFF,0xFF,0xFF};
    dispatch_set_response(fail, 4);
    coprocessor_set_error(ERR_VRAM_NAME_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// handle_vram_free_named  (opcode 0x83)
// Payload: name_hash(4B LE) = 4 bytes minimum.
// ---------------------------------------------------------------------------
void handle_vram_free_named(const uint8_t *payload, uint16_t len) {
    if (len < 4) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint32_t hash = (uint32_t)payload[0]|((uint32_t)payload[1]<<8)|((uint32_t)payload[2]<<16)|((uint32_t)payload[3]<<24);
    for (uint32_t i = 0; i < NAMED_VRAM_SLOTS; i++) {
        if (s_named_slots[i].in_use && s_named_slots[i].hash == hash) {
            // NOTE (M2): slot entry is freed but VRAM bytes are NOT reclaimed.
            // g_vram_used is not decremented. See file-level comment above.
            s_named_slots[i].in_use = false;
            coprocessor_set_error(ERR_OK);
            return;
        }
    }
    coprocessor_set_error(ERR_VRAM_NAME_NOT_FOUND);
}

#endif // FEATURE_NAMED_VRAM
