#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "feature_flags.h"

#if FEATURE_NAMED_VRAM

// Named VRAM slot management (spec §7.3).
// 64-entry FNV-1a hash → VRAM offset table.
// Stored in static SRAM *outside* the arena (not zeroed on profile switch).
// vram_named_clear() is called explicitly by profiles.c on SYSTEM_CONFIG and SOFT_RESET.

// VRAM_ALLOC_NAMED (0x81): allocate byte_count bytes in VRAM, register hash→offset,
//   return offset via MISO (4B LE uint32). Returns 0xFFFFFFFF on failure.
void handle_vram_alloc_named(const uint8_t *payload, uint16_t len);

// VRAM_LOOKUP (0x82): return offset for hash via MISO (4B LE uint32),
//   or 0xFFFFFFFF if not found.
void handle_vram_lookup(const uint8_t *payload, uint16_t len);

// VRAM_FREE_NAMED (0x83): mark slot as free. Does NOT zero VRAM data.
void handle_vram_free_named(const uint8_t *payload, uint16_t len);

// Clear entire table. Called by profiles.c on SYSTEM_CONFIG and SOFT_RESET.
void vram_named_clear(void);

#endif // FEATURE_NAMED_VRAM
