// gpu_driver.c — Host SDK: PicoGPU SPI command driver implementation
// Plain C. No MCU-specific code — depends only on the gpu_hal_* functions
// the user implements for their platform.

#include "gpu_driver.h"
#include <string.h>

// =============================================================================
// CRC-16/CCITT (poly 0x1021, init 0xFFFF) — matches firmware packets.c
// =============================================================================
static uint16_t s_crc_table[256];
static bool     s_crc_ready = false;

static void crc16_init_table(void) {
    for (uint16_t i = 0; i < 256; i++) {
        uint16_t crc = i << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
        s_crc_table[i] = crc;
    }
    s_crc_ready = true;
}

static uint16_t crc16(const uint8_t *data, uint32_t len) {
    if (!s_crc_ready) crc16_init_table();
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc = (uint16_t)((crc << 8) ^ s_crc_table[(crc >> 8) ^ data[i]]);
    }
    return crc;
}

// =============================================================================
// Internal: wait for BUSY to clear
// =============================================================================
bool gpu_wait_not_busy(uint32_t timeout_ms) {
    if (timeout_ms == 0) {
        while (gpu_hal_busy()) {}
        return true;
    }
    // Simple poll with delay (platform provides delay_us)
    uint32_t us = 0;
    while (gpu_hal_busy()) {
        gpu_hal_delay_us(100);
        us += 100;
        if (us >= timeout_ms * 1000) return false;
    }
    return true;
}

// =============================================================================
// Internal: build and send one packet
// Layout: [0xAA][opcode][len_lo][len_hi][payload...][crc_lo][crc_hi]
// CRC covers: opcode + len bytes + payload (not sync byte)
// =============================================================================
// Maximum pixel/data bytes in a single packet — must match firmware MAX_PAYLOAD_SIZE
// (= RING_BUFFER_SIZE = 4096). The 9-byte UPLOAD_VRAM header is overhead on top.
#define GPU_MAX_CHUNK_BYTES  4096u
#define UPLOAD_VRAM_HDR_SIZE 9u
#define UPLOAD_VRAM_DATA_MAX (GPU_MAX_CHUNK_BYTES - UPLOAD_VRAM_HDR_SIZE)  // 4087 bytes/chunk

// =============================================================================
// Internal: build and send one packet
// Layout: [0xAA][opcode][len_lo][len_hi][payload...][crc_lo][crc_hi]
// CRC covers: opcode + len bytes + payload (not sync byte)
// payload must be <= GPU_MAX_CHUNK_BYTES bytes.
// =============================================================================
void gpu_send_command(uint8_t opcode, const uint8_t *payload, uint16_t len) {
    // Wait for GPU to be ready
    gpu_wait_not_busy(0);

    // Compute CRC incrementally — no large scratch buffer needed.
    if (!s_crc_ready) crc16_init_table();
    uint16_t crc = 0xFFFF;
    uint8_t  pre[3] = { opcode, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
    for (int i = 0; i < 3; i++)
        crc = (uint16_t)((crc << 8) ^ s_crc_table[(crc >> 8) ^ pre[i]]);
    for (uint16_t i = 0; i < len && payload; i++)
        crc = (uint16_t)((crc << 8) ^ s_crc_table[(crc >> 8) ^ payload[i]]);

    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8) };
    uint8_t sync = GPU_SYNC_BYTE;

    gpu_hal_cs_assert();
    gpu_hal_spi_write(&sync, 1);
    gpu_hal_spi_write(pre, 3);
    if (len > 0 && payload) gpu_hal_spi_write(payload, len);
    gpu_hal_spi_write(crc_bytes, 2);
    gpu_hal_cs_deassert();
}

// =============================================================================
// Query: send command, wait, receive fixed-size response
// =============================================================================
bool gpu_query(uint8_t opcode, const uint8_t *payload, uint16_t plen,
               uint8_t *response, uint32_t rlen) {
    // Send the query packet
    gpu_send_command(opcode, payload, plen);

    // Wait for GPU to process it (BUSY goes LOW)
    if (!gpu_wait_not_busy(1000)) return false;

    // Additional 50 µs guard (spec §5.5 open issue A placeholder)
    gpu_hal_delay_us(50);

    // Clock in the response
    gpu_hal_cs_assert();
    gpu_hal_spi_read(response, rlen);
    gpu_hal_cs_deassert();
    return true;
}

