#include "fonts.h"
#include "font_default_8x8.h"
#include <stddef.h>

// =============================================================================
// Font Registry Definition
// =============================================================================

const font_entry_t g_font_registry[] = {
    {
        .font_id     = 0,
        .char_w      = FONT_8X8_CHAR_WIDTH,
        .char_h      = FONT_8X8_CHAR_HEIGHT,
        .baseline    = FONT_8X8_BASELINE,
        .is_scalable = false,
        .data        = &font_default_8x8[0][0],
    },
};

const uint32_t g_font_count = sizeof(g_font_registry) / sizeof(g_font_registry[0]);

const font_entry_t *font_get_by_id(uint8_t font_id) {
    for (uint32_t i = 0; i < g_font_count; i++) {
        if (g_font_registry[i].font_id == font_id) {
            return &g_font_registry[i];
        }
    }
    return NULL;
}
