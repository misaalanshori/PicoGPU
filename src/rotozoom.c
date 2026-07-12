/*

MIT No Attribution

Copyright (c) 2020-2023 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

SPDX-License-Identifier: MIT-0

*/

#include <hagl.h>
#include <math.h>
#include <stdlib.h>

#include "head.h"

static const uint8_t SPEED = 2;
static const uint8_t PIXEL_SIZE = 1;

static uint16_t angle;
// static float sinlut[360];
// static float coslut[360];

void rotozoom_init() {
    /* Generate look up tables. */
    // for (uint16_t i = 0; i < 360; i++) {
    //     sinlut[i] = sin(i * M_PI / 180);
    //     coslut[i] = cos(i * M_PI / 180);
    // }
}

uint8_t rgb565_to_rgb332(uint16_t rgb565) {
    // 1. Extract the individual RGB channels from the 16-bit value
    uint8_t r5 = (rgb565 >> 11) & 0x1F; // Top 5 bits (Red)
    uint8_t g6 = (rgb565 >> 5)  & 0x3F; // Middle 6 bits (Green)
    uint8_t b5 = rgb565         & 0x1F; // Bottom 5 bits (Blue)

    // 2. Truncate them to fit the 3-bit and 2-bit spaces
    // Red: 5 bits to 3 bits (Keep top 3 bits, drop bottom 2)
    uint8_t r3 = (r5 >> 2) & 0x07; 
    
    // Green: 6 bits to 3 bits (Keep top 3 bits, drop bottom 3)
    uint8_t g3 = (g6 >> 3) & 0x07; 
    
    // Blue: 5 bits to 2 bits (Keep top 2 bits, drop bottom 3)
    uint8_t b2 = (b5 >> 3) & 0x03; 

    // 3. Shift into the final RGB332 positions and combine
    return (r3 << 5) | (g3 << 2) | b2;
}

#include "hagl_hal.h"

void rotozoom_render(hagl_backend_t const *display) {
    if (active_buffer == NULL) {
        return;
    }
    float s, c, z;
    size_t cs = sizeof(hagl_color_t);

    s = sinf(angle * (float)M_PI / 180.0f);
    c = cosf(angle * (float)M_PI / 180.0f);
    // s = sinlut[angle];
    // c = coslut[angle];
    z = s * 1.2f;

    for (uint16_t x = 0; x < display->width; x = x + PIXEL_SIZE) {
        for (uint16_t y = 20; y < display->height - 20; y = y + PIXEL_SIZE) {

            /* Get a rotated pixel from the head image. */
            int16_t u = (int16_t)((x * c - y * s) * z) % HEAD_WIDTH;
            int16_t v = (int16_t)((x * s + y * c) * z) % HEAD_HEIGHT;

            u = abs(u);
            if (v < 0) {
                v += HEAD_HEIGHT;
            }
            // 1. Calculate the raw pixel index
            uint32_t pixel_index = (HEAD_WIDTH * v) + u;

            // 2. Calculate the byte index (since every pixel is 2 bytes)
            uint32_t byte_index = pixel_index * 2;

            // 3. Read the bytes and combine them (Big-Endian assumption)
            uint8_t high_byte = head[byte_index];
            uint8_t low_byte  = head[byte_index + 1];
            uint16_t raw_rgb565 = (high_byte << 8) | low_byte;

            // If the colors are STILL weird, swap them to Little-Endian:
            // uint16_t raw_rgb565 = (low_byte << 8) | high_byte;

            // 4. Convert and assign
            hagl_color_t color = rgb565_to_rgb332(raw_rgb565);

            if (1 == PIXEL_SIZE) {
                active_buffer[y * DISPLAY_WIDTH + x] = color;
            } else {
                uint32_t idx = y * DISPLAY_WIDTH + x;
                active_buffer[idx] = color;
                active_buffer[idx + 1] = color;
                active_buffer[idx + DISPLAY_WIDTH] = color;
                active_buffer[idx + DISPLAY_WIDTH + 1] = color;
            }
            // hagl_put_pixel(x, y, *color);
        }
    }
}

void rotozoom_animate() {
    angle = (angle + SPEED) % 360;
}
