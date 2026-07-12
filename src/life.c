#include <hagl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "life.h"
#include "hagl_hal.h"

#define GRID_WIDTH   320
#define GRID_HEIGHT  160
#define GRID_SIZE    (GRID_WIDTH * GRID_HEIGHT)

static uint8_t *current_grid = NULL;
static uint8_t *next_grid = NULL;
static hagl_color_t color_palette[10];

void life_init(hagl_backend_t const *display) {
    current_grid = malloc(GRID_SIZE * sizeof(uint8_t));
    next_grid = malloc(GRID_SIZE * sizeof(uint8_t));
    if (current_grid == NULL || next_grid == NULL) {
        return;
    }

    // Precompute a vibrant heatmap palette
    color_palette[0] = hagl_color(display, 0, 0, 0);       // Dead
    color_palette[1] = hagl_color(display, 0, 255, 255);   // Born: Cyan
    color_palette[2] = hagl_color(display, 0, 220, 200);   // Age 2: Light Teal
    color_palette[3] = hagl_color(display, 0, 255, 0);     // Age 3: Green
    color_palette[4] = hagl_color(display, 150, 255, 0);   // Age 4: Lime Green
    color_palette[5] = hagl_color(display, 230, 230, 0);   // Age 5: Yellow-Green
    color_palette[6] = hagl_color(display, 255, 255, 0);   // Age 6: Yellow
    color_palette[7] = hagl_color(display, 255, 150, 0);   // Age 7: Orange
    color_palette[8] = hagl_color(display, 255, 0, 0);     // Age 8: Red
    color_palette[9] = hagl_color(display, 255, 0, 255);   // Age 9+: Purple

    // Seed randomly with ~22% active cells
    for (int i = 0; i < GRID_SIZE; i++) {
        current_grid[i] = (rand() % 100 < 22) ? 1 : 0;
    }
    memset(next_grid, 0, GRID_SIZE);
}

void life_animate(hagl_backend_t const *display) {
    if (current_grid == NULL || next_grid == NULL) {
        return;
    }

    // Toroidal wrap simulation step
    for (int y = 0; y < GRID_HEIGHT; y++) {
        int prev_y = (y == 0) ? (GRID_HEIGHT - 1) : (y - 1);
        int next_y = (y == (GRID_HEIGHT - 1)) ? 0 : (y + 1);

        uint8_t *row_curr = &current_grid[y * GRID_WIDTH];
        uint8_t *row_prev = &current_grid[prev_y * GRID_WIDTH];
        uint8_t *row_next = &current_grid[next_y * GRID_WIDTH];
        uint8_t *row_dest = &next_grid[y * GRID_WIDTH];

        for (int x = 0; x < GRID_WIDTH; x++) {
            int prev_x = (x == 0) ? (GRID_WIDTH - 1) : (x - 1);
            int next_x = (x == (GRID_WIDTH - 1)) ? 0 : (x + 1);

            // Sum neighbors (cells with value > 0 are alive)
            int neighbors = (row_prev[prev_x] > 0) + (row_prev[x] > 0) + (row_prev[next_x] > 0) +
                            (row_curr[prev_x] > 0) +                     (row_curr[next_x] > 0) +
                            (row_next[prev_x] > 0) + (row_next[x] > 0) + (row_next[next_x] > 0);

            uint8_t val = row_curr[x];
            if (val > 0) {
                // Survival rules
                if (neighbors == 2 || neighbors == 3) {
                    row_dest[x] = (val < 9) ? (val + 1) : 9; // Survived: Age increments
                } else {
                    row_dest[x] = 0; // Death: Under/Over-population
                }
            } else {
                // Birth rule
                if (neighbors == 3) {
                    row_dest[x] = 1; // Born
                } else {
                    row_dest[x] = 0;
                }
            }
        }
    }

    // Swap double buffer pointers
    uint8_t *temp = current_grid;
    current_grid = next_grid;
    next_grid = temp;
}

void life_render(hagl_backend_t const *display) {
    if (current_grid == NULL || active_buffer == NULL) {
        return;
    }

    // Clear active area to black using fast 32-bit aligned memset
    memset(&active_buffer[20 * DISPLAY_WIDTH], 0, DISPLAY_WIDTH * (display->height - 40) * sizeof(hagl_color_t));

    // Render active cells
    for (int cy = 0; cy < GRID_HEIGHT; cy++) {
        uint32_t row0_offset = (20 + cy * 2) * DISPLAY_WIDTH;
        uint32_t row1_offset = row0_offset + DISPLAY_WIDTH;
        uint8_t *row_ptr = &current_grid[cy * GRID_WIDTH];

        for (int cx = 0; cx < GRID_WIDTH; cx++) {
            uint8_t val = row_ptr[cx];
            if (val > 0) {
                hagl_color_t color = color_palette[val];
                uint32_t x_offset = cx * 2;

                // Write 2x2 pixel block directly to framebuffer
                active_buffer[row0_offset + x_offset] = color;
                active_buffer[row0_offset + x_offset + 1] = color;
                active_buffer[row1_offset + x_offset] = color;
                active_buffer[row1_offset + x_offset + 1] = color;
            }
        }
    }
}

void life_close() {
    free(current_grid);
    current_grid = NULL;
    free(next_grid);
    next_grid = NULL;
}
