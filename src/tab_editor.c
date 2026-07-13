#include "shared_state.h"
#include "tab_editor.h"
#include "pico/stdlib.h"

void editor_process_char(char c) {
    if (!text_editor) return;
    
    static int esc_state = 0; // 0=normal, 1=ESC, 2='['
    
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
            lv_textarea_add_char(text_editor, c);
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
