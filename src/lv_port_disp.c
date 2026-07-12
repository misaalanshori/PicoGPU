#include "lv_port_disp.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include "pico_hdmi/pixel_formats.h"
#include "pico_hdmi/video_output.h"
#include "pico_hdmi/video_output_rt.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <string.h>

#define DISPLAY_WIDTH 640
#define DISPLAY_HEIGHT 360

// 8bpp RGB332 physical framebuffer (640x360)
static uint8_t framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT] __attribute__((aligned(4)));

// Scanline Callback called on Core 1 in high-speed ISR context
static void __scratch_x("") scanline_callback(uint32_t v_scanline, uint32_t active_line, uint32_t *dst) {
    (void)v_scanline;

    if (active_line < 60 || active_line >= 420) {
        // Output black for vertical blanking borders (top/bottom letterboxing)
        for (int x = 0; x < DISPLAY_WIDTH / 4; x++) {
            dst[x] = 0;
        }
    } else {
        int fb_line = active_line - 60;
        int pixel_zero = fb_line * DISPLAY_WIDTH;
        uint32_t *src32 = (uint32_t *)&framebuffer[pixel_zero];
        for (int x = 0; x < DISPLAY_WIDTH / 4; x++) {
            dst[x] = src32[x];
        }
    }
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint16_t *src = (uint16_t *)px_map;
    int32_t width = lv_area_get_width(area);
    int32_t height = lv_area_get_height(area);

    for (int32_t y = 0; y < height; y++) {
        int32_t target_y = area->y1 + y;
        for (int32_t x = 0; x < width; x++) {
            int32_t target_x = area->x1 + x;
            if (target_x >= 0 && target_x < DISPLAY_WIDTH && target_y >= 0 && target_y < DISPLAY_HEIGHT) {
                uint16_t rgb565 = src[y * width + x];

                // Convert RGB565 (RRRRRGGGGGGBBBBB) to RGB332 (RRRGGGBB)
                uint8_t r3 = (rgb565 >> 13) & 0x07;
                uint8_t g3 = (rgb565 >> 8) & 0x07;
                uint8_t b2 = (rgb565 >> 3) & 0x03;
                uint8_t rgb332 = (r3 << 5) | (g3 << 2) | b2;

                framebuffer[target_y * DISPLAY_WIDTH + target_x] = rgb332;
            }
        }
    }

    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void) {
    // Fill physical framebuffer with dark blue/grey initially
    memset(framebuffer, 0x01, sizeof(framebuffer));

    // 1. Initialize HDMI Library (640x480 standard VGA DVI/HDMI signal)
    hstx_di_queue_init();
    video_output_set_mode(&video_mode_480_p);
    video_output_init(640, 480);
    video_output_set_scanline_callback(scanline_callback);
    video_output_set_pixel_format(HSTX_EXPAND_TMDS_RGB332, HSTX_EXPAND_SHIFT_8BPP);

    // Launch HDMI driver on Core 1
    multicore_launch_core1(video_output_core1_run);
    sleep_ms(100);

    // 2. Register LVGL display
    lv_display_t *disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush_cb);

    // Use a single 20-line partial rendering draw buffer (saves RAM)
    #define DRAW_BUF_HEIGHT 20
    static uint8_t draw_buf[DISPLAY_WIDTH * DRAW_BUF_HEIGHT * sizeof(uint16_t)] __attribute__((aligned(4)));
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
}
