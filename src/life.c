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

    // Split the 320x160 grid into a 4x4 array of chunks (each chunk is 80x40 cells)
    int chunk_alive[16] = {0};
    int chunk_diff[16] = {0};

    // Toroidal wrap simulation step
    for (int y = 0; y < GRID_HEIGHT; y++) {
        int prev_y = (y == 0) ? (GRID_HEIGHT - 1) : (y - 1);
        int next_y = (y == (GRID_HEIGHT - 1)) ? 0 : (y + 1);

        uint8_t *row_curr = &current_grid[y * GRID_WIDTH];
        uint8_t *row_prev = &current_grid[prev_y * GRID_WIDTH];
        uint8_t *row_next = &current_grid[next_y * GRID_WIDTH];
        uint8_t *row_dest = &next_grid[y * GRID_WIDTH];

        int chunk_y_idx = y / 40;

        for (int x = 0; x < GRID_WIDTH; x++) {
            int prev_x = (x == 0) ? (GRID_WIDTH - 1) : (x - 1);
            int next_x = (x == (GRID_WIDTH - 1)) ? 0 : (x + 1);

            // Sum neighbors (cells with value > 0 are alive)
            int neighbors = (row_prev[prev_x] > 0) + (row_prev[x] > 0) + (row_prev[next_x] > 0) +
                            (row_curr[prev_x] > 0) +                     (row_curr[next_x] > 0) +
                            (row_next[prev_x] > 0) + (row_next[x] > 0) + (row_next[next_x] > 0);

            uint8_t val = row_curr[x];
            uint8_t next_val = 0;
            if (val > 0) {
                // Survival rules
                if (neighbors == 2 || neighbors == 3) {
                    next_val = (val < 9) ? (val + 1) : 9; // Survived: Age increments
                } else {
                    next_val = 0; // Death: Under/Over-population
                }
            } else {
                // Birth rule
                if (neighbors == 3) {
                    next_val = 1; // Born
                } else {
                    next_val = 0;
                }
            }

            int chunk_x_idx = x / 80;
            int chunk_idx = chunk_y_idx * 4 + chunk_x_idx;

            // Compare next_val with two frames ago (stored in row_dest[x] before overwrite)
            if ((next_val > 0) != (row_dest[x] > 0)) {
                chunk_diff[chunk_idx]++;
            }

            row_dest[x] = next_val;

            if (next_val > 0) {
                chunk_alive[chunk_idx]++;
            }
        }
    }

    // Process inactivity and revive on a per-chunk basis
    for (int cy_chunk = 0; cy_chunk < 4; cy_chunk++) {
        for (int cx_chunk = 0; cx_chunk < 4; cx_chunk++) {
            int chunk_idx = cy_chunk * 4 + cx_chunk;

            // Thresholds: less than 25 alive cells, or less than 2 differences (static/oscillating)
            if (chunk_alive[chunk_idx] < 25 || chunk_diff[chunk_idx] < 2) {
                int start_x = cx_chunk * 80;
                int end_x = start_x + 80;
                int start_y = cy_chunk * 40;
                int end_y = start_y + 40;

                // Collect old cells (age >= 6) locally inside this chunk
                int old_cell_x[32];
                int old_cell_y[32];
                int old_cell_count = 0;

                for (int y = start_y; y < end_y && old_cell_count < 32; y++) {
                    uint8_t *row_dest = &next_grid[y * GRID_WIDTH];
                    for (int x = start_x; x < end_x && old_cell_count < 32; x++) {
                        if (row_dest[x] >= 6) {
                            old_cell_x[old_cell_count] = x;
                            old_cell_y[old_cell_count] = y;
                            old_cell_count++;
                        }
                    }
                }

                if (old_cell_count > 0) {
                    // Revive a 7x7 neighborhood around up to 2 old survivors in the chunk
                    int groups = (old_cell_count > 2) ? 2 : old_cell_count;
                    for (int g = 0; g < groups; g++) {
                        int target_idx = rand() % old_cell_count;
                        int ox = old_cell_x[target_idx];
                        int oy = old_cell_y[target_idx];

                        for (int dy = -3; dy <= 3; dy++) {
                            int ry = (oy + dy + GRID_HEIGHT) % GRID_HEIGHT;
                            uint8_t *row_dest = &next_grid[ry * GRID_WIDTH];
                            for (int dx = -3; dx <= 3; dx++) {
                                int rx = (ox + dx + GRID_WIDTH) % GRID_WIDTH;
                                if (rand() % 100 < 35) {
                                    row_dest[rx] = 1;
                                }
                            }
                        }
                    }
                } else {
                    // If the chunk is completely dead, drop a random 5x5 spark inside it
                    int sx = start_x + 10 + (rand() % 60);
                    int sy = start_y + 10 + (rand() % 20);

                    for (int dy = -2; dy <= 2; dy++) {
                        int ry = (sy + dy + GRID_HEIGHT) % GRID_HEIGHT;
                        uint8_t *row_dest = &next_grid[ry * GRID_WIDTH];
                        for (int dx = -2; dx <= 2; dx++) {
                            int rx = (sx + dx + GRID_WIDTH) % GRID_WIDTH;
                            if (rand() % 100 < 40) {
                                row_dest[rx] = 1;
                            }
                        }
                    }
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

    // No screen-wide memset/clear to prevent flickering in single-buffered setup.
    // Instead, overwrite every single cell directly (dead cells are drawn in black).
    for (int cy = 0; cy < GRID_HEIGHT; cy++) {
        uint32_t row0_offset = (20 + cy * 2) * DISPLAY_WIDTH;
        uint32_t row1_offset = row0_offset + DISPLAY_WIDTH;
        uint8_t *row_ptr = &current_grid[cy * GRID_WIDTH];

        for (int cx = 0; cx < GRID_WIDTH; cx++) {
            hagl_color_t color = color_palette[row_ptr[cx]];
            uint32_t x_offset = cx * 2;

            // Write 2x2 pixel block directly to framebuffer
            active_buffer[row0_offset + x_offset] = color;
            active_buffer[row0_offset + x_offset + 1] = color;
            active_buffer[row1_offset + x_offset] = color;
            active_buffer[row1_offset + x_offset + 1] = color;
        }
    }
}

void life_close() {
    free(current_grid);
    current_grid = NULL;
    free(next_grid);
    next_grid = NULL;
}
