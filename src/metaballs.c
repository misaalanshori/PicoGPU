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
#include <stdint.h>
#include <stdlib.h>

#include "metaballs.h"

struct vector2 {
    int16_t x;
    int16_t y;
};

struct ball {
    struct vector2 position;
    struct vector2 velocity;
    uint16_t radius;
    uint16_t color;
    float radius_sq;
};

struct ball balls[16];

static const uint8_t NUM_BALLS = 2;
static const uint8_t MIN_VELOCITY = 3;
static const uint8_t MAX_VELOCITY = 5;
static const uint8_t MIN_RADIUS = 22;
static const uint8_t MAX_RADIUS = 32;
static const uint8_t PIXEL_SIZE = 1;

void metaballs_init(hagl_backend_t const *display) {
    /* Set up imaginary balls inside screen coordinates. */
    for (int16_t i = 0; i < NUM_BALLS; i++) {
        balls[i].radius = (rand() % MAX_RADIUS) + MIN_RADIUS;
        balls[i].radius_sq = (float)(balls[i].radius * balls[i].radius);
        balls[i].color = 0xffff;
        balls[i].position.x = rand() % display->width;
        balls[i].position.y = rand() % display->height;
        balls[i].velocity.x = (rand() % MAX_VELOCITY) + MIN_VELOCITY;
        balls[i].velocity.y = (rand() % MAX_VELOCITY) + MIN_VELOCITY;
    }
}

void metaballs_animate(hagl_backend_t const *display) {
    for (int16_t i = 0; i < NUM_BALLS; i++) {
        balls[i].position.x += balls[i].velocity.x;
        balls[i].position.y += balls[i].velocity.y;

        /* Touch left or right edge, change direction. */
        if ((balls[i].position.x < 0) | (balls[i].position.x > display->width)) {
            balls[i].velocity.x = balls[i].velocity.x * -1;
        }

        /* Touch top or bottom edge, change direction. */
        if ((balls[i].position.y < 0) | (balls[i].position.y > display->height)) {
            balls[i].velocity.y = balls[i].velocity.y * -1;
        }
    }
}

/* http://www.geisswerks.com/ryan/BLOBS/blobs.html */
#include "hagl_hal.h"

void metaballs_render(hagl_backend_t const *display) {
    if (active_buffer == NULL) {
        return;
    }
    const hagl_color_t background = hagl_color(display, 0, 0, 0);
    const hagl_color_t black = hagl_color(display, 0, 0, 0);
    const hagl_color_t white = hagl_color(display, 255, 255, 255);
    const hagl_color_t green = hagl_color(display, 0, 255, 0);
    hagl_color_t color;

    // Cache to register-like locals to bypass pointer indexing and float conversion in the loops
    float bx0 = (float)balls[0].position.x;
    float by0 = (float)balls[0].position.y;
    float brsq0 = balls[0].radius_sq;

    float bx1 = (float)balls[1].position.x;
    float by1 = (float)balls[1].position.y;
    float brsq1 = balls[1].radius_sq;

    for (uint16_t y = 20; y < display->height - 20; y += PIXEL_SIZE) {
        float fy = (float)y;
        float dy0 = fy - by0;
        float dy1 = fy - by1;
        float dy0_sq = dy0 * dy0;
        float dy1_sq = dy1 * dy1;
        uint32_t row_offset = y * DISPLAY_WIDTH;

        for (uint16_t x = 0; x < display->width; x += PIXEL_SIZE) {
            float fx = (float)x;
            float sum = 0.0f;

            // Ball 0
            float dx0 = fx - bx0;
            float d2_0 = dx0 * dx0 + dy0_sq;
            if (d2_0 > 0.0f) {
                sum += brsq0 / d2_0;
            } else {
                sum += 1000.0f;
            }

            // Ball 1
            float dx1 = fx - bx1;
            float d2_1 = dx1 * dx1 + dy1_sq;
            if (d2_1 > 0.0f) {
                sum += brsq1 / d2_1;
            } else {
                sum += 1000.0f;
            }

            if (sum > 0.65f) {
                color = black;
            } else if (sum > 0.5f) {
                color = white;
            } else if (sum > 0.4f) {
                color = green;
            } else {
                color = background;
            }

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
