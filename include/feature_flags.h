#pragma once

// =============================================================================
// PicoGPU Feature Flags (spec §12.1)
// =============================================================================
// Each flag declared here matches the exact name used in the implementation
// (.c) files. Override via compiler -D flags or platformio.ini build_flags.
// Undefined flags silently evaluate to 0 in #if; having explicit defaults
// here makes that visible and intentional.

// --- Target hardware ---
// RP2350-specific code paths (HSTX, hardware FPU, etc.)
#ifndef FEATURE_TARGET_RP2350
  #define FEATURE_TARGET_RP2350 0
#endif

// --- Pixel formats ---
#ifndef FEATURE_RGB565
  #define FEATURE_RGB565 0
#endif

// --- FPU-required drawing primitives ---
// Enables PRIM_TRIANGLE_GRADIENT, PRIM_BEZIER_QUAD, PRIM_BEZIER_CUBIC,
// PRIM_GRADIENT_RECT. Requires hardware FPU (RP2350 only).
#ifndef FEATURE_FPU_PRIMITIVES
  #define FEATURE_FPU_PRIMITIVES 0
#endif

// --- Arbitrary-angle sprite rotation (FPU path in blit.c) ---
#ifndef FEATURE_ARBITRARY_ROTATION
  #define FEATURE_ARBITRARY_ROTATION 0
#endif

// --- Dithering ---
// Enables OP_SET_DITHER_MODE / DITHER_BAYER2 / DITHER_BAYER4
#ifndef FEATURE_DITHERING
  #define FEATURE_DITHERING 0
#endif

// --- Screen capture to VRAM ---
// Enables OP_CAPTURE_REGION
#ifndef FEATURE_CAPTURE_REGION
  #define FEATURE_CAPTURE_REGION 0
#endif

// --- Named VRAM allocations ---
// Enables OP_VRAM_ALLOC_NAMED / OP_VRAM_LOOKUP / OP_VRAM_FREE_NAMED
#ifndef FEATURE_NAMED_VRAM
  #define FEATURE_NAMED_VRAM 0
#endif

// --- Display lists ---
// Enables OP_BEGIN_DISPLAY_LIST / OP_END_DISPLAY_LIST / OP_EXEC_DISPLAY_LIST
#ifndef FEATURE_DISPLAY_LIST
  #define FEATURE_DISPLAY_LIST 0
#endif

// --- Single-deferred draw queue ---
// On BUFFERING_SINGLE_DEFERRED profiles, draw commands are queued in SRAM
// and flushed at END_FRAME rather than executed during active scanout.
#ifndef FEATURE_DEFERRED_DRAW
  #define FEATURE_DEFERRED_DRAW 0
#endif

// --- BEGIN_FRAME / END_FRAME lifecycle ---
// Enables OP_BEGIN_FRAME / OP_END_FRAME and the frame timer.
#ifndef FEATURE_BEGIN_END_FRAME
  #define FEATURE_BEGIN_END_FRAME 0
#endif

// --- Frame statistics ---
// Enables OP_ENABLE_FRAME_STATS / OP_GET_FRAME_STATS
#ifndef FEATURE_FRAME_STATS
  #define FEATURE_FRAME_STATS 0
#endif

// --- Unsolicited event buffer ---
// Enables GET_EVENTS / EVT_FRAME_COMPLETE etc.
#ifndef FEATURE_EVENT_BUFFER
  #define FEATURE_EVENT_BUFFER 0
#endif

// --- Vector font rendering (Phase 4+) ---
#ifndef FEATURE_VECTOR_FONTS
  #define FEATURE_VECTOR_FONTS 0
#endif

// --- Pawn VM (bytecode interpreter, Phase 4) ---
// Enables OP_LOAD_PROCEDURE / OP_EXEC_PROCEDURE / OP_VM_RESET
#ifndef FEATURE_PAWN_VM
  #define FEATURE_PAWN_VM 0
#endif

// --- Pawn floating-point extension (Phase 4) ---
#ifndef FEATURE_PAWN_FLOAT
  #define FEATURE_PAWN_FLOAT 0
