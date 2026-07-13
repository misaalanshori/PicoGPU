#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/video_output.h"
#include "pico_hdmi/video_output_rt.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pll.h"
#include "hardware/structs/qmi.h"

#include <math.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "audio.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"

// Audio Configuration
#define AUDIO_SAMPLE_RATE 48000
#define TONE_AMPLITUDE 6000

#define SINE_TABLE_SIZE 256
static int16_t sine_table[SINE_TABLE_SIZE];
static uint32_t audio_phase = 0;
static uint32_t phase_increment = 0;
static int audio_frame_counter = 0;

static int current_melody_length = KOROBEINIKI_LENGTH;
static int melody_index = 0;
static int note_frames_remaining = 0;

static void init_sine_table(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = (float)i * 2.0f * 3.14159265f / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(sinf(angle) * TONE_AMPLITUDE);
    }
}

static void advance_melody(void) {
    if (--note_frames_remaining <= 0) {
        melody_index = (melody_index + 1) % current_melody_length;
        note_frames_remaining = current_melody[melody_index].duration;
        uint16_t freq = current_melody[melody_index].freq;
        if (freq > 0) {
            phase_increment = (uint32_t)(((uint64_t)freq << 32) / AUDIO_SAMPLE_RATE);
        } else {
            phase_increment = 0; // Rest
        }
    }
}

typedef enum {
    SFX_NONE,
    SFX_FLAP,
    SFX_POINT,
    SFX_CRASH
} sfx_type_t;

static volatile sfx_type_t current_sfx = SFX_NONE;
static volatile int sfx_frame = 0;

// Flappy Bird Game Variables
static bool game_active = false;
static bool game_over = false;
static float bird_y = 120.0f;
static float bird_velocity = 0.0f;
static float pipe_x = 640.0f;
static float pipe_gap_y = 140.0f;
static int score = 0;
static int high_score = 0;

#define BIRD_X 100
#define BIRD_SIZE 16
#define PIPE_WIDTH 50
#define GAP_SIZE 100
#define GAME_HEIGHT 280
#define GAME_WIDTH 640
#define GRAVITY 0.35f
#define FLAP_IMPULSE -5.5f
#define PIPE_SPEED 3.0f

static lv_obj_t *game_area = NULL;
static lv_obj_t *bird_obj = NULL;
static lv_obj_t *pipe_top = NULL;
static lv_obj_t *pipe_bottom = NULL;
static lv_obj_t *lbl_score = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *tab4 = NULL;

// Ambient Audio Modes
typedef enum {
    AUDIO_TETRIS,
    AUDIO_RAIN,
    AUDIO_NOISE,
    AUDIO_SILENT
} audio_mode_t;

static volatile audio_mode_t current_audio_mode = AUDIO_TETRIS;

// Desktop Clock Variables (Initialized to 2026-07-12 20:51:49)
static int clock_hours = 20;
static int clock_minutes = 51;
static int clock_seconds = 49;
static int clock_day = 12;
static int clock_month = 7;
static int clock_year = 2026;

// Pomodoro & Stopwatch Timer State Machine
typedef enum {
    TIMER_MODE_POMODORO,
    TIMER_MODE_BREAK,
    TIMER_MODE_STOPWATCH
} timer_mode_t;

static timer_mode_t current_timer_mode = TIMER_MODE_POMODORO;
static bool timer_running = false;
static int timer_seconds = 25 * 60; // 25 minutes default

// UI widgets for Clock & Timer
static lv_obj_t *lbl_clock_time = NULL;
static lv_obj_t *lbl_clock_date = NULL;
static lv_obj_t *lbl_timer_mode = NULL;
static lv_obj_t *lbl_timer_val = NULL;
static lv_obj_t *btn_timer_toggle = NULL;
static lv_obj_t *btn_timer_mode = NULL;

// Settings Tab Widgets
static lv_obj_t *btn_theme = NULL;
static lv_obj_t *btn_audio_cycle = NULL;
static lv_obj_t *slider = NULL;
static lv_obj_t *text_editor = NULL;

// Water Simulation Parameters
#define WATER_W 128
#define WATER_H 72
static int16_t water_buf1[WATER_W * WATER_H] = {0};
static int16_t water_buf2[WATER_W * WATER_H] = {0};
static int16_t *water_prev = water_buf1;
static int16_t *water_next = water_buf2;
static uint8_t water_pixel_buf[WATER_W * WATER_H * 2] = {0}; // RGB565 buffer
static lv_obj_t *img_water = NULL;

static const lv_image_dsc_t water_img_dsc = {
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.w = WATER_W,
    .header.h = WATER_H,
    .data_size = WATER_W * WATER_H * 2,
    .data = water_pixel_buf,
};

// Demo Menu & State Machine
typedef enum {
    DEMO_MENU,
    DEMO_WATER,
    DEMO_PIPES
} demo_state_t;

static demo_state_t current_demo_state = DEMO_MENU;
static lv_obj_t *menu_container = NULL;
static lv_obj_t *btn_demo_water = NULL;
static lv_obj_t *btn_demo_pipes = NULL;

// 3D Pipes Screensaver variables
static bool pipes_occupied[16 * 9] = {false};
static int ps_pipe_x = 0;
static int ps_pipe_y = 0;
static int ps_pipe_dir = 0;
static uint16_t ps_pipe_base_color = 0;
static int ps_pipe_length = 0;
static bool ps_pipe_active = false;

// Toast notification variables
static lv_obj_t *toast_label = NULL;
static uint32_t toast_hide_time = 0;