// =============================================================================
// System control
// =============================================================================
void gpu_system_config(uint8_t profile_id, uint8_t reserve_vm) {
    uint8_t p[2] = { profile_id, reserve_vm };
    gpu_send_command(GPU_OP_SYSTEM_CONFIG, p, 2);
    // SYSTEM_CONFIG asserts BUSY internally — wait for it to complete
    gpu_wait_not_busy(5000);
}

void gpu_soft_reset(void) {
    gpu_send_command(GPU_OP_SOFT_RESET, NULL, 0);
}

void gpu_enable_frame_stats(bool enable) {
    uint8_t p = enable ? 1 : 0;
    gpu_send_command(GPU_OP_ENABLE_FRAME_STATS, &p, 1);
}

void gpu_set_dither_mode(uint8_t mode) {
    gpu_send_command(GPU_OP_SET_DITHER_MODE, &mode, 1);
}

void gpu_set_blend_mode(uint8_t mode) {
    gpu_send_command(GPU_OP_SET_BLEND_MODE, &mode, 1);
}

// =============================================================================
// Drawing state
// =============================================================================
void gpu_set_chroma_key(uint16_t color) {
    uint8_t p[2] = { (uint8_t)(color & 0xFF), (uint8_t)(color >> 8) };
    gpu_send_command(GPU_OP_SET_CHROMA_KEY, p, 2);
}

void gpu_enable_transparency(bool enable) {
    uint8_t p = enable ? 1 : 0;
    gpu_send_command(GPU_OP_ENABLE_TRANSPARENCY, &p, 1);
}

void gpu_fill_screen(uint16_t color) {
    uint8_t p[2] = { (uint8_t)(color & 0xFF), (uint8_t)(color >> 8) };
    gpu_send_command(GPU_OP_FILL_SCREEN, p, 2);
}

void gpu_swap_buffers(void) {
    gpu_send_command(GPU_OP_SWAP_BUFFERS, NULL, 0);
}

void gpu_swap_buffers_immediate(void) {
    gpu_send_command(GPU_OP_SWAP_BUFFERS_IMM, NULL, 0);
}

// =============================================================================
// DRAW_PRIMITIVE helpers — each builds the payload and calls gpu_send_command
// =============================================================================
static inline void put16(uint8_t *buf, int16_t v) {
    buf[0] = (uint8_t)((uint16_t)v & 0xFF);
    buf[1] = (uint8_t)((uint16_t)v >> 8);
}

void gpu_set_pixel(int16_t x, int16_t y, uint16_t color) {
    uint8_t p[7]; p[0] = GPU_PRIM_SET_PIXEL;
    put16(p+1, x); put16(p+3, y); put16(p+5, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 7);
}

void gpu_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thick, uint16_t color) {
    uint8_t p[12]; p[0] = GPU_PRIM_LINE;
    put16(p+1, x0); put16(p+3, y0); put16(p+5, x1); put16(p+7, y1);
    p[9] = thick; put16(p+10, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 12);
}

void gpu_line_dashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                     uint8_t dash_on, uint8_t dash_off, uint16_t color) {
    uint8_t p[13]; p[0] = GPU_PRIM_LINE_DASHED;
    put16(p+1, x0); put16(p+3, y0); put16(p+5, x1); put16(p+7, y1);
    p[9] = dash_on; p[10] = dash_off; put16(p+11, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 13);
}

void gpu_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t bw, uint16_t color) {
    uint8_t p[12]; p[0] = GPU_PRIM_RECT;
    put16(p+1, x); put16(p+3, y); put16(p+5, w); put16(p+7, h);
    p[9] = bw; put16(p+10, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 12);
}

void gpu_rect_filled(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    uint8_t p[11]; p[0] = GPU_PRIM_RECT_FILLED;
    put16(p+1, x); put16(p+3, y); put16(p+5, w); put16(p+7, h); put16(p+9, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 11);
}

void gpu_rect_rounded(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint8_t radius, uint8_t bw, uint16_t color) {
    uint8_t p[13]; p[0] = GPU_PRIM_RECT_ROUNDED;
    put16(p+1, x); put16(p+3, y); put16(p+5, w); put16(p+7, h);
    p[9] = radius; p[10] = bw; put16(p+11, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 13);
}

void gpu_rect_rounded_filled(int16_t x, int16_t y, int16_t w, int16_t h,
                             uint8_t radius, uint16_t color) {
    uint8_t p[12]; p[0] = GPU_PRIM_RECT_ROUNDED_FILLED;
    put16(p+1, x); put16(p+3, y); put16(p+5, w); put16(p+7, h);
    p[9] = radius; put16(p+10, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 12);
}

