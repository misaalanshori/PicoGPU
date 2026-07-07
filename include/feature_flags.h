#pragma once

// =============================================================================
// PicoGPU Feature Flags (spec §12.1)
// =============================================================================
// Enable/disable major GPU subsystems. Override before including this header
// or via compiler -D flags.

// --- Feature: Double-buffering ---
// Requires enough SRAM for two framebuffers.
#ifndef FEATURE_DOUBLE_BUFFER
  #define FEATURE_DOUBLE_BUFFER 0
#endif

// --- Feature: Display lists ---
// Enables OP_BEGIN_DISPLAY_LIST / OP_END_DISPLAY_LIST / OP_EXEC_DISPLAY_LIST
#ifndef FEATURE_DISPLAY_LIST
  #define FEATURE_DISPLAY_LIST 1
#endif

// --- Feature: VM (bytecode interpreter) ---
// Enables OP_LOAD_PROCEDURE / OP_EXEC_PROCEDURE / OP_VM_RESET
#ifndef FEATURE_VM
  #define FEATURE_VM 1
#endif

// --- Feature: VM Parallel (Core 1 execution) ---
// Requires FEATURE_VM. Enables VM_MODE_PARALLEL_CORE1.
#ifndef FEATURE_VM_PARALLEL
  #define FEATURE_VM_PARALLEL 0
#endif

// --- Feature: FPU-required primitives ---
// Enables PRIM_TRIANGLE_GRADIENT, PRIM_BEZIER_QUAD, PRIM_BEZIER_CUBIC,
// PRIM_GRADIENT_RECT. Requires hardware FPU (RP2350 has one).
#ifndef FEATURE_FPU_PRIMITIVES
  #define FEATURE_FPU_PRIMITIVES 1
#endif

// --- Feature: Dithering ---
// Enables OP_SET_DITHER_MODE / DITHER_BAYER2 / DITHER_BAYER4
#ifndef FEATURE_DITHERING
  #define FEATURE_DITHERING 1
#endif

// --- Feature: Chroma key transparency ---
// Enables OP_SET_CHROMA_KEY / OP_ENABLE_TRANSPARENCY
#ifndef FEATURE_CHROMA_KEY
  #define FEATURE_CHROMA_KEY 1
#endif

// --- Feature: Named VRAM allocations ---
// Enables OP_VRAM_ALLOC_NAMED / OP_VRAM_LOOKUP / OP_VRAM_FREE_NAMED
#ifndef FEATURE_VRAM_NAMED
  #define FEATURE_VRAM_NAMED 1
#endif

// --- Feature: Frame statistics ---
// Enables OP_ENABLE_FRAME_STATS / OP_GET_FRAME_STATS
#ifndef FEATURE_FRAME_STATS
  #define FEATURE_FRAME_STATS 1
#endif

// --- Feature: Text rendering ---
// Enables OP_RENDER_TEXT / OP_GET_FONT_METADATA
#ifndef FEATURE_TEXT_RENDER
  #define FEATURE_TEXT_RENDER 1
#endif

// --- Feature: Tilemap rendering ---
// Enables OP_DRAW_TILEMAP
#ifndef FEATURE_TILEMAP
  #define FEATURE_TILEMAP 1
#endif

// --- Feature: 9-patch sprite rendering ---
// Enables OP_DRAW_9PATCH
#ifndef FEATURE_9PATCH
  #define FEATURE_9PATCH 1
#endif

// --- Feature: Flood fill ---
// Enables PRIM_FLOOD_FILL. Requires scratch buffer from arena.
#ifndef FEATURE_FLOOD_FILL
  #define FEATURE_FLOOD_FILL 1
#endif

// --- Feature: Screen capture ---
// Enables OP_CAPTURE_REGION
#ifndef FEATURE_CAPTURE_REGION
  #define FEATURE_CAPTURE_REGION 1
#endif

// --- Feature: Profiling ---
// Enables OP_GET_PROFILE
#ifndef FEATURE_PROFILING
  #define FEATURE_PROFILING 0
#endif

// --- Feature: Scheduled procedures ---
// Enables OP_SCHEDULE_PROCEDURE / OP_UNSCHEDULE_PROCEDURE. Requires FEATURE_VM.
#ifndef FEATURE_SCHEDULED_PROCS
  #define FEATURE_SCHEDULED_PROCS 1
#endif

// =============================================================================
// Dependency #error checks (spec §12.1)
// =============================================================================

#if FEATURE_VM_PARALLEL && !FEATURE_VM
  #error "FEATURE_VM_PARALLEL requires FEATURE_VM"
#endif

#if FEATURE_SCHEDULED_PROCS && !FEATURE_VM
  #error "FEATURE_SCHEDULED_PROCS requires FEATURE_VM"
#endif

#if FEATURE_CAPTURE_REGION && !FEATURE_VRAM_NAMED
  #error "FEATURE_CAPTURE_REGION requires FEATURE_VRAM_NAMED"
#endif

#if FEATURE_FRAME_STATS && !FEATURE_PROFILING
  // FEATURE_FRAME_STATS is a lighter-weight subset; allowed without PROFILING.
  // No error — frame stats uses its own lightweight counters.
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
#define ARENA_OVERHEAD_BYTES (160u * 1024u)  // 160 KB: USB stack + pico_hdmi bufs + stacks
#define TOTAL_SRAM_BYTES     (520u * 1024u)  // RP2350 (SRAM0-5)
#define ARENA_MAX_BYTES      (TOTAL_SRAM_BYTES - ARENA_OVERHEAD_BYTES)  // 360 KB

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
