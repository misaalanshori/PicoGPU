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

static inline int16_t get_sine_sample(void) {
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

    if (tab1 == NULL || tab2 == NULL || tab3 == NULL) {
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
        lv_label_set_text(lbl_mcu, "MCU: Raspberry Pi RP2350 @ 372 MHz");
    }

    lv_obj_t *lbl_signal = lv_label_create(tab3);
    if (lbl_signal) {
        lv_label_set_text(lbl_signal, "Output: 1280x720 HDMI (640x360 Scaled 2x)");
    }

    lbl_stats_heap = lv_label_create(tab3);
    lbl_stats_resync = lv_label_create(tab3);
    lbl_stats_frame = lv_label_create(tab3);
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

        // Check for button press on GPIO 23 to switch tabs
        bool btn_pressed = lv_port_indev_is_button_pressed();
        if (btn_pressed && !last_btn_state) {
            static int active_tab = 0;
            active_tab = (active_tab + 1) % 3;
            if (tabview) {
                lv_tabview_set_active(tabview, active_tab, LV_ANIM_ON);
            }
            printf("[main] GPIO 23 button press detected! Switching to tab %d \r\n", active_tab);
        }
        last_btn_state = btn_pressed;

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