// Dynamic Weekday calculator (Sakamoto's algorithm)
const char* get_day_of_week(int day, int month, int year) {
    static const char *days[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    int dow = (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
    return days[dow];
}

// Custom Space-Tolerant JSON Time & Date Parser
bool parse_time_json(const char *json_raw, int *h, int *m, int *s, int *d, int *mo, int *y) {
    // 1. Strip all whitespaces (spaces, tabs, newlines, carriage returns)
    char json[256];
    int dest_idx = 0;
    for (int i = 0; json_raw[i] != '\0' && dest_idx < 255; i++) {
        char c = json_raw[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            json[dest_idx++] = c;
        }
    }
    json[dest_idx] = '\0';

    // 2. Search for "time":" or 'time':'
    const char *time_ptr = strstr(json, "\"time\":\"");
    if (!time_ptr) {
        time_ptr = strstr(json, "'time':'");
    }
    if (!time_ptr) return false;
    time_ptr += 8; // skip prefix

    int th = 0, tm = 0, ts = 0;
    if (sscanf(time_ptr, "%d:%d:%d", &th, &tm, &ts) != 3) return false;
    if (th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59) return false;

    // 3. Search for "date":" or 'date':'
    const char *date_ptr = strstr(json, "\"date\":\"");
    if (!date_ptr) {
        date_ptr = strstr(json, "'date':'");
    }
    if (!date_ptr) return false;
    date_ptr += 8; // skip prefix

    int ty = 0, tmo = 0, td = 0;
    if (sscanf(date_ptr, "%d-%d-%d", &ty, &tmo, &td) != 3) return false;
    if (ty < 2000 || ty > 2100 || tmo < 1 || tmo > 12 || td < 1 || td > 31) return false;

    *h = th;
    *m = tm;
    *s = ts;
    *y = ty;
    *mo = tmo;
    *d = td;
    
    return true;
}

// Show Toast notification using LVGL
void show_toast(const char *message) {
    if (toast_label == NULL) {
        toast_label = lv_label_create(lv_screen_active());
        if (toast_label) {
            lv_obj_set_style_bg_color(toast_label, lv_color_hex(0x202020), 0);
            lv_obj_set_style_bg_opa(toast_label, LV_OPA_90, 0);
            lv_obj_set_style_text_color(toast_label, lv_color_white(), 0);
            lv_obj_set_style_border_color(toast_label, lv_palette_main(LV_PALETTE_AMBER), 0);
            lv_obj_set_style_border_width(toast_label, 1, 0);
            lv_obj_set_style_pad_all(toast_label, 8, 0);
            lv_obj_set_style_radius(toast_label, 8, 0);
            lv_obj_align(toast_label, LV_ALIGN_BOTTOM_MID, 0, -20);
        }
    }
    
    if (toast_label) {
        lv_label_set_text(toast_label, message);
        lv_obj_remove_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
        toast_hide_time = (time_us_32() / 1000) + 2500; // Visible for 2.5s
    }
}

// Fast, lock-free pseudo-random number generator (LCG)
static uint32_t audio_rand_seed = 0x12345678;
static inline uint32_t fast_rand(void) {
    audio_rand_seed = audio_rand_seed * 1103515245 + 12345;
    return audio_rand_seed;
}

// Shading function using integer math (scale_256: 0 to 256)
static inline uint16_t shade_color_rgb565(uint16_t color, uint32_t scale_256) {
    uint32_t r = (color >> 11) & 0x1F;
    uint32_t g = (color >> 5) & 0x3F;
    uint32_t b = color & 0x1F;
    
    r = (r * scale_256) >> 8;
    g = (g * scale_256) >> 8;
    b = (b * scale_256) >> 8;
    
    return (uint16_t)((r << 11) | (g << 5) | b);
}

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

// Re-init pipes screensaver map
void init_pipes_game(void) {
    memset(pipes_occupied, 0, sizeof(pipes_occupied));
    memset(water_pixel_buf, 0, sizeof(water_pixel_buf));
    ps_pipe_active = false;
    printf("[pipes] Game grid initialized\r\n");
}

// 3D Pipes Screensaver simulation step
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

// Menu Click Callback - Water Sim
static void demo_water_click_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_demo_state = DEMO_WATER;
        if (menu_container) lv_obj_add_flag(menu_container, LV_OBJ_FLAG_HIDDEN);
        if (img_water) lv_obj_remove_flag(img_water, LV_OBJ_FLAG_HIDDEN);
        
        lv_group_t *g = lv_group_get_default();
        if (g) lv_group_remove_all_objs(g);
        
        memset(water_buf1, 0, sizeof(water_buf1));
        memset(water_buf2, 0, sizeof(water_buf2));
        memset(water_pixel_buf, 0, sizeof(water_pixel_buf));
        
        current_sfx = SFX_POINT;
        sfx_frame = 0;
        printf("[demo] Switched to Water Simulation\r\n");
    }
}

// Menu Click Callback - Pipes Sim
static void demo_pipes_click_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_demo_state = DEMO_PIPES;
        if (menu_container) lv_obj_add_flag(menu_container, LV_OBJ_FLAG_HIDDEN);
        if (img_water) lv_obj_remove_flag(img_water, LV_OBJ_FLAG_HIDDEN);
        
        lv_group_t *g = lv_group_get_default();
        if (g) lv_group_remove_all_objs(g);
        
        init_pipes_game();
        
        current_sfx = SFX_POINT;
        sfx_frame = 0;
        printf("[demo] Switched to Pipes Screensaver\r\n");
    }
}

// Exit running simulation back to list menu
void exit_demo_to_menu(void) {
    current_demo_state = DEMO_MENU;
    if (menu_container) lv_obj_remove_flag(menu_container, LV_OBJ_FLAG_HIDDEN);
    if (img_water) lv_obj_add_flag(img_water, LV_OBJ_FLAG_HIDDEN);
    
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_remove_all_objs(g);
        if (btn_demo_water) lv_group_add_obj(g, btn_demo_water);
        if (btn_demo_pipes) lv_group_add_obj(g, btn_demo_pipes);
        if (btn_demo_water) lv_group_focus_obj(btn_demo_water);
    }
    
    current_sfx = SFX_POINT;
    sfx_frame = 0;
    printf("[demo] Exited to Demo Menu\r\n");
}

