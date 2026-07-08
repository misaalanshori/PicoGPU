#pragma once
#include <stdint.h>

// COPY_REGION (0x32): src_x,src_y,w,h,dst_x,dst_y,flags — 13 bytes
void handle_copy_region(const uint8_t *payload, uint16_t len);

// REPLACE_COLOR (0x33): old_color(2B), new_color(2B) — 4 bytes
void handle_replace_color(const uint8_t *payload, uint16_t len);

// DRAW_TILEMAP (0x34): 18-byte fixed payload
void handle_draw_tilemap(const uint8_t *payload, uint16_t len);

// SCROLL_SCREEN (0x35): dx,dy,wrap,fill_color — 7 bytes
void handle_scroll_screen(const uint8_t *payload, uint16_t len);
