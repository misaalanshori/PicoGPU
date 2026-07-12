#include "lv_port_indev.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define BUTTON_PIN 23

static int press_count = 0;
static int release_count = 0;
static bool debounced_state = false;

// Poll the debounced button state directly (assumes called periodically, e.g. every ~1-2ms)
bool lv_port_indev_is_button_pressed(void) {
    bool raw = !gpio_get(BUTTON_PIN); // Active low

    if (raw) {
        release_count = 0;
        if (press_count < 10) {
            press_count++;
        }
        if (press_count >= 5) {
            debounced_state = true;
        }
    } else {
        press_count = 0;
        if (release_count < 10) {
            release_count++;
        }
        if (release_count >= 5) {
            debounced_state = false;
        }
    }

    return debounced_state;
}

// LVGL read callback
static void button_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    // Handled in main.c loop using precise press durations and groups
    data->state = LV_INDEV_STATE_RELEASED;
}

void lv_port_indev_init(void) {
    // Configure GPIO 23 with pull-up
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    // Create default group
    lv_group_t *g = lv_group_create();
    lv_group_set_default(g);

    // Register button in LVGL as a keypad navigation device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, button_read);
    lv_indev_set_group(indev, g);
}
