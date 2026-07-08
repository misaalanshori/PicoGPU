// gpu_driver.h — Host SDK: Public API for PicoGPU SPI communication
// Plain C, no MCU-specific dependencies beyond stdint.h + string.h.
// To use: implement the platform HAL functions (gpu_hal_*) for your host MCU.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "gpu_opcodes.h"
#include "gpu_profiles.h"

// =============================================================================
// Platform HAL — implement these for your host MCU
// =============================================================================

// Send 'len' bytes over SPI (MOSI). CS managed externally.
extern void gpu_hal_spi_write(const uint8_t *data, uint32_t len);

// Read 'len' bytes from SPI (MISO) while clocking dummy MOSI bytes.
extern void gpu_hal_spi_read(uint8_t *buf, uint32_t len);

// Assert CS LOW.
extern void gpu_hal_cs_assert(void);

// Deassert CS HIGH.
extern void gpu_hal_cs_deassert(void);

// Read BUSY pin: returns true if BUSY is HIGH (coprocessor busy).
extern bool gpu_hal_busy(void);

// Delay microseconds.
extern void gpu_hal_delay_us(uint32_t us);

// =============================================================================
// Core send/receive
// =============================================================================

// Send a command packet: [0xAA][opcode][len_lo][len_hi][payload...][crc_lo][crc_hi]
// Waits for BUSY LOW before asserting CS.
void gpu_send_command(uint8_t opcode, const uint8_t *payload, uint16_t len);

// Send a query and receive the fixed-size response.
// Query packet is sent, then after BUSY LOW + 50 µs, CS is reasserted to clock in response.
// Returns true on success, false on timeout.
bool gpu_query(uint8_t opcode, const uint8_t *payload, uint16_t plen,
               uint8_t *response, uint32_t rlen);

// Wait for BUSY to go LOW (with timeout_ms; 0 = wait forever).
// Returns true if BUSY cleared within timeout.
bool gpu_wait_not_busy(uint32_t timeout_ms);

// =============================================================================
// System control commands
// =============================================================================
void gpu_system_config(uint8_t profile_id, uint8_t reserve_vm);
void gpu_soft_reset(void);
void gpu_enable_frame_stats(bool enable);
void gpu_set_dither_mode(uint8_t mode);
void gpu_set_blend_mode(uint8_t mode);

// =============================================================================
// Drawing state
// =============================================================================
void gpu_set_chroma_key(uint16_t color_rgb565);
void gpu_enable_transparency(bool enable);
void gpu_fill_screen(uint16_t color);
void gpu_swap_buffers(void);
void gpu_swap_buffers_immediate(void);

// =============================================================================
// DRAW_PRIMITIVE helpers
// =============================================================================
void gpu_set_pixel(int16_t x, int16_t y, uint16_t color);
void gpu_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint16_t color);
void gpu_line_dashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                     uint8_t dash_on, uint8_t dash_off, uint16_t color);
void gpu_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t border_width, uint16_t color);
void gpu_rect_filled(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void gpu_rect_rounded(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint8_t radius, uint8_t border_width, uint16_t color);
void gpu_rect_rounded_filled(int16_t x, int16_t y, int16_t w, int16_t h,
                             uint8_t radius, uint16_t color);
void gpu_circle(int16_t cx, int16_t cy, int16_t r, uint8_t border_width, uint16_t color);
void gpu_circle_filled(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void gpu_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint8_t border_width, uint16_t color);
void gpu_ellipse_filled(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color);
void gpu_arc(int16_t cx, int16_t cy, int16_t r, int16_t start_deg, int16_t end_deg,
             uint8_t border_width, uint16_t color);
void gpu_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color);
void gpu_triangle_filled(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color);
void gpu_polygon_filled(uint8_t n, const int16_t *xs, const int16_t *ys, uint16_t color);
void gpu_flood_fill(int16_t x, int16_t y, uint16_t fill_color);

// =============================================================================
// Sprite commands
// =============================================================================

// BLIT_SPRITE: stream raw pixel data (no transforms)
void gpu_blit_sprite(int16_t x, int16_t y, uint8_t w, uint8_t h,
                     const uint8_t *pixels, uint32_t pixel_bytes);

// UPLOAD_VRAM: write sprite data to GPU VRAM at byte_offset
void gpu_upload_vram(uint32_t byte_offset, const uint8_t *data, uint32_t byte_count);

// DRAW_VRAM_SPRITE: blit from VRAM with optional transforms
void gpu_draw_vram_sprite(int16_t x, int16_t y, uint16_t w, uint16_t h,
                          uint32_t vram_offset, uint8_t transform_flags,
                          uint16_t palette_color);

// =============================================================================
// Text
// =============================================================================
void gpu_render_text(int16_t x, int16_t y, uint8_t font_id, uint16_t color,
                     uint8_t scale, const char *text);

// =============================================================================
// Queries (return parsed values)
// =============================================================================
uint8_t  gpu_get_status(void);
uint8_t  gpu_get_profile(void);
uint32_t gpu_get_vram_free(void);
uint32_t gpu_get_vram_used(void);
uint32_t gpu_get_sram_free(void);
void     gpu_get_version(uint8_t *major, uint8_t *minor, uint16_t *patch);
uint32_t gpu_get_capabilities(void);

// GET_FRAME_STATS response (8 bytes, spec §5.5)
bool gpu_get_frame_stats(uint16_t *render_ms, uint8_t *ring_peak_pct,
                         uint8_t *missed_frames, uint32_t *frame_count);

// =============================================================================
// Phase 2 — Scissor, Pixel Format, Frame Lifecycle, Events
// =============================================================================

// PUSH_CLIP_RECT (0x20): intersect with current clip, push result (max depth 8)
void gpu_push_clip_rect(int16_t x, int16_t y, int16_t w, int16_t h);

// POP_CLIP_RECT (0x21): restore parent clip
void gpu_pop_clip_rect(void);

// SET_PIXEL_FORMAT (0x14): switch format within active bpp_class
// format: PIXEL_FORMAT_* from gpu_opcodes.h (e.g. GPU_PIXEL_FORMAT_RGB332)
void gpu_set_pixel_format(uint8_t format);

// BEGIN_FRAME (0x12): mark start of frame (resets ring peak counter, starts timer)
void gpu_begin_frame(void);

// END_FRAME (0x13): mark end of frame (updates stats, fires deferred swap, pushes event)
void gpu_end_frame(void);

// =============================================================================
// Phase 2 — Event Buffer
// =============================================================================

// Event record — 8 bytes, matches GET_EVENTS (0xE9) wire format
typedef struct {
    uint8_t  event_type;    // GPU_EVT_* code
    uint8_t  reserved;      // always 0x00
    uint16_t timestamp_ms;  // ms timestamp from GPU clock
    uint32_t payload;       // event-specific data
} gpu_event_record_t;

// Event type codes (matches firmware event_buffer.h EVT_*)
#define GPU_EVT_FRAME_COMPLETE   0x01u
#define GPU_EVT_VM_PROC_DONE     0x02u
#define GPU_EVT_VRAM_NEARLY_FULL 0x03u
#define GPU_EVT_ERROR            0x04u
#define GPU_EVT_BUFFER_OVERFLOW  0xFFu

// GET_EVENTS (0xE9): drain the GPU event buffer.
// Sends the query, reads the variable-length response (B0=count, then N×8-byte records).
// Returns the number of events written into out[] (0 if none or on error).
// out[] must have room for at least max_count entries.
uint8_t gpu_get_events(gpu_event_record_t *out, uint8_t max_count);
