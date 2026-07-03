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

#include <math.h>
#include <string.h>

#include "audio.h"
// ============================================================================
// Configuration
// ============================================================================

#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define BOX_SIZE 128
#define BG_COLOR 0x0010  // Dark blue (RGB565)
#define BOX_COLOR 0x0000 // Black Hole of Death (RGB565)

// Audio configuration
#define AUDIO_SAMPLE_RATE 48000
#define TONE_AMPLITUDE 6000

// ============================================================================
// Animation State
// ============================================================================

static volatile int box_x = 50, box_y = 50;
static int box_dx = 4, box_dy = 2;

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
        hstx_di_queue_push(&island);
    }
}

// ============================================================================
// Scanline Callback (runs on Core 1)
// ============================================================================

static uint16_t rainbow_palette[256];
static uint32_t rainbow_palette32[256];

static inline uint16_t hue_to_rgb565(uint8_t hue)
{
    uint8_t region = hue / 43;
    uint8_t remainder = (hue - region * 43) * 6;
    uint8_t r = 0, g = 0, b = 0;

    switch (region) {
        case 0: r = 255; g = remainder; b = 0; break;         // Red -> Yellow
        case 1: r = 255 - remainder; g = 255; b = 0; break;   // Yellow -> Green
        case 2: r = 0; g = 255; b = remainder; break;         // Green -> Cyan
        case 3: r = 0; g = 255 - remainder; b = 255; break;   // Cyan -> Blue
        case 4: r = remainder; g = 0; b = 255; break;         // Blue -> Magenta
        default: r = 255; g = 0; b = 255 - remainder; break; // Magenta -> Red
    }

    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void init_rainbow_palette(void)
{
    for (int i = 0; i < 256; i++) {
        rainbow_palette[i] = hue_to_rgb565((uint8_t)i);
    }
    for (int i = 0; i < 256; i++) {
        rainbow_palette32[i] = (uint32_t)rainbow_palette[i] | ((uint32_t)rainbow_palette[(uint8_t)(i + 1)] << 16);
    }
}

static void __scratch_x("") scanline_callback(uint32_t v_scanline, uint32_t active_line, uint32_t *dst)
{
    (void)v_scanline;

    int fb_line = active_line;

    uint8_t color_idx = (uint8_t)(box_x + box_y + fb_line);

    // Read current box position
    int bx = box_x;
    int by = box_y;

    uint32_t box = BOX_COLOR | (BOX_COLOR << 16);

    // Check if this line intersects the box vertically
    if (fb_line >= by && fb_line < by + BOX_SIZE) {
        // Three regions: before box, box, after box
        int i = 0;
        int box_start = bx / 2;
        int box_end = (bx + BOX_SIZE) / 2;

        // Region 1: before box
        for (; i < box_start; i++) {
            dst[i] = rainbow_palette32[color_idx];
            color_idx += 2;
        }

        // Region 2: box
        for (; i < box_end && i < FRAME_WIDTH / 2; i++) {
            dst[i] = box;
            color_idx += 2;
        }

        // Region 3: after box
        for (; i < FRAME_WIDTH / 2; i++) {
            dst[i] = rainbow_palette32[color_idx];
            color_idx += 2;
        }
    } else {
        // Fast path: entire line is background
        for (int i = 0; i < FRAME_WIDTH / 2; i++) {
            dst[i] = rainbow_palette32[color_idx];
            color_idx += 2;
        }
    }

    
}

// ============================================================================
// Main (Core 0)
// ============================================================================

static void update_box(void)
{
    int x = box_x + box_dx;
    int y = box_y + box_dy;

    if (x <= 0 || x + BOX_SIZE >= FRAME_WIDTH) {
        box_dx = -box_dx;
        x = box_x + box_dx;
    }
    if (y <= 0 || y + BOX_SIZE >= FRAME_HEIGHT) {
        box_dy = -box_dy;
        y = box_y + box_dy;
    }

    box_x = x;
    box_y = y;
}

int main(void)
{
    // 720p60: 372 MHz at 1.3V. Closest achievable to 371.25 MHz with 12 MHz XOSC
    // (0.2% high -> 74.4 MHz pixel clock, within HDMI tolerance for 720p60).
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(372000, true);

    stdio_init_all();

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    sleep_ms(1000);

    init_sine_table();
    init_rainbow_palette();
    note_frames_remaining = current_melody[0].duration;
    phase_increment = (uint32_t)(((uint64_t)current_melody[0].freq << 32) / AUDIO_SAMPLE_RATE);

    // Initialize HDMI output — rt variant
    hstx_di_queue_init();
    video_output_set_mode(&video_mode_720_p);
    video_output_init(FRAME_WIDTH, FRAME_HEIGHT);

    // Register scanline callback
    video_output_set_scanline_callback(scanline_callback);

    // Pre-fill audio buffer
    generate_audio();

    // Launch Core 1 for HSTX output
    multicore_launch_core1(video_output_core1_run);
    sleep_ms(100);

    // Main loop - animation + audio
    uint32_t last_frame = 0;
    bool led_state = false;

    while (1) {
        // Keep audio buffer fed
        generate_audio();

        while (video_frame_count == last_frame) {
            generate_audio();
            tight_loop_contents();
        }
        last_frame = video_frame_count;

        // Update animation
        update_box();

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
