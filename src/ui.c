#include "shared_state.h"
#include "pico/stdlib.h"
#include "demo_water.h"
#include "demo_pipes.h"
#include "demo_3d.h"
#include "demo_raytracer.h"
#include <stdio.h>
#include <string.h>

// UI Global Allocations
lv_obj_t *tabview = NULL;
lv_obj_t *tab4 = NULL;
lv_obj_t *game_area = NULL;
lv_obj_t *bird_obj = NULL;
lv_obj_t *pipe_top = NULL;
lv_obj_t *pipe_bottom = NULL;
lv_obj_t *lbl_score = NULL;
lv_obj_t *lbl_status = NULL;

lv_obj_t *lbl_clock_time = NULL;
lv_obj_t *lbl_clock_date = NULL;
lv_obj_t *lbl_timer_mode = NULL;
lv_obj_t *lbl_timer_val = NULL;
lv_obj_t *btn_timer_toggle = NULL;
lv_obj_t *btn_timer_mode = NULL;

lv_obj_t *btn_theme = NULL;
lv_obj_t *btn_audio_cycle = NULL;
lv_obj_t *slider = NULL;
lv_obj_t *text_editor = NULL;

lv_obj_t *menu_container = NULL;
lv_obj_t *btn_demo_water = NULL;
lv_obj_t *btn_demo_pipes = NULL;
lv_obj_t *btn_demo_3d = NULL;
lv_obj_t *btn_demo_raytrace = NULL;
lv_obj_t *img_water = NULL;
lv_obj_t *toast_label = NULL;

timer_mode_t current_timer_mode = TIMER_MODE_POMODORO;
bool timer_running = false;
int timer_seconds = 25 * 60;
uint32_t toast_hide_time = 0;

demo_state_t current_demo_state = DEMO_MENU;

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

// Menu Click Callback - 3D Landscape
static void demo_3d_click_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_demo_state = DEMO_3D;
        if (menu_container) lv_obj_add_flag(menu_container, LV_OBJ_FLAG_HIDDEN);
        if (img_water) lv_obj_remove_flag(img_water, LV_OBJ_FLAG_HIDDEN);
        
        lv_group_t *g = lv_group_get_default();
        if (g) lv_group_remove_all_objs(g);
        
        memset(water_pixel_buf, 0, sizeof(water_pixel_buf));
        
        current_sfx = SFX_POINT;
        sfx_frame = 0;
        printf("[demo] Switched to 3D Voxel Landscape\r\n");
    }
}

// Menu Click Callback - 3D Raytracer
static void demo_raytrace_click_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        current_demo_state = DEMO_RAYTRACE;
        if (menu_container) lv_obj_add_flag(menu_container, LV_OBJ_FLAG_HIDDEN);
        if (img_water) lv_obj_remove_flag(img_water, LV_OBJ_FLAG_HIDDEN);
        
        lv_group_t *g = lv_group_get_default();
        if (g) lv_group_remove_all_objs(g);
        
        memset(water_pixel_buf, 0, sizeof(water_pixel_buf));
        
        current_sfx = SFX_POINT;
        sfx_frame = 0;
        printf("[demo] Switched to 3D Raytracer\r\n");
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
        if (btn_demo_3d) lv_group_add_obj(g, btn_demo_3d);
        if (btn_demo_raytrace) lv_group_add_obj(g, btn_demo_raytrace);
        if (btn_demo_water) lv_group_focus_obj(btn_demo_water);
    }
    
    current_sfx = SFX_POINT;
    sfx_frame = 0;
    printf("[demo] Exited to Demo Menu\r\n");
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
    char json[256];
    int dest_idx = 0;
    for (int i = 0; json_raw[i] != '\0' && dest_idx < 255; i++) {
        char c = json_raw[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            json[dest_idx++] = c;
        }
    }
    json[dest_idx] = '\0';

    const char *time_ptr = strstr(json, "\"time\":\"");
    if (!time_ptr) {
        time_ptr = strstr(json, "'time':'");
    }
    if (!time_ptr) return false;
    time_ptr += 8;

    int th = 0, tm = 0, ts = 0;
    if (sscanf(time_ptr, "%d:%d:%d", &th, &tm, &ts) != 3) return false;
    if (th < 0 || th > 23 || tm < 0 || tm > 59 || ts < 0 || ts > 59) return false;

    const char *date_ptr = strstr(json, "\"date\":\"");
    if (!date_ptr) {
        date_ptr = strstr(json, "'date':'");
    }
    if (!date_ptr) return false;
    date_ptr += 8;

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

