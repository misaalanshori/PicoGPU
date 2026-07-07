#pragma once
// blit.h — Sprite blit handlers
// BLIT_SPRITE (0x50): stream blit directly from packet payload to framebuffer
// DRAW_VRAM_SPRITE (0x51): blit from VRAM sprite cache with transforms

#include <stdint.h>

// BLIT_SPRITE handler — payload: x(2), y(2), w(1), h(1), rle_flag(1), pixel_data...
void handle_blit_sprite(const uint8_t *payload, uint16_t len);

// DRAW_VRAM_SPRITE handler — payload: x(2), y(2), w(2), h(2), vram_offset(4), transform_flags(1), palette_color(2)
void handle_draw_vram_sprite(const uint8_t *payload, uint16_t len);