#endif

// --- VM Core 1 parallel execution (Phase 4) ---
#ifndef FEATURE_VM_PARALLEL_CORE1
  #define FEATURE_VM_PARALLEL_CORE1 0
#endif

// --- VM cooperative scheduling (Phase 4) ---
#ifndef FEATURE_VM_COOPERATIVE
  #define FEATURE_VM_COOPERATIVE 0
#endif

// --- ST7796 compatibility shim (Tier 3 display, Phase 5+) ---
#ifndef FEATURE_ST7796_COMPAT
  #define FEATURE_ST7796_COMPAT 0
#endif

// =============================================================================
// Dependency #error checks
// =============================================================================

#if FEATURE_PAWN_FLOAT && !FEATURE_PAWN_VM
  #error "FEATURE_PAWN_FLOAT requires FEATURE_PAWN_VM"
#endif

#if FEATURE_VM_PARALLEL_CORE1 && !FEATURE_PAWN_VM
  #error "FEATURE_VM_PARALLEL_CORE1 requires FEATURE_PAWN_VM"
#endif

#if FEATURE_VM_COOPERATIVE && !FEATURE_PAWN_VM
  #error "FEATURE_VM_COOPERATIVE requires FEATURE_PAWN_VM"
#endif

#if FEATURE_CAPTURE_REGION && !FEATURE_NAMED_VRAM
  #error "FEATURE_CAPTURE_REGION requires FEATURE_NAMED_VRAM (VRAM heap must be initialised)"
#endif

#if FEATURE_FPU_PRIMITIVES && !FEATURE_TARGET_RP2350
  #error "FEATURE_FPU_PRIMITIVES requires FEATURE_TARGET_RP2350 (hardware FPU)"
#endif

#if FEATURE_ARBITRARY_ROTATION && !FEATURE_FPU_PRIMITIVES
  #error "FEATURE_ARBITRARY_ROTATION requires FEATURE_FPU_PRIMITIVES"
#endif

// =============================================================================
// GPIO Pin Assignments (SPI interface)
// =============================================================================
#define PIN_MOSI    0
#define PIN_CS      1
#define PIN_SCK     2
#define PIN_MISO    3
#define PIN_DC      4
#define PIN_BUSY    5
#define PIN_TE      6
#define PIN_RESET   7

// =============================================================================
// SPI PIO Configuration
// =============================================================================
#define SPI_PIO     pio1
#define SPI_PIO_SM  0

// =============================================================================
// GPU Version
// =============================================================================
#define GPU_VERSION_MAJOR 1
#define GPU_VERSION_MINOR 0
#define GPU_VERSION_PATCH 0

// =============================================================================
// Arena Sizing
// =============================================================================
#define ARENA_OVERHEAD_BYTES (80u * 1024u)   // 80 KB: USB stack + pico_hdmi + stacks + other BSS
#define TOTAL_SRAM_BYTES     (520u * 1024u)   // RP2350 (SRAM0-5)
#define ARENA_MAX_BYTES      (TOTAL_SRAM_BYTES - ARENA_OVERHEAD_BYTES)  // 440 KB available

// =============================================================================
// Tuning Constants
// =============================================================================

// Size of the deferred-command queue in bytes
#ifndef DEFERRED_QUEUE_SIZE_BYTES
  #define DEFERRED_QUEUE_SIZE_BYTES 8192
#endif

// Number of VM instructions to execute per cooperative timeslice
#ifndef VM_COOPERATIVE_QUANTUM_INSTRUCTIONS
  #define VM_COOPERATIVE_QUANTUM_INSTRUCTIONS 1000
#endif

// Ring buffer capacity (bytes); must be a power of two
#ifndef RING_BUFFER_SIZE_BYTES
  #define RING_BUFFER_SIZE_BYTES 4096
#endif

// BUSY pin asserted when ring buffer fill >= this percentage
#ifndef RING_BUSY_THRESHOLD_PCT
  #define RING_BUSY_THRESHOLD_PCT 80
#endif
