#pragma once

// =============================================================================
// PicoGPU Error Codes (spec §5.4)
// =============================================================================
// Returned in response packets and stored in g_state.last_error.

#define ERR_OK                  0x00  // Success / no error
#define ERR_UNKNOWN_OPCODE      0x01  // Unrecognized opcode
#define ERR_BAD_LENGTH          0x02  // Payload length mismatch
#define ERR_CRC_FAIL            0x03  // CRC16-CCITT verification failed
#define ERR_NOT_INITIALIZED     0x04  // Command requires ACTIVE state
#define ERR_ALREADY_INITIALIZED 0x05  // SYSTEM_CONFIG issued in ACTIVE state
#define ERR_OUT_OF_MEMORY       0x06  // Arena / VRAM exhausted
#define ERR_INVALID_PARAM       0x07  // Parameter out of valid range
#define ERR_UNSUPPORTED         0x08  // Feature flag disabled at compile time
#define ERR_CLIP_STACK_OVERFLOW 0x09  // PUSH_CLIP_RECT stack full
#define ERR_CLIP_STACK_UNDERFLOW 0x0A // POP_CLIP_RECT on empty stack
#define ERR_VRAM_NOT_FOUND      0x0B  // Named VRAM handle not found
#define ERR_DISPLAY_LIST_ACTIVE 0x0C  // Nested BEGIN_DISPLAY_LIST
#define ERR_NO_DISPLAY_LIST     0x0D  // END/EXEC_DISPLAY_LIST without BEGIN
#define ERR_VM_FAULT            0x0E  // VM bytecode execution fault
#define ERR_BUSY                0x0F  // GPU busy; command cannot be accepted
#define ERR_FRAME_NOT_OPEN      0x10  // Drawing command outside BEGIN/END_FRAME

#define ERR_INTERNAL            0xFF  // Unrecoverable internal error

// ---------------------------------------------------------------------------
// Additional codes used internally (mapped to spec values above where possible)
// ---------------------------------------------------------------------------
#define ERR_CRC_MISMATCH        ERR_CRC_FAIL        // alias used in packets.c
#define ERR_NOT_ACTIVE          ERR_NOT_INITIALIZED // GPU not in ACTIVE state
#define ERR_FEATURE_UNAVAILABLE ERR_UNSUPPORTED     // compile-time feature off
#define ERR_UNSUPPORTED_PROFILE ERR_INVALID_PARAM   // unknown profile_id
#define ERR_VM_UNAVAILABLE      ERR_UNSUPPORTED     // VM not supported in profile
#define ERR_VRAM_FULL           ERR_OUT_OF_MEMORY   // VRAM region exhausted
#define ERR_PAYLOAD_TOO_LARGE   ERR_BAD_LENGTH      // payload > 65535 bytes

// Aliases for Phase 3 named-VRAM and display-list modules
#define ERR_VRAM_NAME_NOT_FOUND ERR_VRAM_NOT_FOUND  // vram_named.c: hash not in table
#define ERR_VRAM_NAME_TABLE_FULL ERR_OUT_OF_MEMORY  // vram_named.c: all 64 slots used
#define ERR_DISPLAY_LIST_FULL   ERR_OUT_OF_MEMORY   // display_list.c: recording exceeded max_bytes
#define ERR_OVERFLOW            ERR_OUT_OF_MEMORY   // dispatch.c: deferred queue full
