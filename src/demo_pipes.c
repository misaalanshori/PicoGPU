#include "shared_state.h"
#include <stdio.h>
#include <string.h>

// Pipes screensaver global allocations
bool pipes_occupied[16 * 9] = {false};
int ps_pipe_x = 0;
int ps_pipe_y = 0;
int ps_pipe_dir = 0;
uint16_t ps_pipe_base_color = 0;
int ps_pipe_length = 0;
bool ps_pipe_active = false;

// Draw horizontal pipe cylinder (width G=8, thickness T=6)
void draw_pipe_segment_horizontal(int x1, int x2, int y_center, uint16_t color) {
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    int start_x = (x1 < x2) ? x1 : x2;
    int end_x = (x1 < x2) ? x2 : x1;
    
    static const uint16_t shade_table[6] = {80, 180, 256, 256, 180, 80};
    
    for (int dy = -3; dy <= 2; dy++) {
        int py = y_center + dy;
        if (py < 0 || py >= WATER_H) continue;
        uint16_t shaded = shade_color_rgb565(color, shade_table[dy + 3]);
        
        for (int px = start_x; px <= end_x; px++) {
            if (px >= 0 && px < WATER_W) {
                pixels[py * WATER_W + px] = shaded;
            }
        }
    }
}

// Draw vertical pipe cylinder (width G=8, thickness T=6)
void draw_pipe_segment_vertical(int y1, int y2, int x_center, uint16_t color) {
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    int start_y = (y1 < y2) ? y1 : y2;
    int end_y = (y1 < y2) ? y2 : y1;
    
    static const uint16_t shade_table[6] = {80, 180, 256, 256, 180, 80};
    
    for (int dx = -3; dx <= 2; dx++) {
        int px = x_center + dx;
        if (px < 0 || px >= WATER_W) continue;
        uint16_t shaded = shade_color_rgb565(color, shade_table[dx + 3]);
        
        for (int py = start_y; py <= end_y; py++) {
            if (py >= 0 && py < WATER_H) {
                pixels[py * WATER_W + px] = shaded;
            }
        }
    }
}

// Draw 3D ball joint (sphere) at turning/endpoints
void draw_pipe_joint(int cx, int cy, uint16_t color) {
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    static const uint16_t radial_shade[4] = {256, 220, 150, 70};
    
    for (int dy = -3; dy <= 3; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= WATER_H) continue;
        
        for (int dx = -3; dx <= 3; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= WATER_W) continue;
            
            int dist2 = dx*dx + dy*dy;
            if (dist2 <= 9) {
                int dist = 0;
                if (dist2 > 4) dist = 3;
                else if (dist2 > 1) dist = 2;
                else if (dist2 > 0) dist = 1;
                
                uint16_t shaded = shade_color_rgb565(color, radial_shade[dist]);
                pixels[py * WATER_W + px] = shaded;
            }
        }
    }
}

void init_pipes_game(void) {
    memset(pipes_occupied, 0, sizeof(pipes_occupied));
    memset(water_pixel_buf, 0, sizeof(water_pixel_buf));
    ps_pipe_active = false;
    printf("[pipes] Game grid initialized\r\n");
}

void update_pipes_simulation(void) {
    if (!img_water) return;
    
    static const uint16_t pipe_colors[] = {
        0xF800, // Red
        0x07E0, // Green
        0x001F, // Blue
        0xFFE0, // Yellow
        0xF81F, // Magenta
        0x07FF, // Cyan
        0xFD20, // Orange
        0x780F  // Purple
    };
    
    if (!ps_pipe_active) {
        int start_cell = fast_rand() % (16 * 9);
        int attempts = 0;
        while (pipes_occupied[start_cell] && attempts < 50) {
            start_cell = fast_rand() % (16 * 9);
            attempts++;
        }
        
        if (attempts >= 50) {
            init_pipes_game();
            return;
        }
        
        ps_pipe_x = start_cell % 16;
        ps_pipe_y = start_cell / 16;
        pipes_occupied[start_cell] = true;
        ps_pipe_base_color = pipe_colors[fast_rand() % 8];
        ps_pipe_dir = fast_rand() % 4;
        ps_pipe_active = true;
        ps_pipe_length = 0;
        
        draw_pipe_joint(ps_pipe_x * 8 + 4, ps_pipe_y * 8 + 4, ps_pipe_base_color);
    }
    
    int cx = ps_pipe_x * 8 + 4;
    int cy = ps_pipe_y * 8 + 4;
    
    if (fast_rand() % 100 < 25) {
        int turn = (fast_rand() % 2 == 0) ? 1 : 3;
        ps_pipe_dir = (ps_pipe_dir + turn) % 4;
        draw_pipe_joint(cx, cy, ps_pipe_base_color);
    }
    
    int next_x = ps_pipe_x;
    int next_y = ps_pipe_y;
    if (ps_pipe_dir == 0) next_x++;
    else if (ps_pipe_dir == 1) next_y++;
    else if (ps_pipe_dir == 2) next_x--;
    else if (ps_pipe_dir == 3) next_y--;
    
    bool blocked = false;
    if (next_x < 0 || next_x >= 16 || next_y < 0 || next_y >= 9) {
        blocked = true;
    } else {
        int idx = next_y * 16 + next_x;
        if (pipes_occupied[idx]) {
            blocked = true;
        }
    }
    
    if (blocked) {
        draw_pipe_joint(cx, cy, ps_pipe_base_color);
        ps_pipe_active = false;
    } else {
        int ncx = next_x * 8 + 4;
        int ncy = next_y * 8 + 4;
        
        if (ps_pipe_dir == 0 || ps_pipe_dir == 2) {
            draw_pipe_segment_horizontal(cx, ncx, cy, ps_pipe_base_color);
        } else {
            draw_pipe_segment_vertical(cy, ncy, cx, ps_pipe_base_color);
        }
        
        ps_pipe_x = next_x;
        ps_pipe_y = next_y;
        pipes_occupied[ps_pipe_y * 16 + ps_pipe_x] = true;
        ps_pipe_length++;
        
        if (ps_pipe_length >= 20) {
            draw_pipe_joint(ncx, ncy, ps_pipe_base_color);
            ps_pipe_active = false;
        }
    }
    
    lv_obj_invalidate(img_water);
}
