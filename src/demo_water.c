#include "shared_state.h"
#include <stdio.h>
#include <string.h>

// Water Simulation Global Allocations
int16_t water_buf1[WATER_W * WATER_H] = {0};
int16_t water_buf2[WATER_W * WATER_H] = {0};
int16_t *water_prev = water_buf1;
int16_t *water_next = water_buf2;
uint8_t water_pixel_buf[WATER_W * WATER_H * 2] = {0};

const lv_image_dsc_t water_img_dsc = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = WATER_W,
    .header.h = WATER_H,
    .data_size = WATER_W * WATER_H * 2,
    .data = water_pixel_buf,
};

static inline uint16_t get_pool_color_rgb565(int x, int y) {
    int px = x % 16;
    int py = y % 16;
    if (px < 1 || py < 1) {
        // Grout line
        return 0xDFFF; 
    } else {
        // Cyan-blue mosaic tile color gradient
        int intensity = (px + py) & 0x0F;
        uint8_t r = 0;
        uint8_t g = (uint8_t)(90 + intensity * 7);
        uint8_t b = (uint8_t)(170 + intensity * 5);
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

void trigger_water_splash(void) {
    int cx = 5 + (fast_rand() % (WATER_W - 10));
    int cy = 5 + (fast_rand() % (WATER_H - 10));
    for (int dy = -3; dy <= 3; dy++) {
        for (int dx = -3; dx <= 3; dx++) {
            if (dx*dx + dy*dy <= 9) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < WATER_W && py >= 0 && py < WATER_H) {
                    water_prev[py * WATER_W + px] = 4000;
                }
            }
        }
    }
    current_sfx = SFX_POINT; // Quick splash beep
    sfx_frame = 0;
    printf("[water] Splash triggered at (%d, %d)\r\n", cx, cy);
}

void update_water_simulation(void) {
    if (!img_water) return;

    // 1. Ripple Propagation
    for (int y = 1; y < WATER_H - 1; y++) {
        for (int x = 1; x < WATER_W - 1; x++) {
            int idx = y * WATER_W + x;
            int16_t val = (water_prev[idx - 1] +
                           water_prev[idx + 1] +
                           water_prev[idx - WATER_W] +
                           water_prev[idx + WATER_W]) / 2 - water_next[idx];
            val = val - (val >> 4); // Loss of energy (damping increased to 6.25%)
            water_next[idx] = val;
        }
    }
    // Swap buffers
    int16_t *temp = water_prev;
    water_prev = water_next;
    water_next = temp;

    // 2. Render refractions & caustics to pixel buffer
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    for (int y = 0; y < WATER_H; y++) {
        for (int x = 0; x < WATER_W; x++) {
            int idx = y * WATER_W + x;
            int offset_x = 0;
            int offset_y = 0;
            
            if (x > 0 && x < WATER_W - 1 && y > 0 && y < WATER_H - 1) {
                offset_x = (water_prev[idx - 1] - water_prev[idx + 1]) >> 3;
                offset_y = (water_prev[idx - WATER_W] - water_prev[idx + WATER_W]) >> 3;
            }
            
            int tx = x + offset_x;
            int ty = y + offset_y;
            if (tx < 0) tx = 0; if (tx >= WATER_W) tx = WATER_W - 1;
            if (ty < 0) ty = 0; if (ty >= WATER_H) ty = WATER_H - 1;
            
            uint16_t base_color = get_pool_color_rgb565(tx, ty);
            
            // Caustics shading from refraction slope magnitude
            int shade = offset_x + offset_y;
            
            uint8_t r = (uint8_t)((base_color >> 11) & 0x1F) << 3;
            uint8_t g = (uint8_t)((base_color >> 5) & 0x3F) << 2;
            uint8_t b = (uint8_t)(base_color & 0x1F) << 3;
            
            int r_new = r + shade * 2;
            int g_new = g + shade * 2;
            int b_new = b + shade * 2;
            
            if (r_new > 255) r_new = 255; else if (r_new < 0) r_new = 0;
            if (g_new > 255) g_new = 255; else if (g_new < 0) g_new = 0;
            if (b_new > 255) b_new = 255; else if (b_new < 0) b_new = 0;
            
            pixels[idx] = (uint16_t)(((r_new & 0xF8) << 8) | ((g_new & 0xFC) << 3) | (b_new >> 3));
        }
    }
    
    // Auto raindrop ripples (once every 40 frames on average)
    if ((fast_rand() % 1000) < 15) {
        int rx = 2 + (fast_rand() % (WATER_W - 4));
        int ry = 2 + (fast_rand() % (WATER_H - 4));
        water_prev[ry * WATER_W + rx] = 1000 + (fast_rand() % 1000);
    }

    lv_obj_invalidate(img_water);
}
