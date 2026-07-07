#include "text.h"
#include "../fonts/font_default_8x8.h"
#include "effects.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"

// ---------------------------------------------------------------------------
// RENDER_TEXT  (opcode 0x60)
//
// Payload layout (minimum 8 bytes before string):
//   [0..1]  x        (uint16_t LE)
//   [2..3]  y        (uint16_t LE)
//   [4]     font_id  (uint8_t;  0 = default 8x8)
//   [5..6]  color    (uint16_t LE, RGB565)
//   [7]     scale    (uint8_t;  0 = use 1)
//   [8..]   null-terminated string
// ---------------------------------------------------------------------------

#define HEADER_SIZE 8u

void handle_render_text(const uint8_t *payload, uint16_t len)
{
    // Sanity check — need at least the 8-byte header + 1 byte (null terminator)
    if (len < (HEADER_SIZE + 1u)) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Parse header
    uint16_t x       = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8u));
    uint16_t y       = (uint16_t)(payload[2] | ((uint16_t)payload[3] << 8u));
    uint8_t  font_id = payload[4];
    uint16_t color   = (uint16_t)(payload[5] | ((uint16_t)payload[6] << 8u));
    uint8_t  scale   = payload[7];

    // Validate font
    if (font_id != 0u) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    // Default scale
    if (scale == 0u) {
        scale = 1u;
    }

    // String starts at payload[8]; must be null-terminated within the payload
    const uint8_t *str     = payload + HEADER_SIZE;
    uint16_t       str_max = (uint16_t)(len - HEADER_SIZE);

    uint16_t cursor_x = x;

    for (uint16_t si = 0; si < str_max; si++) {
        uint8_t c = str[si];

        if (c == '\0') {
            break;  // end of string
        }

        // Map out-of-range characters to space
        if (c < FONT_8X8_FIRST_CHAR || c > FONT_8X8_LAST_CHAR) {
            cursor_x = (uint16_t)(cursor_x + FONT_8X8_CHAR_WIDTH * scale);
            continue;
        }

        uint32_t glyph_idx = (uint32_t)(c - FONT_8X8_FIRST_CHAR);

        // Render each row of the 8x8 glyph
        for (uint8_t row = 0u; row < FONT_8X8_CHAR_HEIGHT; row++) {
            uint8_t bits = font_default_8x8[glyph_idx][row];

            for (uint8_t col = 0u; col < FONT_8X8_CHAR_WIDTH; col++) {
                if ((bits >> (7u - col)) & 1u) {
                    // With scale: draw a scale×scale block
                    for (uint8_t sy = 0u; sy < scale; sy++) {
                        for (uint8_t sx = 0u; sx < scale; sx++) {
                            effect_write_pixel(
                                (uint16_t)(cursor_x + col * scale + sx),
                                (uint16_t)(y        + row * scale + sy),
                                color);
                        }
                    }
                }
            }
        }

        // Advance cursor by one character width (scaled)
        cursor_x = (uint16_t)(cursor_x + FONT_8X8_CHAR_WIDTH * scale);
    }

    g_state.last_error = ERR_OK;
}
