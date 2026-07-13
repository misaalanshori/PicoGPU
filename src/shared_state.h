#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Configuration Macros
// ============================================================================
#define AUDIO_SAMPLE_RATE 48000
#define TONE_AMPLITUDE 6000
#define SINE_TABLE_SIZE 256

#define WATER_W 128
#define WATER_H 72

#define BIRD_X 100
#define BIRD_SIZE 16
#define PIPE_WIDTH 50
#define GAP_SIZE 100
#define GAME_HEIGHT 280
#define GAME_WIDTH 640
#define GRAVITY 0.35f
#define FLAP_IMPULSE -5.5f
#define PIPE_SPEED 3.0f

// ============================================================================
// Shared Enums
// ============================================================================
typedef enum {
    SFX_NONE,
    SFX_FLAP,
    SFX_POINT,
    SFX_CRASH
} sfx_type_t;

typedef enum {
    AUDIO_TETRIS,
    AUDIO_RAIN,
    AUDIO_NOISE,
    AUDIO_SILENT
} audio_mode_t;

typedef enum {
    TIMER_MODE_POMODORO,
    TIMER_MODE_BREAK,
    TIMER_MODE_STOPWATCH
} timer_mode_t;

typedef enum {
    DEMO_MENU,
    DEMO_WATER,
    DEMO_PIPES,
    DEMO_3D,
    DEMO_RAYTRACE
} demo_state_t;

// ============================================================================
// Shared Global Variables (Externs)
// ============================================================================

// UI Tabs and Widgets
extern lv_obj_t *tabview;
extern lv_obj_t *tab4;
extern lv_obj_t *game_area;
extern lv_obj_t *bird_obj;
extern lv_obj_t *pipe_top;
extern lv_obj_t *pipe_bottom;
extern lv_obj_t *lbl_score;
extern lv_obj_t *lbl_status;

extern lv_obj_t *lbl_clock_time;
extern lv_obj_t *lbl_clock_date;
extern lv_obj_t *lbl_timer_mode;
extern lv_obj_t *lbl_timer_val;
extern lv_obj_t *btn_timer_toggle;
extern lv_obj_t *btn_timer_mode;

extern lv_obj_t *btn_theme;
extern lv_obj_t *btn_audio_cycle;
extern lv_obj_t *slider;
extern lv_obj_t *text_editor;

extern lv_obj_t *menu_container;
extern lv_obj_t *btn_demo_water;
extern lv_obj_t *btn_demo_pipes;
extern lv_obj_t *btn_demo_3d;
extern lv_obj_t *btn_demo_raytrace;
extern lv_obj_t *img_water;
extern lv_obj_t *toast_label;

// System Clock
extern int clock_hours;
extern int clock_minutes;
extern int clock_seconds;
extern int clock_day;
extern int clock_month;
extern int clock_year;

// Timer and Toast
extern timer_mode_t current_timer_mode;
extern bool timer_running;
extern int timer_seconds;
extern uint32_t toast_hide_time;

// Demo / Simulation Variables
extern demo_state_t current_demo_state;
extern int16_t water_buf1[WATER_W * WATER_H];
extern int16_t water_buf2[WATER_W * WATER_H];
extern int16_t *water_prev;
extern int16_t *water_next;
extern uint8_t water_pixel_buf[WATER_W * WATER_H * 2];
extern const lv_image_dsc_t water_img_dsc;

extern bool pipes_occupied[16 * 9];
extern int ps_pipe_x;
extern int ps_pipe_y;
extern int ps_pipe_dir;
extern uint16_t ps_pipe_base_color;
extern int ps_pipe_length;
extern bool ps_pipe_active;

extern float rt_light_angle;

// Flappy Bird Variables
extern bool game_active;
extern bool game_over;
extern float bird_y;
extern float bird_velocity;
extern float pipe_x;
extern float pipe_gap_y;
extern int score;
extern int high_score;

// Audio Synth & SFX Variables
extern int16_t sine_table[SINE_TABLE_SIZE];
extern uint32_t audio_phase;
extern uint32_t phase_increment;
extern int audio_frame_counter;
extern int melody_index;
extern int note_frames_remaining;
extern volatile sfx_type_t current_sfx;
extern volatile int sfx_frame;
extern volatile audio_mode_t current_audio_mode;

// ============================================================================
// Helper Declarations
// ============================================================================
const char* get_day_of_week(int day, int month, int year);
bool parse_time_json(const char *json_raw, int *h, int *m, int *s, int *d, int *mo, int *y);
void show_toast(const char *message);
void exit_demo_to_menu(void);
uint32_t fast_rand(void);
static inline uint16_t shade_color_rgb565(uint16_t color, uint32_t scale_256) {
    uint32_t r = (color >> 11) & 0x1F;
    uint32_t g = (color >> 5) & 0x3F;
    uint32_t b = color & 0x1F;
    
    r = (r * scale_256) >> 8;
    g = (g * scale_256) >> 8;
    b = (b * scale_256) >> 8;
    
    return (uint16_t)((r << 11) | (g << 5) | b);
}

#endif // SHARED_STATE_H
