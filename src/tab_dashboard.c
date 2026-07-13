#include "shared_state.h"
#include "tab_dashboard.h"
#include <stdio.h>

void dashboard_process_char(char c) {
    static char rx_buf[256];
    static int rx_idx = 0;
    
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
    } else {
        if (rx_idx < sizeof(rx_buf) - 1) {
            rx_buf[rx_idx++] = c;
        }
    }
}

void dashboard_tick_1hz(void) {
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

    // Update clock labels
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
                
                // Play alert sound
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

        // Update timer label
        if (lbl_timer_val) {
            char str_timer[32];
            snprintf(str_timer, sizeof(str_timer), "%02d:%02d", timer_seconds / 60, timer_seconds % 60);
            lv_label_set_text(lbl_timer_val, str_timer);
        }
    }
}