void gpu_circle(int16_t cx, int16_t cy, int16_t r, uint8_t bw, uint16_t color) {
    uint8_t p[10]; p[0] = GPU_PRIM_CIRCLE;
    put16(p+1, cx); put16(p+3, cy); put16(p+5, r); p[7] = bw; put16(p+8, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 10);
}

void gpu_circle_filled(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    uint8_t p[9]; p[0] = GPU_PRIM_CIRCLE_FILLED;
    put16(p+1, cx); put16(p+3, cy); put16(p+5, r); put16(p+7, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 9);
}

void gpu_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint8_t bw, uint16_t color) {
    uint8_t p[12]; p[0] = GPU_PRIM_ELLIPSE;
    put16(p+1, cx); put16(p+3, cy); put16(p+5, rx); put16(p+7, ry);
    p[9] = bw; put16(p+10, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 12);
}

void gpu_ellipse_filled(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color) {
    uint8_t p[11]; p[0] = GPU_PRIM_ELLIPSE_FILLED;
    put16(p+1, cx); put16(p+3, cy); put16(p+5, rx); put16(p+7, ry); put16(p+9, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 11);
}

void gpu_arc(int16_t cx, int16_t cy, int16_t r, int16_t start, int16_t end,
             uint8_t bw, uint16_t color) {
    uint8_t p[14]; p[0] = GPU_PRIM_ARC;
    put16(p+1, cx); put16(p+3, cy); put16(p+5, r);
    put16(p+7, start); put16(p+9, end); p[11] = bw; put16(p+12, (int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 14);
}

void gpu_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color) {
    uint8_t p[15]; p[0] = GPU_PRIM_TRIANGLE;
    put16(p+1,x0); put16(p+3,y0); put16(p+5,x1); put16(p+7,y1);
    put16(p+9,x2); put16(p+11,y2); put16(p+13,(int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 15);
}

void gpu_triangle_filled(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color) {
    uint8_t p[15]; p[0] = GPU_PRIM_TRIANGLE_FILLED;
    put16(p+1,x0); put16(p+3,y0); put16(p+5,x1); put16(p+7,y1);
    put16(p+9,x2); put16(p+11,y2); put16(p+13,(int16_t)color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 15);
}

void gpu_polygon_filled(uint8_t n, const int16_t *xs, const int16_t *ys, uint16_t color) {
    if (n < 3 || n > 64) return;
    uint8_t p[4 + 64 * 4];
    p[0] = GPU_PRIM_POLYGON_FILLED;
    p[1] = n;
    put16(p+2, (int16_t)color);
    for (uint8_t i = 0; i < n; i++) {
        put16(p + 4 + i*4, xs[i]);
        put16(p + 4 + i*4 + 2, ys[i]);
    }
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, (uint16_t)(4 + n * 4));
}

void gpu_flood_fill(int16_t x, int16_t y, uint16_t fill_color) {
    uint8_t p[7]; p[0] = GPU_PRIM_FLOOD_FILL;
    put16(p+1, x); put16(p+3, y); put16(p+5, (int16_t)fill_color);
    gpu_send_command(GPU_OP_DRAW_PRIMITIVE, p, 7);
}

