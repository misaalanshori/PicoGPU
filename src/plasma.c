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
#include <stdint.h>
#include <stdlib.h>

#include "plasma.h"

hagl_color_t *palette;
uint8_t *plasma;

static const uint8_t SPEED = 4;
static const uint8_t PIXEL_SIZE = 1;

void plasma_init(hagl_backend_t const *display) {
    size_t plasma_size = (display->width / PIXEL_SIZE) * ((display->height - 40) / PIXEL_SIZE);
    uint8_t *ptr = plasma = malloc(plasma_size * sizeof(uint8_t));
    palette = malloc(256 * sizeof(hagl_color_t));
    if (plasma == NULL || palette == NULL) {
        return;
    }

    /* Generate nice continous palette. */
    for (uint16_t i = 0; i < 256; i++) {
        const uint8_t r = 128.0f + 128.0f * sinf(((float)M_PI * i / 128.0f) + 1.0f);
        const uint8_t g = 128.0f + 128.0f * sinf(((float)M_PI * i / 64.0f) + 1.0f);
        const uint8_t b = 64;
        palette[i] = hagl_color(display, r, g, b);
    }

    // Precompute 1D sine tables to avoid hundreds of thousands of trigonometric calls
    float *sin_x = malloc(display->width * sizeof(float));
    float *sin_y = malloc(display->height * sizeof(float));
    if (sin_x && sin_y) {
        for (uint16_t x = 0; x < display->width; x++) {
            sin_x[x] = 128.0f + (128.0f * sinf(x / 32.0f));
        }
        for (uint16_t y = 20; y < display->height - 20; y++) {
            sin_y[y] = 128.0f + (128.0f * sinf(y / 24.0f));
        }

        for (uint16_t y = 20; y < display->height - 20; y += PIXEL_SIZE) {
            float v2 = sin_y[y];
            float y_sq = y * y;
            for (uint16_t x = 0; x < display->width; x += PIXEL_SIZE) {
                float v1 = sin_x[x];
                float v3 = 128.0f + (128.0f * sinf(sqrtf(x * x + y_sq) / 24.0f));
                uint8_t color = (v1 + v2 + v3) / 3;
                *(ptr++) = color;
            }
        }
    }
    free(sin_x);
    free(sin_y);
}

#include "hagl_hal.h"

void plasma_render(hagl_backend_t const *display) {
    if (plasma == NULL || palette == NULL || active_buffer == NULL) {
        return;
    }
    uint8_t *ptr = plasma;

    for (uint16_t y = 20; y < display->height - 20; y += PIXEL_SIZE) {
        uint32_t row_offset = y * DISPLAY_WIDTH;
        for (uint16_t x = 0; x < display->width; x += PIXEL_SIZE) {
            /* Get a color for pixel from the plasma buffer. */
            const uint8_t index = *(ptr++);
            const hagl_color_t color = palette[index];
            /* Put a pixel to the display. */
            if (1 == PIXEL_SIZE) {
                active_buffer[row_offset + x] = color;
            } else {
                uint32_t idx = row_offset + x;
                active_buffer[idx] = color;
                active_buffer[idx + 1] = color;
                active_buffer[idx + DISPLAY_WIDTH] = color;
                active_buffer[idx + DISPLAY_WIDTH + 1] = color;
            }
        }
    }
}

void plasma_animate(hagl_backend_t const *display) {
    uint8_t *ptr = plasma;

    for (uint16_t y = 20; y < display->height - 20; y = y + PIXEL_SIZE) {
        for (uint16_t x = 0; x < display->width; x = x + PIXEL_SIZE) {
            /* Get a color from plasma and choose the next color. */
            /* Unsigned integers wrap automatically. */
            const uint8_t index = *ptr + SPEED;
            /* Put the new color back to the plasma buffer. */
            *(ptr++) = index;
        }
    }
}

void plasma_close() {
    free(plasma);
    free(palette);
}
