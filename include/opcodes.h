#pragma once

// =============================================================================
// PicoGPU SPI Command Opcodes (spec §5.2-5.3)
// =============================================================================

// --- System Control 0x10-0x17 ---
#define OP_SYSTEM_CONFIG        0x10
#define OP_SOFT_RESET           0x11
#define OP_BEGIN_FRAME          0x12
#define OP_END_FRAME            0x13
#define OP_SET_PIXEL_FORMAT     0x14
#define OP_SET_DITHER_MODE      0x15
#define OP_ENABLE_FRAME_STATS   0x16
#define OP_SET_VM_MODE          0x17

// --- Drawing State 0x20-0x22 ---
#define OP_PUSH_CLIP_RECT       0x20
#define OP_POP_CLIP_RECT        0x21
#define OP_SET_BLEND_MODE       0x22

// --- Drawing Primitives 0x30-0x35 ---
#define OP_DRAW_PRIMITIVE       0x30
#define OP_FILL_SCREEN          0x31
#define OP_COPY_REGION          0x32
#define OP_REPLACE_COLOR        0x33
#define OP_DRAW_TILEMAP         0x34
#define OP_SCROLL_SCREEN        0x35

// --- Blit and Sprite 0x50-0x53 ---
#define OP_BLIT_SPRITE          0x50
#define OP_DRAW_VRAM_SPRITE     0x51
#define OP_DRAW_9PATCH          0x52
#define OP_CAPTURE_REGION       0x53

// --- Text 0x60 ---
#define OP_RENDER_TEXT          0x60

// --- Buffer Management 0x70-0x71 ---
#define OP_SWAP_BUFFERS         0x70
#define OP_SWAP_BUFFERS_IMM     0x71

// --- VRAM and Asset Management 0x80-0x86 ---
#define OP_UPLOAD_VRAM          0x80
#define OP_VRAM_ALLOC_NAMED     0x81
#define OP_VRAM_LOOKUP          0x82
#define OP_VRAM_FREE_NAMED      0x83
#define OP_BEGIN_DISPLAY_LIST   0x84
#define OP_END_DISPLAY_LIST     0x85
#define OP_EXEC_DISPLAY_LIST    0x86

// --- VM Control 0x90-0x94 ---
#define OP_LOAD_PROCEDURE       0x90
#define OP_EXEC_PROCEDURE       0x91
#define OP_VM_RESET             0x92
#define OP_SCHEDULE_PROCEDURE   0x93
#define OP_UNSCHEDULE_PROCEDURE 0x94

// --- Transparency 0xA0-0xA1 ---
#define OP_SET_CHROMA_KEY       0xA0
#define OP_ENABLE_TRANSPARENCY  0xA1

// --- Query Opcodes 0xE0-0xEA ---
#define OP_GET_STATUS           0xE0
#define OP_GET_PROFILE          0xE1
#define OP_GET_VRAM_FREE        0xE2
#define OP_GET_VRAM_USED        0xE3
#define OP_GET_VM_STATE         0xE4
#define OP_GET_SRAM_FREE        0xE5
#define OP_GET_VERSION          0xE6
#define OP_GET_CAPABILITIES     0xE7
#define OP_GET_FRAME_STATS      0xE8
#define OP_GET_EVENTS           0xE9
#define OP_GET_FONT_METADATA    0xEA

// =============================================================================
// DRAW_PRIMITIVE Sub-Opcodes (spec §6.1)
// =============================================================================
#define PRIM_SET_PIXEL           0x01
#define PRIM_LINE                0x02
#define PRIM_LINE_DASHED         0x03
#define PRIM_RECT                0x04
#define PRIM_RECT_FILLED         0x05
#define PRIM_RECT_ROUNDED        0x06
#define PRIM_RECT_ROUNDED_FILLED 0x07
#define PRIM_CIRCLE              0x08
#define PRIM_CIRCLE_FILLED       0x09
#define PRIM_ELLIPSE             0x0A
#define PRIM_ELLIPSE_FILLED      0x0B
#define PRIM_ARC                 0x0C
#define PRIM_TRIANGLE            0x0D
#define PRIM_TRIANGLE_FILLED     0x0E
#define PRIM_TRIANGLE_GRADIENT   0x0F  // FPU only (FEATURE_FPU_PRIMITIVES)
#define PRIM_POLYGON_FILLED      0x10
#define PRIM_BEZIER_QUAD         0x11  // FPU only (FEATURE_FPU_PRIMITIVES)
#define PRIM_BEZIER_CUBIC        0x12  // FPU only (FEATURE_FPU_PRIMITIVES)
#define PRIM_GRADIENT_RECT       0x13  // FPU only (FEATURE_FPU_PRIMITIVES)
#define PRIM_FLOOD_FILL          0x14

// =============================================================================
// Pixel Format Enum Values (for SET_PIXEL_FORMAT payload)
// =============================================================================
#define PIXEL_FORMAT_RGB332  0x00
#define PIXEL_FORMAT_MONO8   0x01
#define PIXEL_FORMAT_INDEX8  0x02
#define PIXEL_FORMAT_RGB565  0x10
#define PIXEL_FORMAT_RGB121  0x20
#define PIXEL_FORMAT_MONO4   0x21
#define PIXEL_FORMAT_INDEX4  0x22
#define PIXEL_FORMAT_RGB888  0x30

// =============================================================================
// VM Mode Values
// =============================================================================
#define VM_MODE_BLOCKING_CORE0    0x00
#define VM_MODE_COOPERATIVE_CORE0 0x01
#define VM_MODE_PARALLEL_CORE1    0x02

// =============================================================================
// Schedule Trigger Types
// =============================================================================
#define TRIGGER_VBLANK       0x00
#define TRIGGER_BEGIN_FRAME  0x01
#define TRIGGER_END_FRAME    0x02

// =============================================================================
// Dither Modes
// =============================================================================
#define DITHER_NONE   0x00
#define DITHER_BAYER2 0x01
#define DITHER_BAYER4 0x02

// =============================================================================
// Blend Modes
// =============================================================================
#define BLEND_OVERWRITE 0x00
#define BLEND_XOR       0x01
#define BLEND_OR        0x02
#define BLEND_AND       0x03

// =============================================================================
// Protocol Constants
// =============================================================================
#define PACKET_SYNC_BYTE 0xAAu
