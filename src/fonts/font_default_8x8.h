#pragma once
#include <stdint.h>

// =============================================================================
// Default 8x8 Pixel Bitmap Font
// =============================================================================
// Covers ASCII 0x20 (space) through 0x7F (DEL marker)
// 96 characters x 8 bytes each = 768 bytes, stored in flash (XIP)
// Each byte represents one row; bit 7 = leftmost column.

#define FONT_8X8_FIRST_CHAR  0x20
#define FONT_8X8_LAST_CHAR   0x7E
#define FONT_8X8_CHAR_COUNT  96
#define FONT_8X8_CHAR_WIDTH   8
#define FONT_8X8_CHAR_HEIGHT  8
#define FONT_8X8_BASELINE     7

extern const uint8_t font_default_8x8[FONT_8X8_CHAR_COUNT][FONT_8X8_CHAR_HEIGHT];