// Procedural mosaic pool tiles generator
static inline uint16_t get_pool_color_rgb565(int x, int y) {
    int px = x % 16;
    int py = y % 16;
    if (px < 1 || py < 1) {
        // Grout line: very light cyan-white
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

// Inject water splash at random coordinates
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

// Solve 2D wave equations, warp tiles with heightmap refraction, and blend caustics
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



static inline int16_t get_sine_sample(void) {
    uint32_t sfx_phase_inc = 0;
    
    // SFX always takes highest priority (plays over any ambient noise/music)
    if (current_sfx != SFX_NONE) {
        uint32_t sfx_freq = 0;
        if (current_sfx == SFX_FLAP) {
            sfx_freq = 200 + sfx_frame * 80;
            sfx_frame++;
            if (sfx_frame > 5) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_POINT) {
            sfx_freq = (sfx_frame < 5) ? 880 : 1046;
            sfx_frame++;
            if (sfx_frame > 10) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_CRASH) {
            sfx_freq = 150 - sfx_frame * 7;
            if (sfx_freq < 40) sfx_freq = 40;
            sfx_frame++;
            if (sfx_frame > 15) current_sfx = SFX_NONE;
        }
        
        sfx_phase_inc = (uint32_t)(((uint64_t)sfx_freq << 32) / AUDIO_SAMPLE_RATE);
        static uint32_t sfx_phase = 0;
        int16_t s = sine_table[(sfx_phase >> 24) & 0xFF];
        sfx_phase += sfx_phase_inc;
        return s;
    }

    // Process active ambient mode
    if (current_audio_mode == AUDIO_TETRIS) {
        if (phase_increment == 0) return 0;
        int16_t s = sine_table[(audio_phase >> 24) & 0xFF];
        audio_phase += phase_increment;
        return s;
    } else if (current_audio_mode == AUDIO_NOISE) {
        // White Noise: random sample using upper 16 bits of fast LCG
        int16_t raw = (int16_t)(fast_rand() >> 16);
        int16_t noise = (int16_t)((int32_t)raw * 4000 / 32768);
        return noise;
    } else if (current_audio_mode == AUDIO_RAIN) {
        // Rain Sound: Smooth brown wash (LPF at ~76Hz with gain compensation) + decaying raindrop patters
        static float lp_history = 0.0f;
        static float patter_val = 0.0f;
        
        int16_t raw1 = (int16_t)(fast_rand() >> 16);
        float raw_noise = (float)raw1 * (4000.0f / 32768.0f);
        // Leaky integrator for deep, smooth brown noise
        lp_history = 0.99f * lp_history + 0.15f * raw_noise;
        
        // Decay existing patter pop
        patter_val *= 0.92f;
        
        // Trigger a new raindrop patter envelope (~150 times per second)
        if ((fast_rand() % 1000) < 3) {
            int16_t raw2 = (int16_t)(fast_rand() >> 16);
            patter_val = (float)raw2 * (4000.0f / 32768.0f);
        }
        
        float final_val = lp_history + patter_val;
        if (final_val > 32767.0f) final_val = 32767.0f;
        if (final_val < -32768.0f) final_val = -32768.0f;
        
        return (int16_t)final_val;
    }

    // AUDIO_SILENT
    return 0;
}

static void generate_audio(void) {
    int iterations = 0;
    while (hstx_di_queue_get_level() < 200 && iterations < 50) {
        iterations++;
        audio_sample_t samples[4];
        for (int i = 0; i < 4; i++) {
            int16_t s = get_sine_sample();
            samples[i].left = s;
            samples[i].right = s;
        }

        hstx_packet_t packet;
        audio_frame_counter = hstx_packet_set_audio_samples(&packet, samples, 4, audio_frame_counter);

        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        if (!hstx_di_queue_push(&island)) {
            break;
        }
    }
}

// Tick callback helper
static uint32_t my_tick_get_cb(void) {
    return time_us_32() / 1000;
}

// UI Objects
static lv_obj_t *tabview;
static lv_obj_t *progress_bar;
static lv_obj_t *cpu_arc;
static lv_obj_t *lbl_stats_heap;
static lv_obj_t *lbl_stats_resync;
static lv_obj_t *lbl_stats_frame;

// Dark/Light Mode toggle event callback
static void theme_btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        static bool dark_mode = true;
        dark_mode = !dark_mode;

        // Re-initialize default theme with toggled mode using valid display pointer
        lv_display_t *disp = lv_display_get_default();
        if (disp) {
            lv_theme_t *th = lv_theme_default_init(
                disp,
                lv_palette_main(LV_PALETTE_DEEP_PURPLE),
                lv_palette_main(LV_PALETTE_AMBER),
                dark_mode,
                LV_FONT_DEFAULT
            );
            lv_display_set_theme(disp, th);
        }
    }
}

// Timer Toggle Callback
static void timer_toggle_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        timer_running = !timer_running;
        current_sfx = SFX_POINT; // play a quick beep/chime
        sfx_frame = 0;
        
        if (btn_timer_toggle) {
            lv_obj_t *lbl = lv_obj_get_child(btn_timer_toggle, 0);
            if (lbl) {
                lv_label_set_text(lbl, timer_running ? "Pause" : "Start");
            }
        }
        printf("[timer] Toggled timer state: running = %d\r\n", timer_running);
    }
}

// Timer Mode Callback
static void timer_mode_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_sfx = SFX_FLAP; // click sound
        sfx_frame = 0;

        if (timer_running) {
            // Reset active timer
            timer_running = false;
            if (btn_timer_toggle) {
                lv_obj_t *lbl = lv_obj_get_child(btn_timer_toggle, 0);
                if (lbl) lv_label_set_text(lbl, "Start");
            }
            printf("[timer] Reset active timer.\r\n");
        } else {
            // Cycle modes: Pomodoro -> Break -> Stopwatch
            current_timer_mode = (current_timer_mode + 1) % 3;
            printf("[timer] Cycled mode to %d\r\n", current_timer_mode);
        }

        // Reset timer duration according to new mode
        if (current_timer_mode == TIMER_MODE_POMODORO) {
            timer_seconds = 25 * 60;
            if (lbl_timer_mode) lv_label_set_text(lbl_timer_mode, "POMODORO WORK TIMER");
            if (lbl_timer_val) lv_label_set_text(lbl_timer_val, "25:00");
        } else if (current_timer_mode == TIMER_MODE_BREAK) {
            timer_seconds = 5 * 60;
            if (lbl_timer_mode) lv_label_set_text(lbl_timer_mode, "SHORT BREAK TIMER");
            if (lbl_timer_val) lv_label_set_text(lbl_timer_val, "05:00");
        } else if (current_timer_mode == TIMER_MODE_STOPWATCH) {
            timer_seconds = 0;
            if (lbl_timer_mode) lv_label_set_text(lbl_timer_mode, "STOPWATCH LAP TIMER");
            if (lbl_timer_val) lv_label_set_text(lbl_timer_val, "00:00.0");
        }
    }
}

// Audio Mode cycle callback
static lv_obj_t *lbl_audio_mode = NULL;
static void audio_cycle_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_audio_mode = (current_audio_mode + 1) % 4;
        current_sfx = SFX_POINT; // click beep
        sfx_frame = 0;

        if (lbl_audio_mode) {
            if (current_audio_mode == AUDIO_TETRIS) {
                lv_label_set_text(lbl_audio_mode, "Audio Mode: Tetris");
            } else if (current_audio_mode == AUDIO_RAIN) {
                lv_label_set_text(lbl_audio_mode, "Audio Mode: Rain");
            } else if (current_audio_mode == AUDIO_NOISE) {
                lv_label_set_text(lbl_audio_mode, "Audio Mode: Noise");
            } else {
                lv_label_set_text(lbl_audio_mode, "Audio Mode: Silent");
            }
        }
        printf("[audio] Cycled audio mode to %d\r\n", current_audio_mode);
    }
}

