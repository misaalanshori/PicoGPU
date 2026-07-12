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

static inline int16_t get_sine_sample(void) {
    uint32_t sfx_phase_inc = 0;
    
    if (current_sfx != SFX_NONE) {
        uint32_t sfx_freq = 0;
        if (current_sfx == SFX_FLAP) {
            // Ascending flap sweep
            sfx_freq = 200 + sfx_frame * 80; // 200 to 600 Hz
            sfx_frame++;
            if (sfx_frame > 5) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_POINT) {
            // High point chime
            sfx_freq = (sfx_frame < 5) ? 880 : 1046;
            sfx_frame++;
            if (sfx_frame > 10) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_CRASH) {
            // Descending buzz
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

    // Fallback to background melody if no SFX is active
    if (phase_increment == 0) return 0;
    int16_t s = sine_table[(audio_phase >> 24) & 0xFF];
    audio_phase += phase_increment;
    return s;
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
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, "System");
    tab4 = lv_tabview_add_tab(tabview, "Game");

    if (tab1 == NULL || tab2 == NULL || tab3 == NULL || tab4 == NULL) {
        printf("[main] ERROR: Failed to create one of the tabs!\r\n");
        return;
    }

    // ==========================================
    // Tab 1: Dashboard
    // ==========================================
    lv_obj_set_flex_flow(tab1, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab1, 10, 0);

    lv_obj_t *title = lv_label_create(tab1);
    if (title) {
        lv_label_set_text(title, "PicoHDMI + LVGL 9.5");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    }

    lv_obj_t *desc = lv_label_create(tab1);
    if (desc) {
        lv_label_set_text(desc, "Press GPIO 23 button to switch tabs!");
        lv_obj_set_style_text_color(desc, lv_palette_main(LV_PALETTE_GREY), 0);
    }

    // Progress Bar showing animated progress
    progress_bar = lv_bar_create(tab1);
    if (progress_bar) {
        lv_obj_set_size(progress_bar, 280, 15);
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    }

    // Animated Arc
    cpu_arc = lv_arc_create(tab1);
    if (cpu_arc) {
        lv_obj_set_size(cpu_arc, 80, 80);
        lv_arc_set_rotation(cpu_arc, 135);
        lv_arc_set_bg_angles(cpu_arc, 0, 270);
        lv_arc_set_value(cpu_arc, 10);
    }

    // ==========================================
    // Tab 2: Controls
    // ==========================================
    lv_obj_set_flex_flow(tab2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(tab2, 10, 0);

    lv_obj_t *ctrl_title = lv_label_create(tab2);
    if (ctrl_title) {
        lv_label_set_text(ctrl_title, "Interactive Control");
        lv_obj_set_style_text_font(ctrl_title, &lv_font_montserrat_18, 0);
    }

    // Button to toggle theme
    lv_obj_t *btn_theme = lv_button_create(tab2);
    if (btn_theme) {
        lv_obj_set_size(btn_theme, 180, 40);
        lv_obj_add_event_cb(btn_theme, theme_btn_event_cb, LV_EVENT_ALL, NULL);

        lv_obj_t *btn_theme_label = lv_label_create(btn_theme);
        if (btn_theme_label) {
            lv_label_set_text(btn_theme_label, "Toggle Theme");
            lv_obj_center(btn_theme_label);
        }
    }

    // Dynamic Slider
    lv_obj_t *slider = lv_slider_create(tab2);
    if (slider) {
        lv_obj_set_size(slider, 200, 10);
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    }

    // Add interactive widgets to group for keypad navigation
    lv_group_t *g = lv_group_get_default();
    if (g) {
        if (btn_theme) lv_group_add_obj(g, btn_theme);
        if (slider) lv_group_add_obj(g, slider);
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
        lv_label_set_text(lbl_mcu, "MCU: Raspberry Pi RP2350 @ 252 MHz");
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

    // Set system clock to 252 MHz (safe, nominal timing for 640x480p60 HDMI)
    // No vreg voltage elevation or QMI flash overrides required.
    set_sys_clock_khz(252000, true);

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
                // Long press detected! Switch tabs (cycles through all 4 tabs)
                static int active_tab = 0;
                active_tab = (active_tab + 1) % 4; // Dashboard -> Controls -> System -> Game
                if (tabview) {
                    lv_tabview_set_active(tabview, active_tab, LV_ANIM_ON);
                }
                printf("[main] Long press detected! Switching to tab %d \r\n", active_tab);
                button_processed = true;
                waiting_for_double_click = false; // Cancel any pending clicks
            }
        }

        if (!btn_pressed && last_btn_state) {
            uint32_t release_time = time_us_32() / 1000;
            if (!button_processed) {
                uint32_t duration = release_time - press_start_time;
                if (duration < 800) {
                    if (active_tab_idx == 3) {
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
                            // Double click detected! Move focus to next widget
                            lv_group_focus_next(lv_group_get_default());
                            printf("[main] Double click! Focus moved to next widget.\r\n");
                            waiting_for_double_click = false;
                        } else {
                            waiting_for_double_click = true;
                        }
                    }
                    last_click_time = release_time;
                }
            }
        }

        if (waiting_for_double_click && active_tab_idx != 3) {
            uint32_t current_time = time_us_32() / 1000;
            if (current_time - last_click_time >= 350) {
                // Single click detected! Send LV_EVENT_CLICKED directly to focused widget
                lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                if (focused) {
                    lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                    printf("[main] Single click! Sent LV_EVENT_CLICKED to focused widget.\r\n");
                }
                waiting_for_double_click = false;
            }
        }

        last_btn_state = btn_pressed;

        // 50 Hz Flappy Bird Game Physics Tick
        static uint32_t last_game_update = 0;
        uint32_t now_ms = time_us_32() / 1000;
        if (now_ms - last_game_update >= 20) {
            last_game_update = now_ms;

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
        if ((tick_counter % 17) == 0) {
            // Animate Progress Bar
            if (progress_bar) {
                static int progress_val = 0;
                progress_val = (progress_val + 1) % 101;
                lv_bar_set_value(progress_bar, progress_val, LV_ANIM_ON);
            }

            // Animate Arc
            if (cpu_arc) {
                static int arc_val = 10;
                static int arc_dir = 1;
                arc_val += arc_dir * 2;
                if (arc_val >= 90) { arc_val = 90; arc_dir = -1; }
                if (arc_val <= 10) { arc_val = 10; arc_dir = 1; }
                lv_arc_set_value(cpu_arc, arc_val);
            }
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

    return 0;
}
