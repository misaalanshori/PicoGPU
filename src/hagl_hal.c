#include <rgb565.h>
#include <hagl/backend.h>

#include "hagl_hal.h"

#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/pixel_formats.h"
#include "pico_hdmi/video_output.h" // for DI_HSYNC_ACTIVE
#include "pico_hdmi/video_output_rt.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <stdlib.h>

#define BUFFER_SIZE    (DISPLAY_WIDTH * DISPLAY_HEIGHT)

// Allocate the single framebuffer pointer dynamically
static hagl_color_t *framebuffer = NULL;

// Pointers to manage the single buffering state
hagl_color_t *active_buffer = NULL; // Where HAGL draws
hagl_color_t *front_buffer = NULL;  // What is currently on the screen

static void __scratch_x("") scanline_callback(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;

    if (active_line < 60 || active_line >= 420) {
        for (int x = 0; x < DISPLAY_WIDTH / 4; x++) {
            dst[x] = 0;
        }
    } else {
        int fb_line = active_line - 60;
        int pixel_zero = fb_line * DISPLAY_WIDTH;
        uint32_t *src32 = (uint32_t *)&front_buffer[pixel_zero];
        for (int x = 0; x < DISPLAY_WIDTH / 4; x++) {
            dst[x] = src32[x];
        }
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
    // No-op for single buffering
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
    // Allocate framebuffer dynamically
    framebuffer = malloc(BUFFER_SIZE);
    active_buffer = framebuffer;
    front_buffer = framebuffer;

    // Initialize HDMI output — rt variant
    hstx_di_queue_init();
    video_output_set_mode(&video_mode_480_p);
    video_output_init(640, 480);

    // Register scanline callback
    video_output_set_scanline_callback(scanline_callback);

    // Set pixel format to 8bpp RGB332 with hardware expansion BEFORE Core 1 is launched
    // to ensure the DMA and HSTX start in the correct mode from the first frame.
    video_output_set_pixel_format(HSTX_EXPAND_TMDS_RGB332, HSTX_EXPAND_SHIFT_8BPP);

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