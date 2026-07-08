// gpu_opcodes.h — Host SDK: SPI opcode + sub-opcode + error code constants
// Mirrors firmware's include/opcodes.h + include/error_codes.h.
// Standalone: no firmware dependencies. Include in your host MCU project.
#pragma once
#include <stdint.h>

// =============================================================================
// Command Opcodes (spec §5.2)
// =============================================================================
#define GPU_OP_SYSTEM_CONFIG        0x10u
#define GPU_OP_SOFT_RESET           0x11u
#define GPU_OP_BEGIN_FRAME          0x12u
#define GPU_OP_END_FRAME            0x13u
#define GPU_OP_SET_PIXEL_FORMAT     0x14u
#define GPU_OP_SET_DITHER_MODE      0x15u
#define GPU_OP_ENABLE_FRAME_STATS   0x16u
#define GPU_OP_SET_VM_MODE          0x17u

#define GPU_OP_PUSH_CLIP_RECT       0x20u
#define GPU_OP_POP_CLIP_RECT        0x21u
#define GPU_OP_SET_BLEND_MODE       0x22u

#define GPU_OP_DRAW_PRIMITIVE       0x30u
#define GPU_OP_FILL_SCREEN          0x31u
#define GPU_OP_COPY_REGION          0x32u
#define GPU_OP_REPLACE_COLOR        0x33u
#define GPU_OP_DRAW_TILEMAP         0x34u
#define GPU_OP_SCROLL_SCREEN        0x35u

#define GPU_OP_BLIT_SPRITE          0x50u
#define GPU_OP_DRAW_VRAM_SPRITE     0x51u
#define GPU_OP_DRAW_9PATCH          0x52u
#define GPU_OP_CAPTURE_REGION       0x53u

#define GPU_OP_RENDER_TEXT          0x60u

#define GPU_OP_SWAP_BUFFERS         0x70u
#define GPU_OP_SWAP_BUFFERS_IMM     0x71u

#define GPU_OP_UPLOAD_VRAM          0x80u
#define GPU_OP_VRAM_ALLOC_NAMED     0x81u
#define GPU_OP_VRAM_LOOKUP          0x82u
#define GPU_OP_VRAM_FREE_NAMED      0x83u
#define GPU_OP_BEGIN_DISPLAY_LIST   0x84u
#define GPU_OP_END_DISPLAY_LIST     0x85u
#define GPU_OP_EXEC_DISPLAY_LIST    0x86u

#define GPU_OP_LOAD_PROCEDURE       0x90u
#define GPU_OP_EXEC_PROCEDURE       0x91u
#define GPU_OP_VM_RESET             0x92u
#define GPU_OP_SCHEDULE_PROCEDURE   0x93u
#define GPU_OP_UNSCHEDULE_PROCEDURE 0x94u

#define GPU_OP_SET_CHROMA_KEY       0xA0u
#define GPU_OP_ENABLE_TRANSPARENCY  0xA1u

#define GPU_OP_GET_STATUS           0xE0u
#define GPU_OP_GET_PROFILE          0xE1u
#define GPU_OP_GET_VRAM_FREE        0xE2u
#define GPU_OP_GET_VRAM_USED        0xE3u
#define GPU_OP_GET_VM_STATE         0xE4u
#define GPU_OP_GET_SRAM_FREE        0xE5u
#define GPU_OP_GET_VERSION          0xE6u
#define GPU_OP_GET_CAPABILITIES     0xE7u
#define GPU_OP_GET_FRAME_STATS      0xE8u
#define GPU_OP_GET_EVENTS           0xE9u
#define GPU_OP_GET_FONT_METADATA    0xEAu

// =============================================================================
// DRAW_PRIMITIVE sub-opcodes (spec §6.1)
// =============================================================================
#define GPU_PRIM_SET_PIXEL           0x01u
#define GPU_PRIM_LINE                0x02u
#define GPU_PRIM_LINE_DASHED         0x03u
#define GPU_PRIM_RECT                0x04u
#define GPU_PRIM_RECT_FILLED         0x05u
#define GPU_PRIM_RECT_ROUNDED        0x06u
#define GPU_PRIM_RECT_ROUNDED_FILLED 0x07u
#define GPU_PRIM_CIRCLE              0x08u
#define GPU_PRIM_CIRCLE_FILLED       0x09u
#define GPU_PRIM_ELLIPSE             0x0Au
#define GPU_PRIM_ELLIPSE_FILLED      0x0Bu
#define GPU_PRIM_ARC                 0x0Cu
#define GPU_PRIM_TRIANGLE            0x0Du
#define GPU_PRIM_TRIANGLE_FILLED     0x0Eu
#define GPU_PRIM_TRIANGLE_GRADIENT   0x0Fu
#define GPU_PRIM_POLYGON_FILLED      0x10u
#define GPU_PRIM_BEZIER_QUAD         0x11u
#define GPU_PRIM_BEZIER_CUBIC        0x12u
#define GPU_PRIM_GRADIENT_RECT       0x13u
#define GPU_PRIM_FLOOD_FILL          0x14u

