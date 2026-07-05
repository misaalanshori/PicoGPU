#include <rgb565.h>
#include <hagl/backend.h>

#include "hagl_hal.h"

#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/video_output.h" // for DI_HSYNC_ACTIVE
#include "pico_hdmi/video_output_rt.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#define BUFFER_SIZE    (DISPLAY_WIDTH * DISPLAY_HEIGHT)

// Allocate the double buffers
static hagl_color_t buffer_a[BUFFER_SIZE];
static hagl_color_t buffer_b[BUFFER_SIZE];

static uint32_t color_cache[256];

// Pointers to manage the double buffering state
static hagl_color_t *active_buffer = buffer_a; // Where HAGL draws
static hagl_color_t *front_buffer = buffer_b;  // What is currently on the screen

uint16_t rgb332_to_rgb565_accurate(uint8_t rgb332) {
    // 1. Extract the individual RGB channels
    uint8_t r = (rgb332 >> 5) & 0x07; // Top 3 bits
    uint8_t g = (rgb332 >> 2) & 0x07; // Middle 3 bits
    uint8_t b = rgb332 & 0x03;        // Bottom 2 bits

    // 2. Expand them to fill the 5-bit and 6-bit spaces
    // Red: 3 bits to 5 bits (Copy the RRR bits, then append the top 2 R bits)
    uint16_t r5 = (r << 2) | (r >> 1);
    
    // Green: 3 bits to 6 bits (Perfectly copy GGG twice -> GGGGGG)
    uint16_t g6 = (g << 3) | g;
    
    // Blue: 2 bits to 5 bits (Copy BB, then BB, then top B -> BBBBB)
    uint16_t b5 = (b << 3) | (b << 1) | (b >> 1);

    // 3. Shift into the final RGB565 positions and combine
    return (r5 << 11) | (g6 << 5) | b5;
}

static void __scratch_x("") scanline_callback(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;

    int fb_line = active_line/4;
    int pixel_zero = fb_line * DISPLAY_WIDTH;

    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        uint32_t packed = color_cache[front_buffer[pixel_zero + x]];
        dst[x * 2]     = packed;
        dst[x * 2 + 1] = packed;
    }
}


static void put_pixel(void *self, int16_t x0, int16_t y0, hagl_color_t color)
{
    // Hardware clipping/bounds check
    if (x0 < 0 || x0 >= DISPLAY_WIDTH || y0 < 0 || y0 >= DISPLAY_HEIGHT) {
        return;
    }
    // Calculate the 1D array index for the 2D coordinate
    active_buffer[(DISPLAY_WIDTH * y0) + x0] = color;
}

static size_t flush(void *self)
{
    // Swap the pointers
    uint8_t *temp = active_buffer;
    active_buffer = front_buffer;
    front_buffer = temp;

    // Optional: Clear the new active buffer so you start the next frame with a blank slate
    // memset(active_buffer, 0, BUFFER_SIZE);
    return BUFFER_SIZE;
}

static void close(void *self)
{

}

static hagl_color_t color(void *self, uint8_t r, uint8_t g, uint8_t b) {
    return (hagl_color_t)((r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6));
}

void hagl_hal_init(hagl_backend_t *backend)
{
    for (int i = 0; i < 256; i++) {
        uint16_t color = rgb332_to_rgb565_accurate(i);
        color_cache[i] = (uint32_t)color | ((uint32_t)color << 16);
    }

    // Initialize HDMI output — rt variant
    hstx_di_queue_init();
    video_output_set_mode(&video_mode_720_p);
    video_output_init(DISPLAY_WIDTH*4, DISPLAY_HEIGHT*4);

    // Register scanline callback
    video_output_set_scanline_callback(scanline_callback);

    // Launch Core 1 for HSTX output
    multicore_launch_core1(video_output_core1_run);
    sleep_ms(100);
    
    backend->width = DISPLAY_WIDTH;
    backend->height = DISPLAY_HEIGHT;
    backend->depth = DISPLAY_DEPTH;
    backend->put_pixel = put_pixel;
    backend->color = color;

    backend->flush = flush;
    backend->close = close;

}