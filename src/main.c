/**
 * pico_hdmi Bouncing Box (rt) Example
 *
 * Same visuals as bouncing_box, but goes through the runtime-mode-switching
 * variant of the library (video_output_rt.c) at 1280x720 @ 60Hz (CEA VIC 4).
 *
 * The CMakeLists enables PICO_HDMI_RUNTIME_MODES and defines VIDEO_MODE_1280x720
 * on both the library and this executable.
 *
 * Target: RP2350 (Raspberry Pi Pico 2), overclocked to 372 MHz at 1.3V core.
 */

#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/video_output.h" // for DI_HSYNC_ACTIVE
#include "pico_hdmi/video_output_rt.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/pll.h"
#include "hardware/structs/qmi.h"

#include <math.h>
#include <string.h>

#include "audio.h"

#include <malloc.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <sys/time.h>

#include <aps.h>
#include <font6x9.h>
#include <fps.h>
#include <hagl.h>
#include <hagl_hal.h>

#include "deform.h"
#include "metaballs.h"
#include "plasma.h"
#include "rotozoom.h"

// Audio configuration
#define AUDIO_SAMPLE_RATE 48000
#define TONE_AMPLITUDE 6000

// ============================================================================
// Audio State - Für Elise
// ============================================================================

#define SINE_TABLE_SIZE 256
static int16_t sine_table[SINE_TABLE_SIZE];
static uint32_t audio_phase = 0;
static uint32_t phase_increment = 0;
static int audio_frame_counter = 0;

// Use Korobeiniki for the demo (Für Elise kept for reference)

static int current_melody_length = KOROBEINIKI_LENGTH;

static int melody_index = 0;
static int note_frames_remaining = 0;

// From pico_effects
static uint8_t effect = 3;
volatile bool fps_flag = false;
volatile bool switch_flag = true;
static fps_instance_t fps;
static aps_instance_t bps;

// static uint8_t *buffer;
// static hagl_backend_t backend;
static hagl_backend_t *display;

wchar_t message[32];

static const uint64_t US_PER_FRAME_60_FPS = 1000000 / 60;
static const uint64_t US_PER_FRAME_30_FPS = 1000000 / 30;
static const uint64_t US_PER_FRAME_25_FPS = 1000000 / 25;

static char demo[4][32] = {
    "METABALLS",
    "PLASMA",
    "ROTOZOOM",
    "DEFORM",
};

static void init_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = (float)i * 2.0F * 3.14159265F / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(sinf(angle) * TONE_AMPLITUDE);
    }
}