void build_ui(void) {
    // Create tabview
    tabview = lv_tabview_create(lv_screen_active());
    if (tabview == NULL) {
        printf("[main] ERROR: Failed to create tabview!\r\n");
        return;
    }
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 40);

    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, "Dashboard");
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, "Controls");
    lv_obj_t *tab_editor = lv_tabview_add_tab(tabview, "Editor");
    lv_obj_t *tab_demo = lv_tabview_add_tab(tabview, "Demo");
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "System");
    tab4 = lv_tabview_add_tab(tabview, "Game");

    if (tab1 == NULL || tab2 == NULL || tab_editor == NULL || tab_demo == NULL || tab3 == NULL || tab4 == NULL) {
        printf("[main] ERROR: Failed to create one of the tabs!\r\n");
        return;
    }

    // ==========================================
    // Tab 1: Dashboard (Clock & Pomodoro)
    // ==========================================
    lv_obj_set_flex_flow(tab1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab1, 10, 0);

    // Large digital clock
    lbl_clock_time = lv_label_create(tab1);
    if (lbl_clock_time) {
        lv_label_set_text(lbl_clock_time, "20:51:49");
        lv_obj_set_style_text_font(lbl_clock_time, &lv_font_montserrat_24, 0);
        lv_obj_set_style_margin_bottom(lbl_clock_time, 5, 0);
    }

    // Date
    lbl_clock_date = lv_label_create(tab1);
    if (lbl_clock_date) {
        lv_label_set_text(lbl_clock_date, "Sunday, July 12, 2026");
        lv_obj_set_style_text_color(lbl_clock_date, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_margin_bottom(lbl_clock_date, 15, 0);
    }

    // Separator line
    lv_obj_t *sep = lv_obj_create(tab1);
    if (sep) {
        lv_obj_set_size(sep, 300, 2);
        lv_obj_set_style_bg_color(sep, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_margin_bottom(sep, 15, 0);
    }

    // Timer Mode Title
    lbl_timer_mode = lv_label_create(tab1);
    if (lbl_timer_mode) {
        lv_label_set_text(lbl_timer_mode, "POMODORO WORK TIMER");
        lv_obj_set_style_text_font(lbl_timer_mode, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_timer_mode, lv_palette_main(LV_PALETTE_AMBER), 0);
    }

    // Timer Value
    lbl_timer_val = lv_label_create(tab1);
    if (lbl_timer_val) {
        lv_label_set_text(lbl_timer_val, "25:00");
        lv_obj_set_style_text_font(lbl_timer_val, &lv_font_montserrat_24, 0);
        lv_obj_set_style_margin_bottom(lbl_timer_val, 10, 0);
    }

    // Timer Buttons Container
    lv_obj_t *btn_container = lv_obj_create(tab1);
    if (btn_container) {
        lv_obj_set_size(btn_container, 300, 50);
        lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_remove_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

        // Toggle Button
        btn_timer_toggle = lv_button_create(btn_container);
        if (btn_timer_toggle) {
            lv_obj_set_size(btn_timer_toggle, 100, 35);
            lv_obj_add_event_cb(btn_timer_toggle, timer_toggle_event_cb, LV_EVENT_ALL, NULL);
            lv_obj_t *lbl_toggle = lv_label_create(btn_timer_toggle);
            if (lbl_toggle) {
                lv_label_set_text(lbl_toggle, "Start");
                lv_obj_center(lbl_toggle);
            }
        }

        // Mode / Reset Button
        btn_timer_mode = lv_button_create(btn_container);
        if (btn_timer_mode) {
            lv_obj_set_size(btn_timer_mode, 120, 35);
            lv_obj_add_event_cb(btn_timer_mode, timer_mode_event_cb, LV_EVENT_ALL, NULL);
            lv_obj_t *lbl_mode = lv_label_create(btn_timer_mode);
            if (lbl_mode) {
                lv_label_set_text(lbl_mode, "Reset/Mode");
                lv_obj_center(lbl_mode);
            }
        }
    }

    // ==========================================
    // Tab 2: Controls (Settings & Audio)
    // ==========================================
    lv_obj_set_flex_flow(tab2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab2, 10, 0);

    lv_obj_t *ctrl_title = lv_label_create(tab2);
    if (ctrl_title) {
        lv_label_set_text(ctrl_title, "System Settings");
        lv_obj_set_style_text_font(ctrl_title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_margin_bottom(ctrl_title, 10, 0);
    }

    // Button to toggle theme
    btn_theme = lv_button_create(tab2);
    if (btn_theme) {
        lv_obj_set_size(btn_theme, 180, 35);
        lv_obj_add_event_cb(btn_theme, theme_btn_event_cb, LV_EVENT_ALL, NULL);

        lv_obj_t *btn_theme_label = lv_label_create(btn_theme);
        if (btn_theme_label) {
            lv_label_set_text(btn_theme_label, "Toggle Theme");
            lv_obj_center(btn_theme_label);
        }
    }

    // Button to cycle audio mode
    btn_audio_cycle = lv_button_create(tab2);
    if (btn_audio_cycle) {
        lv_obj_set_size(btn_audio_cycle, 220, 35);
        lv_obj_add_event_cb(btn_audio_cycle, audio_cycle_event_cb, LV_EVENT_ALL, NULL);
        lbl_audio_mode = lv_label_create(btn_audio_cycle);
        if (lbl_audio_mode) {
            lv_label_set_text(lbl_audio_mode, "Audio Mode: Tetris");
            lv_obj_center(lbl_audio_mode);
        }
    }

    // Dynamic Slider
    slider = lv_slider_create(tab2);
    if (slider) {
        lv_obj_set_size(slider, 200, 10);
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    }

    // Add interactive widgets to group for keypad navigation (initially Tab 0 widgets only)
    lv_group_t *g = lv_group_get_default();
    if (g) {
        lv_group_remove_all_objs(g);
        if (btn_timer_toggle) lv_group_add_obj(g, btn_timer_toggle);
        if (btn_timer_mode) lv_group_add_obj(g, btn_timer_mode);
    }

    // ==========================================
    // Tab: Editor (Text Editor)
    // ==========================================
    lv_obj_remove_flag(tab_editor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab_editor, 10, 0);

    text_editor = lv_textarea_create(tab_editor);
    if (text_editor) {
        lv_obj_set_size(text_editor, 620, 260);
        lv_obj_align(text_editor, LV_ALIGN_CENTER, 0, 0);
        lv_textarea_set_placeholder_text(text_editor, "Type in serial console to edit text...\nUse arrow keys & backspace!");
        lv_textarea_set_cursor_click_pos(text_editor, true);
        lv_obj_set_style_text_font(text_editor, &lv_font_montserrat_14, 0);
    }

    // ==========================================
    // Tab: Demo (Menu & Graphic Demos)
    // ==========================================
    lv_obj_remove_flag(tab_demo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab_demo, 10, 0);

    // Menu Container
    menu_container = lv_obj_create(tab_demo);
    if (menu_container) {
        lv_obj_set_size(menu_container, 620, 260);
        lv_obj_align(menu_container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_flex_flow(menu_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(menu_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(menu_container, 10, 0);
        
        lv_obj_t *menu_title = lv_label_create(menu_container);
        if (menu_title) {
            lv_label_set_text(menu_title, "Select Graphic Demo");
            lv_obj_set_style_text_font(menu_title, &lv_font_montserrat_18, 0);
            lv_obj_set_style_margin_bottom(menu_title, 15, 0);
        }
        
        // Water button
        btn_demo_water = lv_button_create(menu_container);
        if (btn_demo_water) {
            lv_obj_set_size(btn_demo_water, 240, 35);
            lv_obj_add_event_cb(btn_demo_water, demo_water_click_cb, LV_EVENT_ALL, NULL);
            lv_obj_set_style_margin_bottom(btn_demo_water, 10, 0);
            lv_obj_t *lbl = lv_label_create(btn_demo_water);
            if (lbl) {
                lv_label_set_text(lbl, "Water Simulation");
                lv_obj_center(lbl);
            }
        }
        
        // Pipes button
        btn_demo_pipes = lv_button_create(menu_container);
        if (btn_demo_pipes) {
            lv_obj_set_size(btn_demo_pipes, 240, 35);
            lv_obj_add_event_cb(btn_demo_pipes, demo_pipes_click_cb, LV_EVENT_ALL, NULL);
            lv_obj_t *lbl = lv_label_create(btn_demo_pipes);
            if (lbl) {
                lv_label_set_text(lbl, "3D Pipes Screensaver");
                lv_obj_center(lbl);
            }
        }
    }

    // Image Widget for Demos (initially hidden)
    img_water = lv_image_create(tab_demo);
    if (img_water) {
        lv_image_set_src(img_water, &water_img_dsc);
        lv_obj_set_size(img_water, 640, 280);
        lv_image_set_inner_align(img_water, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_align(img_water, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(img_water, LV_OBJ_FLAG_HIDDEN);
    }

    // ==========================================
    // Tab 3: System Specs
    // ==========================================
    lv_obj_set_flex_flow(tab3, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab3, 20, 0);

    lv_obj_t *sys_title = lv_label_create(tab3);
    if (sys_title) {
        lv_label_set_text(sys_title, "System Statistics");
        lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_margin_bottom(sys_title, 10, 0);
    }

    // Info Labels
    lv_obj_t *lbl_mcu = lv_label_create(tab3);
    if (lbl_mcu) {
        lv_label_set_text(lbl_mcu, "MCU: Raspberry Pi RP2350 @ 384 MHz");
    }

    lv_obj_t *lbl_signal = lv_label_create(tab3);
    if (lbl_signal) {
        lv_label_set_text(lbl_signal, "Output: 640x480 HDMI (640x360 Centered)");
    }

    lbl_stats_heap = lv_label_create(tab3);
    lbl_stats_resync = lv_label_create(tab3);
    lbl_stats_frame = lv_label_create(tab3);

    // ==========================================
    // Tab 4: Flappy Bird Game
    // ==========================================
    lv_obj_remove_flag(tab4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab4, 0, 0);

    // Game Area Container
    game_area = lv_obj_create(tab4);
    if (game_area) {
        lv_obj_set_size(game_area, GAME_WIDTH, GAME_HEIGHT);
        lv_obj_align(game_area, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(game_area, lv_color_hex(0x101010), 0); // Dark background
        lv_obj_set_style_bg_opa(game_area, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(game_area, 0, 0);
        lv_obj_set_style_pad_all(game_area, 0, 0);
        lv_obj_remove_flag(game_area, LV_OBJ_FLAG_SCROLLABLE);

        // Green pipes (top and bottom)
        pipe_top = lv_obj_create(game_area);
        if (pipe_top) {
            lv_obj_set_style_bg_color(pipe_top, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_bg_opa(pipe_top, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pipe_top, 0, 0);
            lv_obj_set_size(pipe_top, PIPE_WIDTH, 100);
            lv_obj_set_pos(pipe_top, (int)pipe_x, 0);
        }

        pipe_bottom = lv_obj_create(game_area);
        if (pipe_bottom) {
            lv_obj_set_style_bg_color(pipe_bottom, lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_set_style_bg_opa(pipe_bottom, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pipe_bottom, 0, 0);
            lv_obj_set_size(pipe_bottom, PIPE_WIDTH, 100);
            lv_obj_set_pos(pipe_bottom, (int)pipe_x, (int)(pipe_gap_y + GAP_SIZE / 2));
        }

        // Circular yellow bird
        bird_obj = lv_obj_create(game_area);
        if (bird_obj) {
            lv_obj_set_style_bg_color(bird_obj, lv_palette_main(LV_PALETTE_YELLOW), 0);
            lv_obj_set_style_bg_opa(bird_obj, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bird_obj, 0, 0);
            lv_obj_set_style_radius(bird_obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_size(bird_obj, BIRD_SIZE, BIRD_SIZE);
            lv_obj_set_pos(bird_obj, BIRD_X, (int)bird_y);
        }

        // Score display
        lbl_score = lv_label_create(game_area);
        if (lbl_score) {
            lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 10);
            lv_obj_set_style_text_font(lbl_score, &lv_font_montserrat_14, 0);
            lv_label_set_text(lbl_score, "Score: 0 | High: 0");
        }

        // Click / Startup overlay message
        lbl_status = lv_label_create(game_area);
        if (lbl_status) {
            lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(lbl_status, lv_palette_main(LV_PALETTE_AMBER), 0);
            lv_label_set_text(lbl_status, "TAP TO START / FLAP\n(Long Press to Exit)");
        }
    }
}

static uint8_t core0_stack[16384] __attribute__((aligned(16)));

void real_main(void);

int main(void) {
    // Switch Core 0 stack pointer to main SRAM buffer to prevent Scratch Y overflow
    __asm volatile (
        "msr msp, %0 \n"
        "isb \n"
        : : "r" (&core0_stack[16384]) : "memory"
    );
    real_main();
    while (1);
}

void real_main(void) {
    // LED Init for boot diagnostics
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    gpio_init(24);
    gpio_set_dir(24, GPIO_OUT);
    gpio_put(24, true);

    // Overclocking RP2350 core & flash to run at 384 MHz,
    // decoupling clk_hstx to USB PLL running at 126 MHz.
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(20);

    qmi_hw->m[0].timing = (qmi_hw->m[0].timing & ~0xFF) | 4;

    volatile uint32_t *flash_ptr = (volatile uint32_t *)0x10000000;
    (void)*flash_ptr;

    set_sys_clock_khz(384000, true);

    clock_configure(
        clk_usb,
        0, // clk_usb does not have a primary glitchless multiplexer
        CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        384 * MHZ,
        48 * MHZ);

    clock_configure(
        clk_adc,
        0,
        CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        384 * MHZ,
        48 * MHZ);

    pll_init(pll_usb, 1, 1008 * MHZ, 4, 2);
 
    clock_configure(
        clk_hstx,
        0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        126 * MHZ,
        126 * MHZ);

    sleep_ms(2500);
    stdio_init_all();
    printf("[main] stdio inited \r\n");

    // Initialize LVGL
    lv_init();

    // Register custom tick source
    lv_tick_set_cb(my_tick_get_cb);

    // Initialize Ports (HDMI Core 1 + Display buffer + Input Button)
    lv_port_disp_init();
    lv_port_indev_init();
    printf("[main] LVGL display and input drivers registered \r\n");
    sleep_ms(2500);
    
    // Build the user interface
    build_ui();
    printf("[main] UI widgets constructed \r\n");

    // Set initial default theme using default display pointer
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        lv_theme_t *th = lv_theme_default_init(
            disp,
            lv_palette_main(LV_PALETTE_DEEP_PURPLE),
            lv_palette_main(LV_PALETTE_AMBER),
            true, // default dark mode
            LV_FONT_DEFAULT
        );
        lv_display_set_theme(disp, th);
    }

    // Audio State Init
    init_sine_table();
    note_frames_remaining = current_melody[0].duration;
    phase_increment = (uint32_t)(((uint64_t)current_melody[0].freq << 32) / AUDIO_SAMPLE_RATE);
    generate_audio();

    // Main loop - animation + audio
    uint32_t last_frame = 0;
    bool led_state = false;
    uint32_t tick_counter = 0;
    bool last_btn_state = false;

    printf("[main] entering main execution loop \r\n");
    while (1) {
        // 1. Non-blocking USB Serial Input Polling & Routing
        static char rx_buf[256];
        static int rx_idx = 0;
        static int esc_state = 0; // 0=normal, 1=ESC, 2='['
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            uint16_t active_tab_idx = 0;
            if (tabview) {
                active_tab_idx = lv_tabview_get_tab_active(tabview);
            }

            if (active_tab_idx == 0) {
                // --- Dashboard Mode: JSON sync ---
                if (c == '\n' || c == '\r') {
                    if (rx_idx > 0) {
                        rx_buf[rx_idx] = '\0';
                        printf("[serial] Received: %s\r\n", rx_buf);
                        
                        int h = 0, m = 0, s = 0, d = 0, mo = 0, y = 0;
                        if (parse_time_json(rx_buf, &h, &m, &s, &d, &mo, &y)) {
                            clock_hours = h;
                            clock_minutes = m;
                            clock_seconds = s;
                            clock_day = d;
                            clock_month = mo;
                            clock_year = y;
                            printf("[serial] Time synced successfully to %02d:%02d:%02d!\r\n", h, m, s);
                            show_toast("Time Synced Successfully!");
                        } else {
                            if (rx_buf[0] == '{') {
                                printf("[serial] Invalid JSON format or bounds error.\r\n");
                                show_toast("Sync Failed: Invalid JSON!");
                            }
                        }
                        rx_idx = 0;
                    }
                } else if (c >= 32 && c < 127) {
                    if (rx_idx < sizeof(rx_buf) - 1) {
                        rx_buf[rx_idx++] = (char)c;
                    }
                }
            } else if (active_tab_idx == 2) {
                // --- Text Editor Mode: ANSI & Character Echo ---
                if (text_editor) {
                    if (esc_state == 0) {
                        if (c == 27) { // ESC
                            esc_state = 1;
                        } else if (c == 8) { // Backspace
                            lv_textarea_delete_char(text_editor);
                        } else if (c == 127) { // DEL (Forward Delete)
                            lv_textarea_delete_char_forward(text_editor);
                        } else if (c == '\r' || c == '\n') { // Enter
                            static uint32_t last_enter_time = 0;
                            uint32_t now = time_us_32() / 1000;
                            if (now - last_enter_time > 100) { // debounce for \r\n
                                lv_textarea_add_char(text_editor, '\n');
                                last_enter_time = now;
                            }
                        } else if (c >= 32 && c < 127) {
                            lv_textarea_add_char(text_editor, (char)c);
                        }
                    } else if (esc_state == 1) {
                        if (c == '[') {
                            esc_state = 2;
                        } else {
                            esc_state = 0;
                        }
                    } else if (esc_state == 2) {
                        if (c == 'A') { // Up arrow
                            lv_textarea_cursor_up(text_editor);
                            esc_state = 0;
                        } else if (c == 'B') { // Down arrow
                            lv_textarea_cursor_down(text_editor);
                            esc_state = 0;
                        } else if (c == 'C') { // Right arrow
                            lv_textarea_cursor_right(text_editor);
                            esc_state = 0;
                        } else if (c == 'D') { // Left arrow
                            lv_textarea_cursor_left(text_editor);
                            esc_state = 0;
                        } else if (c == '3') { // Delete key prefix (3)
                            esc_state = 3;
                        } else {
                            esc_state = 0;
                        }
                    } else if (esc_state == 3) {
                        if (c == '~') { // Delete key suffix (~)
                            lv_textarea_delete_char_forward(text_editor);
                        }
                        esc_state = 0;
                    }
                }
            }
            c = getchar_timeout_us(0);
        }

        // 2. Auto-hide Active Toast notifications
        if (toast_label && !(lv_obj_has_flag(toast_label, LV_OBJ_FLAG_HIDDEN))) {
            uint32_t now = time_us_32() / 1000;
            if (now >= toast_hide_time) {
                lv_obj_add_flag(toast_label, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Keep HDMI Audio Buffer filled
        generate_audio();

        // Increment LVGL ticks (handled by custom callback)
        lv_timer_handler();

        // Check for single-button gestures on GPIO 23:
        // - Long Press (held >= 800ms) -> Switch active tabs
        // - Double Click (< 350ms interval) -> Focus next widget in group
        // - Single Click (duration < 800ms, no double click) -> Send Enter (click currently focused widget)
        static uint32_t press_start_time = 0;
        static bool waiting_for_double_click = false;
        static uint32_t last_click_time = 0;
        static bool button_processed = false;

        bool btn_pressed = lv_port_indev_is_button_pressed();

        if (btn_pressed && !last_btn_state) {
            press_start_time = time_us_32() / 1000;
            button_processed = false;
        }

        uint16_t active_tab_idx = 0;
        if (tabview) {
            active_tab_idx = lv_tabview_get_tab_active(tabview);
        }

        if (btn_pressed && !button_processed) {
            uint32_t current_time = time_us_32() / 1000;
            if (current_time - press_start_time >= 800) {
                // Long press detected! Switch tabs (cycles through all 6 tabs)
                static int active_tab = 0;
                active_tab = (active_tab + 1) % 6; // Dashboard -> Controls -> Editor -> Demo -> System -> Game
                if (tabview) {
                    lv_tabview_set_active(tabview, active_tab, LV_ANIM_ON);
                }
                printf("[main] Long press detected! Switching to tab %d \r\n", active_tab);
                button_processed = true;
                waiting_for_double_click = false; // Cancel any pending clicks

                // Dynamically update default group objects depending on active tab
                lv_group_t *g_group = lv_group_get_default();
                if (g_group) {
                    lv_group_remove_all_objs(g_group);
                    if (active_tab == 0) { // Dashboard (Clock & Pomodoro)
                        if (btn_timer_toggle) lv_group_add_obj(g_group, btn_timer_toggle);
                        if (btn_timer_mode) lv_group_add_obj(g_group, btn_timer_mode);
                        if (btn_timer_toggle) lv_group_focus_obj(btn_timer_toggle);
                    } else if (active_tab == 1) { // Controls (Settings)
                        if (btn_theme) lv_group_add_obj(g_group, btn_theme);
                        if (btn_audio_cycle) lv_group_add_obj(g_group, btn_audio_cycle);
                        if (slider) lv_group_add_obj(g_group, slider);
                        if (btn_theme) lv_group_focus_obj(btn_theme);
                    } else if (active_tab == 2) { // Editor
                        if (text_editor) lv_group_add_obj(g_group, text_editor);
                        if (text_editor) lv_group_focus_obj(text_editor);
                    } else if (active_tab == 3) { // Demo
                        if (current_demo_state == DEMO_MENU) {
                            if (btn_demo_water) lv_group_add_obj(g_group, btn_demo_water);
                            if (btn_demo_pipes) lv_group_add_obj(g_group, btn_demo_pipes);
                            if (btn_demo_water) lv_group_focus_obj(btn_demo_water);
                        }
                    }
                }
            }
        }

        if (!btn_pressed && last_btn_state) {
            uint32_t release_time = time_us_32() / 1000;
            if (!button_processed) {
                uint32_t duration = release_time - press_start_time;
                if (duration < 800) {
                    if (active_tab_idx == 5) {
                        // Game Tab active -> Single click flaps instantly, no double-click delay needed!
                        if (!game_active) {
                            // Start/restart game
                            bird_y = 120.0f;
                            bird_velocity = 0.0f;
                            pipe_x = 640.0f;
                            pipe_gap_y = 100.0f + (rand() % 80); // random gap center
                            score = 0;
                            game_active = true;
                            game_over = false;
                            if (lbl_status) lv_obj_add_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
                            
                            printf("[game] Start/Restart! Game active = true, variables reset.\r\n");

                            char str_score[64];
                            snprintf(str_score, sizeof(str_score), "Score: %d | High: %d", score, high_score);
                            if (lbl_score) lv_label_set_text(lbl_score, str_score);
                        }
                        bird_velocity = FLAP_IMPULSE;
                        current_sfx = SFX_FLAP;
                        sfx_frame = 0;
                        printf("[game] Flapped! Velocity = %.1f\r\n", bird_velocity);
                    } else {
                        // Other tabs -> Standard double click / single click detection
                        if (release_time - last_click_time < 350) {
                            if (active_tab_idx == 3 && current_demo_state != DEMO_MENU) {
                                exit_demo_to_menu();
                            } else {
                                // Double click detected! Move focus to next widget
                                lv_group_focus_next(lv_group_get_default());
                                printf("[main] Double click! Focus moved to next widget.\r\n");
                            }
                            waiting_for_double_click = false;
                        } else {
                            waiting_for_double_click = true;
                        }
                    }
                    last_click_time = release_time;
                }
            }
        }

        if (waiting_for_double_click && active_tab_idx != 5) {
            uint32_t current_time = time_us_32() / 1000;
            if (current_time - last_click_time >= 350) {
                if (active_tab_idx == 3) {
                    if (current_demo_state == DEMO_WATER) {
                        trigger_water_splash();
                    } else if (current_demo_state == DEMO_PIPES) {
                        init_pipes_game();
                    } else {
                        // Demo menu: send event to focused list item button
                        lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                        if (focused) {
                            lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                        }
                    }
                } else {
                    // Single click detected! Send LV_EVENT_CLICKED directly to focused widget
                    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                    if (focused) {
                        lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                        printf("[main] Single click! Sent LV_EVENT_CLICKED to focused widget.\r\n");
                    }
                }
                waiting_for_double_click = false;
            }
        }

        last_btn_state = btn_pressed;

        // 50 Hz Game Physics and Water Simulation Tick
        static uint32_t last_game_update = 0;
        uint32_t now_ms = time_us_32() / 1000;
        if (now_ms - last_game_update >= 20) {
            last_game_update = now_ms;

            if (active_tab_idx == 3) {
                if (current_demo_state == DEMO_WATER) {
                    update_water_simulation();
                } else if (current_demo_state == DEMO_PIPES) {
                    update_pipes_simulation();
                }
            }

            if (game_active) {
                // Apply gravity & velocity
                bird_velocity += GRAVITY;
                if (bird_velocity > 8.0f) bird_velocity = 8.0f; // Terminal velocity
                bird_y += bird_velocity;

                // Scroll pipe
                pipe_x -= PIPE_SPEED;
                if (pipe_x < -PIPE_WIDTH) {
                    pipe_x = GAME_WIDTH;
                    pipe_gap_y = 60.0f + (rand() % 100); // Random gap center (60 to 160)
                    score++;
                    current_sfx = SFX_POINT;
                    sfx_frame = 0;
                    
                    printf("[game] Score! Current score = %d\r\n", score);
                    
                    char str_score[64];
                    if (score > high_score) {
                        high_score = score;
                    }
                    snprintf(str_score, sizeof(str_score), "Score: %d | High: %d", score, high_score);
                    if (lbl_score) lv_label_set_text(lbl_score, str_score);
                }

                // Collision Checks
                bool collided = false;
                
                // Ground/Ceiling check
                if (bird_y <= 0 || bird_y + BIRD_SIZE >= GAME_HEIGHT) {
                    collided = true;
                }
                
                // Pipe check
                if (BIRD_X + BIRD_SIZE >= pipe_x && BIRD_X <= pipe_x + PIPE_WIDTH) {
                    // Check if outside the gap
                    float gap_top = pipe_gap_y - GAP_SIZE / 2;
                    float gap_bottom = pipe_gap_y + GAP_SIZE / 2;
                    if (bird_y < gap_top || bird_y + BIRD_SIZE > gap_bottom) {
                        collided = true;
                    }
                }

                if (collided) {
                    // Game Over!
                    game_active = false;
                    game_over = true;
                    current_sfx = SFX_CRASH;
                    sfx_frame = 0;
                    
                    printf("[game] Collided! Game Over. Final Score = %d, High Score = %d\r\n", score, high_score);
                    
                    if (lbl_status) {
                        lv_label_set_text(lbl_status, "GAME OVER\nTAP TO RESTART\n(Long Press to Exit)");
                        lv_obj_remove_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
                    }
                }

                // Update widget layout
                if (bird_obj) lv_obj_set_pos(bird_obj, BIRD_X, (int)bird_y);
                
                if (pipe_top) {
                    int top_h = (int)(pipe_gap_y - GAP_SIZE / 2);
                    if (top_h < 0) top_h = 0;
                    lv_obj_set_size(pipe_top, PIPE_WIDTH, top_h);
                    lv_obj_set_pos(pipe_top, (int)pipe_x, 0);
                }
                
                if (pipe_bottom) {
                    int bot_y = (int)(pipe_gap_y + GAP_SIZE / 2);
                    int bot_h = GAME_HEIGHT - bot_y;
                    if (bot_h < 0) bot_h = 0;
                    lv_obj_set_size(pipe_bottom, PIPE_WIDTH, bot_h);
                    lv_obj_set_pos(pipe_bottom, (int)pipe_x, bot_y);
                }
            } else {
                // If not playing, let the bird hover gently in place
                static float hover_angle = 0.0f;
                hover_angle += 0.1f;
                if (!game_over) {
                    bird_y = 120.0f + sinf(hover_angle) * 5.0f;
                    if (bird_obj) lv_obj_set_pos(bird_obj, BIRD_X, (int)bird_y);
                }
            }
        }

        // 1 Hz Desktop Clock and Pomodoro Timer tick
        static uint32_t last_clock_update = 0;
        uint32_t now_ms_clock = time_us_32() / 1000;
        if (now_ms_clock - last_clock_update >= 1000) {
            last_clock_update = now_ms_clock;

            // 1. Advance Clock
            clock_seconds++;
            if (clock_seconds >= 60) {
                clock_seconds = 0;
                clock_minutes++;
                if (clock_minutes >= 60) {
                    clock_minutes = 0;
                    clock_hours++;
                    if (clock_hours >= 24) {
                        clock_hours = 0;
                        clock_day++;
                        if (clock_day > 30) {
                            clock_day = 1;
                            clock_month++;
                            if (clock_month > 12) {
                                clock_month = 1;
                                clock_year++;
                            }
                        }
                    }
                }
            }

            // Update clock labels on screen
            if (lbl_clock_time) {
                char str_time[32];
                snprintf(str_time, sizeof(str_time), "%02d:%02d:%02d", clock_hours, clock_minutes, clock_seconds);
                lv_label_set_text(lbl_clock_time, str_time);
            }

            if (lbl_clock_date) {
                char str_date[64];
                const char *dow = get_day_of_week(clock_day, clock_month, clock_year);
                snprintf(str_date, sizeof(str_date), "%s, %02d/%02d/%04d", dow, clock_day, clock_month, clock_year);
                lv_label_set_text(lbl_clock_date, str_date);
            }

            // 2. Advance Pomodoro / Stopwatch timer
            if (timer_running) {
                if (current_timer_mode == TIMER_MODE_POMODORO || current_timer_mode == TIMER_MODE_BREAK) {
                    timer_seconds--;
                    if (timer_seconds <= 0) {
                        timer_seconds = 0;
                        timer_running = false;
                        
                        // Play alert sound (point SFX)
                        current_sfx = SFX_POINT;
                        sfx_frame = 0;
                        
                        if (btn_timer_toggle) {
                            lv_obj_t *lbl = lv_obj_get_child(btn_timer_toggle, 0);
                            if (lbl) lv_label_set_text(lbl, "Start");
                        }
                        printf("[timer] Time's up!\r\n");
                    }
                } else if (current_timer_mode == TIMER_MODE_STOPWATCH) {
                    timer_seconds++;
                }

                // Update timer label on screen
                if (lbl_timer_val) {
                    char str_timer[32];
                    snprintf(str_timer, sizeof(str_timer), "%02d:%02d", timer_seconds / 60, timer_seconds % 60);
                    lv_label_set_text(lbl_timer_val, str_timer);
                }
            }
        }

        // Perform frame-level melody updates
        if (video_frame_count != last_frame) {
            last_frame = video_frame_count;
            advance_melody();

            // Toggle LED every 30 video frames
            if ((video_frame_count % 30) == 0) {
                led_state = !led_state;
                gpio_put(PICO_DEFAULT_LED_PIN, led_state);
                gpio_put(24, led_state);
            }
        }

        // Slowly animate UI widgets
        tick_counter++;
        if ((tick_counter % 5000) == 0) {
            printf("[main] loop heartbeat: ticks=%u frames=%u\r\n", (unsigned int)tick_counter, (unsigned int)video_frame_count);
        }

        if ((tick_counter % 32) == 0) {
            // Update stats
            if (lbl_stats_heap) {
                lv_mem_monitor_t mon;
                lv_mem_monitor(&mon);
                char str_heap[64];
                snprintf(str_heap, sizeof(str_heap), "LVGL Heap Free: %u KB", (unsigned int)(mon.free_size / 1024));
                lv_label_set_text(lbl_stats_heap, str_heap);
            }

            if (lbl_stats_resync) {
                char str_resync[64];
                extern volatile uint32_t video_output_resync_count;
                snprintf(str_resync, sizeof(str_resync), "HDMI Resync Count: %u", (unsigned int)video_output_resync_count);
                lv_label_set_text(lbl_stats_resync, str_resync);
            }

            if (lbl_stats_frame) {
                char str_frame[64];
                snprintf(str_frame, sizeof(str_frame), "Video Frames Rendered: %u", (unsigned int)video_frame_count);
                lv_label_set_text(lbl_stats_frame, str_frame);
            }
        }

        sleep_ms(1);
    }


}
