#include "shared_state.h"
#include <math.h>
#include <string.h>

// Procedural heightmap generator using layered sines
static inline uint8_t get_terrain_height(int x, int y) {
    float fx = (float)(x & 255) * 0.0245f;
    float fy = (float)(y & 255) * 0.0245f;
    
    float h = sinf(fx) * cosf(fy) * 35.0f;
    h += sinf(fx * 2.5f) * sinf(fy * 2.0f) * 12.0f; // detail hills
    
    int height = (int)(60.0f + h);
    if (height < 0) height = 0;
    if (height > 255) height = 255;
    return (uint8_t)height;
}

// Procedural colormap generator
static inline uint16_t get_terrain_color(int x, int y, uint8_t height) {
    if (height < 45) {
        int wave = (x + y) & 4;
        return wave ? 0x011F : 0x001B;
    } else if (height > 85) {
        return 0xF7BE; // Snowy peaks
    } else if (height > 70) {
        return 0x7BEF; // Rock
    } else {
        // Grass
        int intensity = (height - 45); // 0 to 25
        uint8_t g = (uint8_t)(80 + intensity * 6);
        uint8_t r = (uint8_t)(20 + intensity * 2);
        uint8_t b = (uint8_t)(30 + intensity);
        return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

// Grid cell hash helper for procedural placement
static inline uint32_t cell_hash(int gx, int gy) {
    uint32_t h = (uint32_t)gx * 374761393U + (uint32_t)gy * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}

typedef struct {
    float depth;
    float sx;
    float sy;
    float sh;
} visible_tree_t;

void update_terrain_simulation(void) {
    if (!img_water) return;
    
    static float cam_x = 0.0f;
    static float cam_y = 0.0f;
    static float cam_angle = 0.0f;
    
    // Autopilot movement
    cam_x += cosf(cam_angle) * 1.2f;
    cam_y += sinf(cam_angle) * 1.2f;
    cam_angle += 0.006f;
    
    float cam_height = (float)get_terrain_height((int)cam_x, (int)cam_y) + 40.0f;
    
    uint16_t *pixels = (uint16_t *)water_pixel_buf;
    
    // Z-buffer for billboard occlusion
    static uint8_t depth_buffer[WATER_W * WATER_H];
    memset(depth_buffer, 255, sizeof(depth_buffer));
    
    // Clear buffer to sky gradient
    for (int y = 0; y < WATER_H; y++) {
        uint8_t r = 20;
        uint8_t g = (uint8_t)(40 + y * 2);
        uint8_t b = (uint8_t)(100 + y * 2);
        uint16_t sky_color = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        for (int x = 0; x < WATER_W; x++) {
            pixels[y * WATER_W + x] = sky_color;
        }
    }
    
    float cos_c = cosf(cam_angle);
    float sin_c = sinf(cam_angle);
    
    // 1. Raycast Terrain columns
    for (int i = 0; i < WATER_W; i++) {
        float screen_x = (float)i / (float)WATER_W - 0.5f;
        float rx = cos_c - screen_x * sin_c;
        float ry = sin_c + screen_x * cos_c;
        
        float len = sqrtf(rx*rx + ry*ry);
        rx /= len;
        ry /= len;
        
        int max_y = WATER_H;
        
        for (float depth = 4.0f; depth < 120.0f; depth += 1.2f) {
            int px = (int)(cam_x + rx * depth);
            int py = (int)(cam_y + ry * depth);
            
            uint8_t h = get_terrain_height(px, py);
            int proj_y = (int)((cam_height - (float)h) * 18.0f / depth + 20.0f);
            
            if (proj_y < 0) proj_y = 0;
            if (proj_y >= WATER_H) proj_y = WATER_H - 1;
            
            if (proj_y < max_y) {
                uint16_t color = get_terrain_color(px, py, h);
                if (depth > 60.0f) {
                    float fog = (depth - 60.0f) / 60.0f;
                    if (fog > 0.8f) fog = 0.8f;
                    color = shade_color_rgb565(color, (uint32_t)((1.0f - fog) * 256.0f));
                }
                
                uint8_t d_val = (uint8_t)depth;
                for (int y = proj_y; y < max_y; y++) {
                    pixels[y * WATER_W + i] = color;
                    depth_buffer[y * WATER_W + i] = d_val;
                }
                max_y = proj_y;
            }
        }
    }
    
    // 2. Gather visible trees within frustum
    static visible_tree_t visible_trees[64];
    int visible_tree_count = 0;
    
    int cam_gx = (int)cam_x / 16;
    int cam_gy = (int)cam_y / 16;
    
    for (int gy = cam_gy - 7; gy <= cam_gy + 7; gy++) {
        for (int gx = cam_gx - 7; gx <= cam_gx + 7; gx++) {
            uint32_t hash = cell_hash(gx, gy);
            // 20% tree density
            if ((hash % 100) < 20) {
                int tx = gx * 16 + (hash >> 8) % 12 + 2;
                int ty = gy * 16 + (hash >> 16) % 12 + 2;
                
                uint8_t h = get_terrain_height(tx, ty);
                // Only grow in green valley/grassy area
                if (h >= 45 && h < 70) {
                    float dx = (float)tx - cam_x;
                    float dy = (float)ty - cam_y;
                    
                    float rot_x = dx * sin_c - dy * cos_c;
                    float rot_y = dx * cos_c + dy * sin_c;
                    
                    if (rot_y > 4.0f && rot_y < 120.0f) {
                        float screen_x = -rot_x / rot_y * WATER_W * 0.9f + (WATER_W / 2.0f);
                        if (screen_x >= -20.0f && screen_x < (float)WATER_W + 20.0f) {
                            if (visible_tree_count < 64) {
                                visible_trees[visible_tree_count].depth = rot_y;
                                visible_trees[visible_tree_count].sx = screen_x;
                                visible_trees[visible_tree_count].sy = (cam_height - (float)h) * 18.0f / rot_y + 20.0f;
                                visible_trees[visible_tree_count].sh = 10.0f * 18.0f / rot_y; // 10.0 is base height
                                visible_tree_count++;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 3. Selection sort visible trees by depth descending (back-to-front)
    for (int i = 0; i < visible_tree_count - 1; i++) {
        int max_idx = i;
        for (int j = i + 1; j < visible_tree_count; j++) {
            if (visible_trees[j].depth > visible_trees[max_idx].depth) {
                max_idx = j;
            }
        }
        if (max_idx != i) {
            visible_tree_t temp = visible_trees[i];
            visible_trees[i] = visible_trees[max_idx];
            visible_trees[max_idx] = temp;
        }
    }
    
    // 4. Render Trees (with occlusion check)
    for (int i = 0; i < visible_tree_count; i++) {
        float sx = visible_trees[i].sx;
        float sy = visible_trees[i].sy;
        float sh = visible_trees[i].sh;
        float depth = visible_trees[i].depth;
        
        int trunk_h = (int)(sh * 0.25f);
        if (trunk_h < 1) trunk_h = 1;
        int leaves_h = (int)(sh * 0.75f);
        if (leaves_h < 1) leaves_h = 1;
        int leaves_w = (int)(sh * 0.5f);
        if (leaves_w < 1) leaves_w = 1;
        
        // Draw trunk
        int isx = (int)sx;
        int isy = (int)sy;
        uint8_t d_val = (uint8_t)depth;
        
        for (int y = isy - trunk_h; y < isy; y++) {
            if (y >= 0 && y < WATER_H) {
                if (isx >= 0 && isx < WATER_W) {
                    if (d_val < depth_buffer[y * WATER_W + isx]) {
                        pixels[y * WATER_W + isx] = 0x5A22; // Brown trunk
                    }
                }
            }
        }
        
        // Draw leaves (cone shape)
        for (int dy = 0; dy < leaves_h; dy++) {
            int w = (int)((float)dy / (float)leaves_h * (float)leaves_w);
            int y = isy - trunk_h - dy;
            if (y < 0 || y >= WATER_H) continue;
            
            for (int dx = -w; dx <= w; dx++) {
                int x = isx + dx;
                if (x >= 0 && x < WATER_W) {
                    if (d_val < depth_buffer[y * WATER_W + x]) {
                        uint16_t green = 0x03E0;
                        if (dy > leaves_h * 0.7f) green = 0x07E0; // bright peak
                        else if (dy < leaves_h * 0.3f) green = 0x0280; // dark base
                        
                        // Fog on trees
                        if (depth > 60.0f) {
                            float fog = (depth - 60.0f) / 60.0f;
                            if (fog > 0.8f) fog = 0.8f;
                            green = shade_color_rgb565(green, (uint32_t)((1.0f - fog) * 256.0f));
                        }
                        pixels[y * WATER_W + x] = green;
                    }
                }
            }
        }
    }
    
    lv_obj_invalidate(img_water);
}