static void advance_melody(void)
{
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

static inline int16_t get_sine_sample(void)
{
    if (phase_increment == 0)
        return 0; // Rest
    int16_t s = sine_table[(audio_phase >> 24) & 0xFF];
    audio_phase += phase_increment;
    return s;
}

static void generate_audio(void)
{
    // Keep the audio queue fed
    // Change this to like 500 and DI_RING_BUFFER_SIZE to 512 so the audio just slows down and not lag horribly
    // Or just disable audio tbh
    while (hstx_di_queue_get_level() < 200) {
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


// From pico_effects

size_t total_heap() {
    extern char __StackLimit, __bss_end__;

    return &__StackLimit - &__bss_end__;
}

size_t free_heap(void) {
    struct mallinfo m = mallinfo();

    return total_heap() - m.uordblks;
}

bool switch_timer_callback(struct repeating_timer *t) {
    switch_flag = true;
    return true;
}

bool show_timer_callback(struct repeating_timer *t) {
    fps_flag = true;
    return true;
}

void static inline switch_demo() {
    switch_flag = false;
    printf("[switch] closing effect %d\r\n", effect);

    switch (effect) {
        case 0:
            //metaballs_close();
            break;
        case 1:
            plasma_close();
            break;
        case 2:
            //rotozoom_close();
            break;
        case 3:
            deform_close();
            break;
    }

    effect = (effect + 1) % 4;
    printf("[switch] opening effect %d, free heap: %d\r\n", effect, free_heap());

    switch (effect) {
        case 0:
            printf("[switch] metaballs_init start\r\n");
            metaballs_init(display);
            printf("[switch] metaballs_init done\r\n");
            break;
        case 1:
            printf("[switch] plasma_init start\r\n");
            plasma_init(display);
            printf("[switch] plasma_init done\r\n");
            break;
        case 2:
            printf("[switch] rotozoom_init start\r\n");
            rotozoom_init(display);
            printf("[switch] rotozoom_init done\r\n");
            break;
        case 3:
            printf("[switch] deform_init start\r\n");
            deform_init(display);
            printf("[switch] deform_init done\r\n");
            break;
    }

    fps_init(&fps);
    aps_init(&bps);
}

void static inline show_fps() {
    hagl_color_t green = hagl_color(display, 0, 255, 0);

    fps_flag = 0;

    /* Set clip window to full screen so we can display the messages. */
    hagl_set_clip(display, 0, 0, display->width - 1, display->height - 1);

    /* Print the message on top left corner. */
    swprintf(message, sizeof(message), L"%s    ", demo[effect]);
    hagl_put_text(display, message, 4, 4, green, font6x9);

    /* Print the message on lower left corner. */
    swprintf(message, sizeof(message), L"%.*f FPS  ", 0, fps.current);
    hagl_put_text(display, message, 4, display->height - 14, green, font6x9);

    /* Print the message on lower right corner. */
    swprintf(message, sizeof(message), L"%.*f KBPS  ", 0, bps.current / 1024);
    hagl_put_text(
        display, message, display->width - 60, display->height - 14, green, font6x9
    );

    /* Set clip window back to smaller so effects do not mess the messages. */
    hagl_set_clip(display, 0, 20, display->width - 1, display->height - 21);
}

// ============================================================================
// Main (Core 0)
// ============================================================================
int main(void)
{
    // Immediate LED initialization to debug boot success
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    // 720p60: 372 MHz at 1.3V. Closest achievable to 371.25 MHz with 12 MHz XOSC
    // (0.2% high -> 74.4 MHz pixel clock, within HDMI tolerance for 720p60).
    // vreg_set_voltage(VREG_VOLTAGE_1_30);
    // sleep_ms(10);
    // set_sys_clock_khz(372000, true);

    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(20);

    qmi_hw->m[0].timing = (qmi_hw->m[0].timing & ~0xFF) | 4;

    volatile uint32_t *flash_ptr = (volatile uint32_t *)0x10000000;
    (void)*flash_ptr;

    set_sys_clock_khz(372000, true);

    sleep_ms(2500);

    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    sleep_ms(2500);

    

    printf("[main] stdio inited \r\n");

    // From pico_effects
    size_t bytes = 0;
    struct repeating_timer switch_timer;
    struct repeating_timer show_timer;

    printf("[main] %d total heap \r\n", total_heap());
    printf("[main] %d free heap \r\n", free_heap());

    fps_init(&fps);

    display = hagl_init();
    printf("[main] display inited \r\n");
    

    hagl_clear(display);
    hagl_set_clip(display, 0, 20, display->width - 1, display->height - 21);
    

    /* Change demo every 10 seconds. */
    add_repeating_timer_ms(5000, switch_timer_callback, NULL, &switch_timer);

    /* Update displayed FPS counter every 250 ms. */
    add_repeating_timer_ms(250, show_timer_callback, NULL, &show_timer);

    printf("[main] timer inited \r\n");

    init_sine_table();
    note_frames_remaining = current_melody[0].duration;
    phase_increment = (uint32_t)(((uint64_t)current_melody[0].freq << 32) / AUDIO_SAMPLE_RATE);

    // Pre-fill audio buffer
    generate_audio();

    // Main loop - animation + audio
    uint32_t last_frame = 0;
    bool led_state = false;

    printf("[main] loop starting \r\n");
    while (1) {
        // Keep audio buffer fed
        generate_audio();

        while (video_frame_count == last_frame) {
            generate_audio();
            tight_loop_contents();
        }
        last_frame = video_frame_count;

        // from pico_effects
        {
            // uint64_t start = time_us_64();

            switch (effect) {
                case 0:
                    metaballs_animate(display);
                    metaballs_render(display);
                    break;
                case 1:
                    plasma_animate(display);
                    plasma_render(display);
                    break;
                case 2:
                    rotozoom_animate();
                    rotozoom_render(display);
                    break;
                case 3:
                    deform_animate();
                    deform_render(display);
                    break;
            }

            /* Update the displayed fps if requested. */
            show_fps();

            /* Flush back buffer contents to display. NOP if single buffering. */
            bytes = hagl_flush(display);

            aps_update(&bps, bytes);
            fps_update(&fps);

            /* Print the message in console and switch to next demo. */
            if (switch_flag) {
                printf(
                    "[main] %s at %d fps / %d kBps\r\n",
                    demo[effect],
                    (uint32_t)fps.current,
                    (uint32_t)(bps.current / 1024)
                );
                switch_demo();
            }

            // /* Cap the demos to 60 fps. This is mostly to accommodate to smaller */
            // /* screens where plasma will run too fast. */
            // busy_wait_until(start + US_PER_FRAME_60_FPS);
        }

        // Advance melody (one note step per frame)
        advance_melody();

        // LED heartbeat
        if ((video_frame_count % 30) == 0) {
            led_state = !led_state;
            gpio_put(PICO_DEFAULT_LED_PIN, led_state);
        }


    }

    return 0;
}