// UI Builder Function
void build_ui(void) {
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

    lbl_clock_time = lv_label_create(tab1);
    if (lbl_clock_time) {
        lv_label_set_text(lbl_clock_time, "20:51:49");
        lv_obj_set_style_text_font(lbl_clock_time, &lv_font_montserrat_24, 0);
        lv_obj_set_style_margin_bottom(lbl_clock_time, 5, 0);
    }

    lbl_clock_date = lv_label_create(tab1);
    if (lbl_clock_date) {
        lv_label_set_text(lbl_clock_date, "Sunday, July 12, 2026");
        lv_obj_set_style_text_color(lbl_clock_date, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_margin_bottom(lbl_clock_date, 15, 0);
    }

    lv_obj_t *sep = lv_obj_create(tab1);
    if (sep) {
        lv_obj_set_size(sep, 300, 2);
        lv_obj_set_style_bg_color(sep, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
        lv_obj_set_style_border_width(sep, 0, 0);
        lv_obj_set_style_margin_bottom(sep, 15, 0);
    }

    lbl_timer_mode = lv_label_create(tab1);
    if (lbl_timer_mode) {
        lv_label_set_text(lbl_timer_mode, "POMODORO WORK TIMER");
        lv_obj_set_style_text_font(lbl_timer_mode, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_timer_mode, lv_palette_main(LV_PALETTE_AMBER), 0);
    }

    lbl_timer_val = lv_label_create(tab1);
    if (lbl_timer_val) {
        lv_label_set_text(lbl_timer_val, "25:00");
        lv_obj_set_style_text_font(lbl_timer_val, &lv_font_montserrat_24, 0);
        lv_obj_set_style_margin_bottom(lbl_timer_val, 10, 0);
    }

    lv_obj_t *btn_container = lv_obj_create(tab1);
    if (btn_container) {
        lv_obj_set_size(btn_container, 300, 50);
        lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_remove_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

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

    slider = lv_slider_create(tab2);
    if (slider) {
        lv_obj_set_size(slider, 200, 10);
        lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    }

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
        
        btn_demo_pipes = lv_button_create(menu_container);
        if (btn_demo_pipes) {
            lv_obj_set_size(btn_demo_pipes, 240, 35);
            lv_obj_add_event_cb(btn_demo_pipes, demo_pipes_click_cb, LV_EVENT_ALL, NULL);
            lv_obj_set_style_margin_bottom(btn_demo_pipes, 10, 0);
            lv_obj_t *lbl = lv_label_create(btn_demo_pipes);
            if (lbl) {
                lv_label_set_text(lbl, "3D Pipes Screensaver");
                lv_obj_center(lbl);
            }
        }
        
        btn_demo_3d = lv_button_create(menu_container);
        if (btn_demo_3d) {
            lv_obj_set_size(btn_demo_3d, 240, 35);
            lv_obj_add_event_cb(btn_demo_3d, demo_3d_click_cb, LV_EVENT_ALL, NULL);
            lv_obj_set_style_margin_bottom(btn_demo_3d, 10, 0);
            lv_obj_t *lbl = lv_label_create(btn_demo_3d);
            if (lbl) {
                lv_label_set_text(lbl, "3D Voxel Landscape");
                lv_obj_center(lbl);
            }
        }
        
        btn_demo_raytrace = lv_button_create(menu_container);
        if (btn_demo_raytrace) {
            lv_obj_set_size(btn_demo_raytrace, 240, 35);
            lv_obj_add_event_cb(btn_demo_raytrace, demo_raytrace_click_cb, LV_EVENT_ALL, NULL);
            lv_obj_t *lbl = lv_label_create(btn_demo_raytrace);
            if (lbl) {
                lv_label_set_text(lbl, "3D Raytracer");
                lv_obj_center(lbl);
            }
        }
    }

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

    lv_obj_t *lbl_mcu = lv_label_create(tab3);
    if (lbl_mcu) {
        lv_label_set_text(lbl_mcu, "MCU: Raspberry Pi RP2350 @ 384 MHz");
    }

    lv_obj_t *lbl_signal = lv_label_create(tab3);
    if (lbl_signal) {
        lv_label_set_text(lbl_signal, "Output: 640x480 HDMI (640x360 Centered)");
    }

    extern lv_obj_t *lbl_stats_heap;
    extern lv_obj_t *lbl_stats_resync;
    extern lv_obj_t *lbl_stats_frame;
    lbl_stats_heap = lv_label_create(tab3);
    lbl_stats_resync = lv_label_create(tab3);
    lbl_stats_frame = lv_label_create(tab3);

    // ==========================================
    // Tab 4: Flappy Bird Game
    // ==========================================
    lv_obj_remove_flag(tab4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab4, 0, 0);

    game_area = lv_obj_create(tab4);
    if (game_area) {
        lv_obj_set_size(game_area, GAME_WIDTH, GAME_HEIGHT);
        lv_obj_align(game_area, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(game_area, lv_color_hex(0x101010), 0);
        lv_obj_set_style_bg_opa(game_area, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(game_area, 0, 0);
        lv_obj_set_style_pad_all(game_area, 0, 0);
        lv_obj_remove_flag(game_area, LV_OBJ_FLAG_SCROLLABLE);

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

        bird_obj = lv_obj_create(game_area);
        if (bird_obj) {
            lv_obj_set_style_bg_color(bird_obj, lv_palette_main(LV_PALETTE_YELLOW), 0);
            lv_obj_set_style_bg_opa(bird_obj, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bird_obj, 0, 0);
            lv_obj_set_style_radius(bird_obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_size(bird_obj, BIRD_SIZE, BIRD_SIZE);
            lv_obj_set_pos(bird_obj, BIRD_X, (int)bird_y);
        }

        lbl_score = lv_label_create(game_area);
        if (lbl_score) {
            lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 10);
            lv_obj_set_style_text_font(lbl_score, &lv_font_montserrat_14, 0);
            lv_label_set_text(lbl_score, "Score: 0 | High: 0");
        }

        lbl_status = lv_label_create(game_area);
        if (lbl_status) {
            lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(lbl_status, lv_palette_main(LV_PALETTE_AMBER), 0);
            lv_label_set_text(lbl_status, "TAP TO START / FLAP\n(Long Press to Exit)");
        }
    }
}
