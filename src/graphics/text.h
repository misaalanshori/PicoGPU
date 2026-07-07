#pragma once
#include <stdint.h>

// RENDER_TEXT (opcode 0x60) handler
// Payload: x(2B LE), y(2B LE), font_id(1B), color(2B LE), scale(1B, 0=default=1),
//          null-terminated string bytes
void handle_render_text(const uint8_t *payload, uint16_t len);
