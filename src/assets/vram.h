#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "feature_flags.h"

// VRAM sprite cache management.
// g_vram points into the arena, allocated by profiles.c during SYSTEM_CONFIG.

extern uint8_t  *g_vram;        // pointer to VRAM region in arena
extern uint32_t  g_vram_size;   // total bytes in sprite cache
extern uint32_t  g_vram_used;   // high-water mark of used bytes

// Called by profiles.c to set up VRAM region from arena
void vram_init(uint8_t *ptr, uint32_t size);

// UPLOAD_VRAM handler (opcode 0x80)
// Payload: byte_offset(4B LE), byte_count(4B LE), rle_flag(1B), then pixel_data
void handle_upload_vram(const uint8_t *payload, uint32_t len);

// GET_VRAM_FREE response
uint32_t vram_get_free(void);
// GET_VRAM_USED response
uint32_t vram_get_used(void);

#if FEATURE_CAPTURE_REGION
// CAPTURE_REGION handler (opcode 0x53)
// Payload: src_x(2B LE), src_y(2B LE), w(2B LE), h(2B LE), vram_offset(4B LE)
void handle_capture_region(const uint8_t *payload, uint32_t len);
#endif