// =============================================================================
// Sprite commands
// =============================================================================
void gpu_blit_sprite(int16_t x, int16_t y, uint8_t w, uint8_t h,
                     const uint8_t *pixels, uint32_t pixel_bytes) {
    // BLIT_SPRITE streams raw pixel data. Chunk by rows so each packet fits
    // within GPU_MAX_CHUNK_BYTES. Header is 7 bytes; data budget = 4089 bytes/chunk.
    // Each chunk is a full BLIT_SPRITE packet — the firmware draws as it receives.
    //
    // Payload layout: [x(2)][y(2)][w(1)][h(1)][rle(1)][pixels...]
    // For multi-chunk sprites, x/y/w stay the same; y advances by rows_sent.
    const uint32_t HDR   = 7u;
    const uint32_t DMAX  = GPU_MAX_CHUNK_BYTES - HDR;  // max pixel bytes per packet
    const uint32_t bytes_per_row = (uint32_t)w * 1u;   // 1 byte/px for 8bpp (adjust for 16bpp)

    uint32_t sent = 0;
    int16_t  cur_y = y;

    while (sent < pixel_bytes) {
        uint32_t rows_this = DMAX / bytes_per_row;
        if (rows_this == 0) rows_this = 1;              // at least one row
        uint32_t chunk_px = rows_this * bytes_per_row;
        if (chunk_px > pixel_bytes - sent) chunk_px = pixel_bytes - sent;
        uint8_t  chunk_h = (uint8_t)(chunk_px / bytes_per_row);

        uint8_t hdr[HDR];
        hdr[0] = (uint8_t)((uint16_t)x & 0xFF); hdr[1] = (uint8_t)((uint16_t)x >> 8);
        hdr[2] = (uint8_t)((uint16_t)cur_y & 0xFF); hdr[3] = (uint8_t)((uint16_t)cur_y >> 8);
        hdr[4] = w; hdr[5] = chunk_h; hdr[6] = 0; // rle=0

        // Build packet: header + pixel chunk
        uint16_t plen = (uint16_t)(HDR + chunk_px);
        if (!s_crc_ready) crc16_init_table();
        uint16_t crc  = 0xFFFF;
        uint8_t  pre[3] = { GPU_OP_BLIT_SPRITE, (uint8_t)(plen & 0xFF), (uint8_t)(plen >> 8) };
        for (int i = 0; i < 3; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^pre[i]]);
        for (uint32_t i = 0; i < HDR; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^hdr[i]]);
        for (uint32_t i = 0; i < chunk_px; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^pixels[sent+i]]);
        uint8_t crc_b[2] = { (uint8_t)(crc&0xFF), (uint8_t)(crc>>8) };

        gpu_wait_not_busy(0);
        uint8_t sync = GPU_SYNC_BYTE;
        gpu_hal_cs_assert();
        gpu_hal_spi_write(&sync, 1);
        gpu_hal_spi_write(pre, 3);
        gpu_hal_spi_write(hdr, HDR);
        gpu_hal_spi_write(pixels + sent, chunk_px);
        gpu_hal_spi_write(crc_b, 2);
        gpu_hal_cs_deassert();

        sent  += chunk_px;
        cur_y += (int16_t)chunk_h;
    }
}

void gpu_upload_vram(uint32_t byte_offset, const uint8_t *data, uint32_t byte_count) {
    // Split into chunks of UPLOAD_VRAM_DATA_MAX bytes each.
    // Each chunk is a complete UPLOAD_VRAM packet with its own CRC and offset field.
    // The GPU's handle_upload_vram() writes each chunk directly to g_vram+offset,
    // so chunks can arrive in any order and the final image is correct.
    uint32_t remaining = byte_count;
    uint32_t offset    = byte_offset;

    if (!s_crc_ready) crc16_init_table();

    while (remaining > 0) {
        uint32_t chunk = remaining < UPLOAD_VRAM_DATA_MAX ? remaining : UPLOAD_VRAM_DATA_MAX;

        // Build 9-byte header
        uint8_t hdr[UPLOAD_VRAM_HDR_SIZE];
        hdr[0] = (uint8_t)(offset);        hdr[1] = (uint8_t)(offset >> 8);
        hdr[2] = (uint8_t)(offset >> 16);  hdr[3] = (uint8_t)(offset >> 24);
        hdr[4] = (uint8_t)(chunk);         hdr[5] = (uint8_t)(chunk >> 8);
        hdr[6] = (uint8_t)(chunk >> 16);   hdr[7] = (uint8_t)(chunk >> 24);
        hdr[8] = 0; // rle_flag = 0 (Phase 3)

        uint16_t plen = (uint16_t)(UPLOAD_VRAM_HDR_SIZE + chunk);
        uint8_t  pre[3] = { GPU_OP_UPLOAD_VRAM,
                            (uint8_t)(plen & 0xFF),
                            (uint8_t)(plen >> 8) };

        // Compute CRC incrementally
        uint16_t crc = 0xFFFF;
        for (int i = 0; i < 3; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^pre[i]]);
        for (uint32_t i = 0; i < UPLOAD_VRAM_HDR_SIZE; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^hdr[i]]);
        for (uint32_t i = 0; i < chunk; i++)
            crc = (uint16_t)((crc<<8)^s_crc_table[(crc>>8)^data[i]]);
        uint8_t crc_b[2] = { (uint8_t)(crc&0xFF), (uint8_t)(crc>>8) };

        gpu_wait_not_busy(0);
        uint8_t sync = GPU_SYNC_BYTE;
        gpu_hal_cs_assert();
        gpu_hal_spi_write(&sync, 1);
        gpu_hal_spi_write(pre,   3);
        gpu_hal_spi_write(hdr,   UPLOAD_VRAM_HDR_SIZE);
        gpu_hal_spi_write(data,  chunk);
        gpu_hal_spi_write(crc_b, 2);
        gpu_hal_cs_deassert();

        data      += chunk;
        offset    += chunk;
        remaining -= chunk;
    }
}

