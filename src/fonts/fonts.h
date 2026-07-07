#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Font Registry
// =============================================================================
// Provides a unified lookup table for all compiled-in fonts.

typedef struct {
    uint8_t         font_id;      // Unique font ID (0 = default 8x8)
    uint16_t        char_w;       // Character cell width in pixels
    uint16_t        char_h;       // Character cell height in pixels
    uint16_t        baseline;     // Baseline row within cell (0-indexed from top)
    bool            is_scalable;  // true for vector fonts (future)
    const uint8_t  *data;         // Pointer to flash-resident font bitmap data
} font_entry_t;

extern const font_entry_t g_font_registry[];
extern const uint32_t     g_font_count;

// Lookup a font by ID. Returns NULL if not found.
const font_entry_t *font_get_by_id(uint8_t font_id);
