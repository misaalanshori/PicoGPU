// arena.c — Static linear arena allocator for PicoGPU
// Spec §3.1: All GPU memory (framebuffers, VRAM, VM heap) uses static arena allocation.
// No per-sprite or per-frame malloc/free. Re-partitioned on every SYSTEM_CONFIG.

#include "arena.h"
#include "feature_flags.h"
#include <string.h>

// Arena backing store — ARENA_MAX_BYTES = TOTAL_SRAM - ARENA_OVERHEAD.
// Aligned to 16 bytes for DMA safety. Placed in BSS (zero-initialized on cold boot).
static uint8_t __attribute__((aligned(16))) _arena_backing[ARENA_MAX_BYTES];

uint8_t  *g_arena_base     = _arena_backing;
uint32_t  g_arena_used     = 0;
uint32_t  g_arena_capacity = ARENA_MAX_BYTES;

void arena_reset(void) {
    memset(_arena_backing, 0, ARENA_MAX_BYTES);
    g_arena_used = 0;
}

uint8_t *arena_alloc(uint32_t size, uint32_t align) {
    if (align == 0) align = 1;
    // Round up g_arena_used to requested alignment
    uint32_t aligned = (g_arena_used + align - 1) & ~(align - 1);
    if (aligned + size > g_arena_capacity) return NULL; // overflow
    g_arena_used = aligned + size;
    return _arena_backing + aligned;
}

uint32_t arena_free_bytes(void) {
    return (g_arena_used <= g_arena_capacity) ? (g_arena_capacity - g_arena_used) : 0;
}