void gpu_draw_vram_sprite(int16_t x, int16_t y, uint16_t w, uint16_t h,
                          uint32_t vram_offset, uint8_t transform_flags,
                          uint16_t palette_color) {
    uint8_t p[15];
    put16(p+0,  x); put16(p+2,  y);
    put16(p+4,  (int16_t)w); put16(p+6,  (int16_t)h);
    p[8]  = (uint8_t)(vram_offset);
    p[9]  = (uint8_t)(vram_offset >> 8);
    p[10] = (uint8_t)(vram_offset >> 16);
    p[11] = (uint8_t)(vram_offset >> 24);
    p[12] = transform_flags;
    put16(p+13, (int16_t)palette_color);
    gpu_send_command(GPU_OP_DRAW_VRAM_SPRITE, p, 15);
}

// =============================================================================
// Text
// =============================================================================
void gpu_render_text(int16_t x, int16_t y, uint8_t font_id, uint16_t color,
                     uint8_t scale, const char *text) {
    if (!text) return;
    uint16_t slen = 0;
    while (text[slen]) slen++;
    uint16_t plen = 8 + slen + 1; // header(8) + string + null terminator
    uint8_t p[8 + 256];
    if (plen > sizeof(p)) plen = sizeof(p); // truncate at 256 chars
    put16(p+0, x); put16(p+2, y);
    p[4] = font_id;
    put16(p+5, (int16_t)color);
    p[7] = scale;
    uint16_t copy_len = plen - 8;
    if (copy_len > slen) copy_len = slen;
    memcpy(p + 8, text, copy_len);
    p[8 + copy_len] = 0; // null terminator
    gpu_send_command(GPU_OP_RENDER_TEXT, p, plen);
}

// =============================================================================
// Queries
// =============================================================================
uint8_t gpu_get_status(void) {
    uint8_t r = 0xFF;
    gpu_query(GPU_OP_GET_STATUS, NULL, 0, &r, 1);
    return r;
}

uint8_t gpu_get_profile(void) {
    uint8_t r = 0xFF;
    gpu_query(GPU_OP_GET_PROFILE, NULL, 0, &r, 1);
    return r;
}

uint32_t gpu_get_vram_free(void) {
    uint8_t r[4] = {0};
    gpu_query(GPU_OP_GET_VRAM_FREE, NULL, 0, r, 4);
    return (uint32_t)r[0] | ((uint32_t)r[1]<<8) | ((uint32_t)r[2]<<16) | ((uint32_t)r[3]<<24);
}

uint32_t gpu_get_vram_used(void) {
    uint8_t r[4] = {0};
    gpu_query(GPU_OP_GET_VRAM_USED, NULL, 0, r, 4);
    return (uint32_t)r[0] | ((uint32_t)r[1]<<8) | ((uint32_t)r[2]<<16) | ((uint32_t)r[3]<<24);
}

uint32_t gpu_get_sram_free(void) {
    uint8_t r[4] = {0};
    gpu_query(GPU_OP_GET_SRAM_FREE, NULL, 0, r, 4);
    return (uint32_t)r[0] | ((uint32_t)r[1]<<8) | ((uint32_t)r[2]<<16) | ((uint32_t)r[3]<<24);
}

void gpu_get_version(uint8_t *major, uint8_t *minor, uint16_t *patch) {
    uint8_t r[4] = {0};
    gpu_query(GPU_OP_GET_VERSION, NULL, 0, r, 4);
    if (major) *major = r[0];
    if (minor) *minor = r[1];
    if (patch) *patch = (uint16_t)r[2] | ((uint16_t)r[3] << 8);
}

uint32_t gpu_get_capabilities(void) {
    uint8_t r[4] = {0};
    gpu_query(GPU_OP_GET_CAPABILITIES, NULL, 0, r, 4);
    return (uint32_t)r[0] | ((uint32_t)r[1]<<8) | ((uint32_t)r[2]<<16) | ((uint32_t)r[3]<<24);
}
