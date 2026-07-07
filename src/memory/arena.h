#pragma once
// arena.h — Linear bump allocator for GPU framebuffer + VRAM + VM heap
// All GPU memory comes from the static arena. Re-partitioned on every SYSTEM_CONFIG.

#include <stdint.h>
#include <stddef.h>

// Total arena capacity (see feature_flags.h: ARENA_MAX_BYTES = 480 KB)
extern uint8_t  *g_arena_base;    // start of arena backing storage
extern uint32_t  g_arena_used;    // bytes currently allocated
extern uint32_t  g_arena_capacity;// total bytes available

// Zero all arena memory and reset the allocation pointer.
void arena_reset(void);

// Bump-allocate 'size' bytes with the given alignment (must be power of 2).
// Returns a pointer into the arena, or NULL if insufficient space remains.
uint8_t *arena_alloc(uint32_t size, uint32_t align);

// Remaining bytes available for allocation.
uint32_t arena_free_bytes(void);