// =============================================================================
// Error codes (spec §5.4)
// =============================================================================
#define GPU_ERR_OK                  0x00u
#define GPU_ERR_UNKNOWN_OPCODE      0x01u
#define GPU_ERR_INVALID_PARAM       0x02u
#define GPU_ERR_NOT_ACTIVE          0x03u
#define GPU_ERR_UNSUPPORTED_PROFILE 0x04u
#define GPU_ERR_VRAM_FULL           0x05u
#define GPU_ERR_CRC_MISMATCH        0x06u
#define GPU_ERR_FEATURE_UNAVAILABLE 0x07u
#define GPU_ERR_VM_UNAVAILABLE      0x08u
#define GPU_ERR_OVERFLOW            0x09u
#define GPU_ERR_PAYLOAD_TOO_LARGE   0x0Au
#define GPU_ERR_VRAM_NAME_NOT_FOUND 0x0Bu  // VRAM_LOOKUP: hash not in table
#define GPU_ERR_VRAM_NAME_TABLE_FULL 0x0Cu // VRAM_ALLOC_NAMED: all 64 slots used
#define GPU_ERR_DISPLAY_LIST_FULL   0x0Du  // recording exceeded max_bytes
#define GPU_ERR_UNSUPPORTED         0x0Eu  // SET_PIXEL_FORMAT rejected by HAL
#define GPU_ERR_INTERNAL            0xFFu

// =============================================================================
// Protocol constants
// =============================================================================
#define GPU_SYNC_BYTE               0xAAu

// =============================================================================
// Pixel format constants
// =============================================================================
#define GPU_PIXEL_FORMAT_RGB332     0x00u
#define GPU_PIXEL_FORMAT_MONO8      0x01u
#define GPU_PIXEL_FORMAT_INDEX8     0x02u
#define GPU_PIXEL_FORMAT_RGB565     0x10u
#define GPU_PIXEL_FORMAT_RGB121     0x20u
#define GPU_PIXEL_FORMAT_MONO4      0x21u
#define GPU_PIXEL_FORMAT_INDEX4     0x22u
#define GPU_PIXEL_FORMAT_RGB888     0x30u

// =============================================================================
// Capabilities bitmask (GET_CAPABILITIES response, spec §12.2)
// =============================================================================
#define GPU_CAP_RP2350              (1u << 0)
#define GPU_CAP_RGB565              (1u << 1)
#define GPU_CAP_FPU_PRIMITIVES      (1u << 2)
#define GPU_CAP_ARBITRARY_ROTATION  (1u << 3)
#define GPU_CAP_VECTOR_FONTS        (1u << 4)
#define GPU_CAP_PAWN_VM             (1u << 5)
#define GPU_CAP_PAWN_FLOAT          (1u << 6)
#define GPU_CAP_ST7796_COMPAT       (1u << 7)
#define GPU_CAP_DITHERING           (1u << 11)
#define GPU_CAP_DISPLAY_LIST        (1u << 12)
#define GPU_CAP_NAMED_VRAM          (1u << 13)
#define GPU_CAP_BEGIN_END_FRAME     (1u << 14)
#define GPU_CAP_FRAME_STATS         (1u << 15)
#define GPU_CAP_EVENT_BUFFER        (1u << 16)
#define GPU_CAP_VM_PARALLEL_CORE1   (1u << 18)
#define GPU_CAP_VM_COOPERATIVE      (1u << 19)

// =============================================================================
// Transform flags for DRAW_VRAM_SPRITE (spec §7.2)
// =============================================================================
#define GPU_TRANSFORM_ROT_0         0x00u
#define GPU_TRANSFORM_ROT_90        0x01u
#define GPU_TRANSFORM_ROT_180       0x02u
#define GPU_TRANSFORM_ROT_270       0x03u
#define GPU_TRANSFORM_HFLIP         0x04u
#define GPU_TRANSFORM_VFLIP         0x08u
#define GPU_TRANSFORM_PALETTE       0x10u

// =============================================================================
// Named-VRAM constants (VRAM_ALLOC_NAMED / VRAM_LOOKUP / VRAM_FREE_NAMED)
// =============================================================================
// Sentinel returned in the 4-byte MISO response when a named-VRAM op fails.
#define GPU_VRAM_OFFSET_INVALID     0xFFFFFFFFu

// Maximum entries in the GPU named-slot table (firmware constant, for reference).
#define GPU_NAMED_VRAM_SLOTS        64u

// FNV-1a 32-bit hash — use gpu_fnv1a32() to compute the name hash that the
// GPU firmware expects in VRAM_ALLOC_NAMED / VRAM_LOOKUP / VRAM_FREE_NAMED.
// The function is provided in gpu_driver.c.
uint32_t gpu_fnv1a32(const char *name);

// =============================================================================
// Display-list constants (BEGIN_DISPLAY_LIST / EXEC_DISPLAY_LIST)
// =============================================================================
// Maximum recordable slot index (0–63).
#define GPU_DL_MAX_SLOTS            64u

// Maximum nesting depth for EXEC_DISPLAY_LIST calls.
#define GPU_DL_MAX_EXEC_DEPTH       4u

// BLIT_SPRITE RLE flag values
#define GPU_BLIT_RAW                0x00u  // raw pixel data
#define GPU_BLIT_RLE                0x01u  // spec §5.7 RLE encoding

// SCROLL_SCREEN wrap flag
#define GPU_SCROLL_CLAMP            0x00u
#define GPU_SCROLL_WRAP             0x01u

// DRAW_TILEMAP / COPY_REGION flags
#define GPU_REGION_FLAGS_NONE       0x00u
#define GPU_REGION_FLAGS_WRAP       0x01u

// Gradient direction for gpu_gradient_rect()
#define GPU_GRADIENT_HORIZONTAL     0x00u
#define GPU_GRADIENT_VERTICAL       0x01u
