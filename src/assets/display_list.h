#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "feature_flags.h"

#if FEATURE_DISPLAY_LIST

// Display list recording and replay engine (spec §7.4).
// BEGIN_DISPLAY_LIST (0x84): 9-byte payload: slot_id(1B), vram_offset(4B LE), max_bytes(4B LE)
// END_DISPLAY_LIST   (0x85): no payload
// EXEC_DISPLAY_LIST  (0x86): 1-byte payload: slot_id(1B)

// 8-byte VRAM header written at vram_offset by END_DISPLAY_LIST:
// [0..3] magic (DL_MAGIC)
// [4]    slot_id
// [5]    reserved (0)
// [6..7] byte_count (uint16_t LE) — command bytes after the header
// The header is followed immediately by the CRC16 of the command body,
// stored as an in-band suffix after byte_count bytes of commands.
// H5 fix: checksum replaces the unused _pad byte and is computed by END,
//         verified by EXEC, using the same CRC16-CCITT as the SPI protocol.
#define DL_MAGIC 0x444C4953u  // "DLIS" in LE
typedef struct {
    uint32_t magic;       // DL_MAGIC
    uint8_t  slot_id;
    uint8_t  checksum_lo; // CRC16-CCITT low byte of command body
    uint16_t byte_count;  // command bytes after the header (max 65535)
} dl_header_t;

// Handlers
void handle_begin_display_list(const uint8_t *payload, uint16_t len);
void handle_end_display_list(void);
void handle_exec_display_list(const uint8_t *payload, uint16_t len);

// Called by dispatch.c for every incoming command while recording is active.
// Returns true if the command was consumed by the recording engine (caller must NOT execute it).
// Returns false if the command bypasses recording (streaming commands, management commands).
bool dl_record_command(uint8_t opcode, const uint8_t *payload, uint16_t len);

// Query: is a recording currently active?
bool dl_is_recording(void);

// Reset all state (called on SOFT_RESET / SYSTEM_CONFIG)
void display_list_reset(void);

#endif // FEATURE_DISPLAY_LIST
