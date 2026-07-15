#pragma once

// =============================================================================
// PicoGPU Error Codes — CANONICAL TABLE (spec §5.4)
// =============================================================================
// These values are the authoritative wire-protocol error codes.
// Both firmware and the host SDK must use these exact numeric values.
// Do NOT renumber. Do NOT add new base codes without updating the spec.

#define ERR_OK                  0x00  // Success / no error
#define ERR_UNKNOWN_OPCODE      0x01  // Unrecognized opcode
#define ERR_CRC_MISMATCH        0x02  // CRC16-CCITT verification failed
#define ERR_INVALID_PARAM       0x03  // Parameter out of valid range / bad length
#define ERR_VRAM_FULL           0x04  // VRAM region exhausted
#define ERR_NOT_INITIALIZED     0x05  // Command requires ACTIVE state (GPU not yet configured)
#define ERR_VM_UNAVAILABLE      0x06  // VM not supported in current profile
#define ERR_OUT_OF_MEMORY       0x07  // Arena exhausted (SRAM allocation failed)
#define ERR_FEATURE_UNAVAILABLE 0x08  // Feature flag disabled at compile time
#define ERR_CLIP_STACK_OVERFLOW  0x09 // PUSH_CLIP_RECT with full stack
#define ERR_CLIP_STACK_UNDERFLOW 0x0A // POP_CLIP_RECT on empty stack
#define ERR_VRAM_NOT_FOUND      0x0B  // Named VRAM handle not found
#define ERR_DISPLAY_LIST_ACTIVE 0x0C  // Nested BEGIN_DISPLAY_LIST
#define ERR_NO_DISPLAY_LIST     0x0D  // END/EXEC_DISPLAY_LIST without BEGIN
#define ERR_VM_FAULT            0x0E  // VM bytecode execution fault
#define ERR_BUSY                0x0F  // GPU busy; command cannot be accepted
#define ERR_FRAME_NOT_OPEN      0x10  // Drawing command outside BEGIN/END_FRAME

#define ERR_INTERNAL            0xFF  // Unrecoverable internal error

// ---------------------------------------------------------------------------
// Semantic aliases — map common usage patterns to canonical codes above.
// These never appear on the wire; they're for readability at call sites.
// ---------------------------------------------------------------------------
#define ERR_CRC_FAIL            ERR_CRC_MISMATCH        // alias used in packets.c
#define ERR_BAD_LENGTH          ERR_INVALID_PARAM       // payload length wrong
#define ERR_ALREADY_INITIALIZED ERR_INVALID_PARAM       // SYSTEM_CONFIG in ACTIVE state
#define ERR_NOT_ACTIVE          ERR_NOT_INITIALIZED     // GPU not in ACTIVE state
#define ERR_UNSUPPORTED         ERR_FEATURE_UNAVAILABLE // compile-time feature off
#define ERR_UNSUPPORTED_PROFILE ERR_INVALID_PARAM       // unknown profile_id
#define ERR_PAYLOAD_TOO_LARGE   ERR_INVALID_PARAM       // payload > ring buffer size
#define ERR_VRAM_FULL_          ERR_VRAM_FULL           // compat alias

// Aliases for Phase 3 named-VRAM and display-list modules
#define ERR_VRAM_NAME_NOT_FOUND  ERR_VRAM_NOT_FOUND     // vram_named.c: hash not in table
#define ERR_VRAM_NAME_TABLE_FULL ERR_OUT_OF_MEMORY      // vram_named.c: all 64 slots used
#define ERR_DISPLAY_LIST_FULL    ERR_OUT_OF_MEMORY      // display_list.c: exceeded max_bytes
#define ERR_OVERFLOW             ERR_OUT_OF_MEMORY      // dispatch.c: deferred queue full
