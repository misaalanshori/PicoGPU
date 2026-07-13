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

#include "shared_state.h"
#include "audio.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "audio_impl.h"
#include "game_flappy.h"
#include "demo_water.h"
#include "demo_pipes.h"
#include "demo_3d.h"
#include "demo_raytracer.h"
#include "tab_dashboard.h"
#include "tab_editor.h"
#include "ui.h"

// System Clock Global Definitions
int clock_hours = 20;
int clock_minutes = 51;
int clock_seconds = 49;
int clock_day = 12;
int clock_month = 7;
int clock_year = 2026;

// System Statistics Labels
lv_obj_t *lbl_stats_heap = NULL;
lv_obj_t *lbl_stats_resync = NULL;
lv_obj_t *lbl_stats_frame = NULL;

// Fast LCG random seed and function
static uint32_t audio_rand_seed = 0x12345678;
uint32_t fast_rand(void) {
    audio_rand_seed = audio_rand_seed * 1103515245 + 12345;
    return audio_rand_seed;
}

// Tick callback helper
static uint32_t my_tick_get_cb(void) {
    return time_us_32() / 1000;
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
    extern const note_t *current_melody;
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
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            uint16_t active_tab_idx = 0;
            if (tabview) {
                active_tab_idx = lv_tabview_get_tab_active(tabview);
            }

            if (active_tab_idx == 0) {
                dashboard_process_char((char)c);
            } else if (active_tab_idx == 2) {
                editor_process_char((char)c);
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

        // Increment LVGL ticks
        lv_timer_handler();

        // Check for single-button gestures on GPIO 23:
        // - Long Press (held >= 800ms) -> Switch active tabs
        // - Double Click (< 350ms interval) -> Focus next widget in group
        // - Single Click -> Send Enter/Click to focused widget
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
                // Long press detected! Cycle active tabs
                static int active_tab = 0;
                active_tab = (active_tab + 1) % 6;
                if (tabview) {
                    lv_tabview_set_active(tabview, active_tab, LV_ANIM_ON);
                }
                printf("[main] Long press detected! Switching to tab %d \r\n", active_tab);
                button_processed = true;
                waiting_for_double_click = false;

                // Update default group objects
                lv_group_t *g_group = lv_group_get_default();
                if (g_group) {
                    lv_group_remove_all_objs(g_group);
                    if (active_tab == 0) {
                        if (btn_timer_toggle) lv_group_add_obj(g_group, btn_timer_toggle);
                        if (btn_timer_mode) lv_group_add_obj(g_group, btn_timer_mode);
                        if (btn_timer_toggle) lv_group_focus_obj(btn_timer_toggle);
                    } else if (active_tab == 1) {
                        if (btn_theme) lv_group_add_obj(g_group, btn_theme);
                        if (btn_audio_cycle) lv_group_add_obj(g_group, btn_audio_cycle);
                        if (slider) lv_group_add_obj(g_group, slider);
                        if (btn_theme) lv_group_focus_obj(btn_theme);
                    } else if (active_tab == 2) {
                        if (text_editor) lv_group_add_obj(g_group, text_editor);
                        if (text_editor) lv_group_focus_obj(text_editor);
                    } else if (active_tab == 3) {
                        if (current_demo_state == DEMO_MENU) {
                            if (btn_demo_water) lv_group_add_obj(g_group, btn_demo_water);
                            if (btn_demo_pipes) lv_group_add_obj(g_group, btn_demo_pipes);
                            if (btn_demo_3d) lv_group_add_obj(g_group, btn_demo_3d);
                            if (btn_demo_raytrace) lv_group_add_obj(g_group, btn_demo_raytrace);
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
                        // Game Tab active -> Single click flaps/starts game instantly
                        flap_bird();
                    } else {
                        // Other tabs -> Standard double click / single click detection
                        if (release_time - last_click_time < 350) {
                            if (active_tab_idx == 3 && current_demo_state != DEMO_MENU) {
                                exit_demo_to_menu();
                            } else {
                                // Double click detected! Move focus
                                lv_group_focus_next(lv_group_get_default());
                                printf("[main] Double click! Focus moved.\r\n");
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
                    } else if (current_demo_state == DEMO_3D) {
                        current_sfx = SFX_POINT;
                        sfx_frame = 0;
                        printf("[demo] 3D Landscape click beep!\r\n");
                    } else if (current_demo_state == DEMO_RAYTRACE) {
                        current_sfx = SFX_POINT;
                        sfx_frame = 0;
                        printf("[demo] Raytracer click beep!\r\n");
                    } else {
                        // Demo menu: click focused button
                        lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                        if (focused) {
                            lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                        }
                    }
                } else {
                    // Single click: click focused widget
                    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                    if (focused) {
                        lv_obj_send_event(focused, LV_EVENT_CLICKED, NULL);
                        printf("[main] Single click! Sent click to widget.\r\n");
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
                } else if (current_demo_state == DEMO_3D) {
                    update_terrain_simulation();
                } else if (current_demo_state == DEMO_RAYTRACE) {
                    update_raytracer_simulation();
                }
            }

            if (active_tab_idx == 5) {
                update_game_physics();
            }
        }

        // 1 Hz Desktop Clock and Pomodoro Timer tick
        static uint32_t last_clock_update = 0;
        uint32_t now_ms_clock = time_us_32() / 1000;
        if (now_ms_clock - last_clock_update >= 1000) {
            last_clock_update = now_ms_clock;
            dashboard_tick_1hz();
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

        // Heartbeat & memory stats
        tick_counter++;
        if ((tick_counter % 5000) == 0) {
            printf("[main] loop heartbeat: ticks=%u frames=%u\r\n", (unsigned int)tick_counter, (unsigned int)video_frame_count);
        }

        if ((tick_counter % 32) == 0) {
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
