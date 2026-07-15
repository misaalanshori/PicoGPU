// dispatch.c — Central opcode router (spec §5.2-5.5, TIP §5.2)
// Receives validated (opcode, payload, len) tuples from packets.c.
// Enforces coprocessor state guard (UNINIT/INIT/ACTIVE) before routing.
// Handles all query opcodes in-line; routes drawing commands to subsystems.

#include "dispatch.h"
#include "packets.h"
#include "../state/coprocessor_state.h"
#include "../state/event_buffer.h"
#include "../memory/profiles.h"
#include "../memory/arena.h"
#include "../graphics/effects.h"
#include "../graphics/primitives.h"
#include "../graphics/primitives_fpu.h"
#include "../graphics/blit.h"
#include "../graphics/text.h"
#include "../graphics/scissor.h"
#include "../graphics/region.h"
#include "../graphics/nine_patch.h"
#include "../assets/vram.h"
#include "../assets/vram_named.h"
#include "../assets/display_list.h"
#include "../hal/rp2350/display_rp2350.h"
#include "../transport/spi_slave.h"
#include "../transport/ring_buffer.h"
#include "error_codes.h"
#include "opcodes.h"
#include "feature_flags.h"

#include "pico/time.h"
#include <string.h>
#include <stdint.h>


// MISO response buffer (spec §5.5) — max response size is GET_EVENTS (variable, capped at 256B)
#define RESPONSE_BUF_SIZE 256u
static uint8_t  s_response_buf[RESPONSE_BUF_SIZE];
static uint32_t s_response_len = 0;

void dispatch_init(void) {
    s_response_len = 0;
    memset(s_response_buf, 0, sizeof(s_response_buf));
    scissor_init();
    event_buffer_init();
}

void dispatch_set_response(const uint8_t *data, uint32_t len) {
    if (len > RESPONSE_BUF_SIZE) len = RESPONSE_BUF_SIZE;
    memcpy(s_response_buf, data, len);
    s_response_len = len;
}

const uint8_t *dispatch_get_response(uint32_t *out_len) {
    *out_len = s_response_len;
    s_response_len = 0;
    return s_response_buf;
}

// Helper: put little-endian uint32 into response buffer at offset
static inline void put_u32le(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}
static inline void put_u16le(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
}

// =============================================================================
// Capabilities bitmask from FEATURE_* flags (spec §12.2)
// =============================================================================
static uint32_t build_capabilities(void) {
    uint32_t caps = 0;
#if FEATURE_TARGET_RP2350
    caps |= (1u << 0);
#endif
#if FEATURE_RGB565
    caps |= (1u << 1);
#endif
#if FEATURE_FPU_PRIMITIVES
    caps |= (1u << 2);
#endif
#if FEATURE_ARBITRARY_ROTATION
    caps |= (1u << 3);
#endif
#if FEATURE_VECTOR_FONTS
    caps |= (1u << 4);
#endif
#if FEATURE_PAWN_VM
    caps |= (1u << 5);
#endif
#if FEATURE_PAWN_FLOAT
    caps |= (1u << 6);
#endif
#if FEATURE_ST7796_COMPAT
    caps |= (1u << 7);
#endif
#if FEATURE_DITHERING
    caps |= (1u << 11);
#endif
#if FEATURE_DISPLAY_LIST
    caps |= (1u << 12);
#endif
#if FEATURE_NAMED_VRAM
    caps |= (1u << 13);
#endif
#if FEATURE_BEGIN_END_FRAME
    caps |= (1u << 14);
#endif
#if FEATURE_FRAME_STATS
    caps |= (1u << 15);
#endif
#if FEATURE_EVENT_BUFFER
    caps |= (1u << 16);
#endif
#if FEATURE_VM_PARALLEL_CORE1
    caps |= (1u << 18);
#endif
#if FEATURE_VM_COOPERATIVE
    caps |= (1u << 19);
#endif
    return caps;
}

// =============================================================================
// Query handlers (spec §5.5) — accepted in ALL states
// =============================================================================
static void handle_get_status(void) {
    uint8_t r = g_state.last_error;
    dispatch_set_response(&r, 1);
}

static void handle_get_profile(void) {
    uint8_t r = g_state.active_profile_id;
    dispatch_set_response(&r, 1);
}

static void handle_get_vram_free(void) {
    uint8_t buf[4];
    put_u32le(buf, vram_get_free());
    dispatch_set_response(buf, 4);
}

static void handle_get_vram_used(void) {
    uint8_t buf[4];
    put_u32le(buf, vram_get_used());
    dispatch_set_response(buf, 4);
}

static void handle_get_vm_state(void) {
    uint8_t buf[2] = { 0x00, 0x00 }; // VM not available (Phase 0/1)
    dispatch_set_response(buf, 2);
}

static void handle_get_sram_free(void) {
    uint8_t buf[4];
    put_u32le(buf, arena_free_bytes());
    dispatch_set_response(buf, 4);
}

static void handle_get_version(void) {
    uint8_t buf[4] = {
        GPU_VERSION_MAJOR,
        GPU_VERSION_MINOR,
        (uint8_t)(GPU_VERSION_PATCH & 0xFF),
        (uint8_t)(GPU_VERSION_PATCH >> 8),
    };
    dispatch_set_response(buf, 4);
}

static void handle_get_capabilities(void) {
    uint8_t buf[4];
    put_u32le(buf, build_capabilities());
    dispatch_set_response(buf, 4);
}

static void handle_get_frame_stats(void) {
    uint8_t buf[8];
    put_u16le(buf + 0, g_state.last_render_ms);
    buf[2] = g_state.ring_peak_pct;
    buf[3] = g_state.missed_frames;
    put_u32le(buf + 4, g_state.frame_count);
    dispatch_set_response(buf, 8);
}

static void handle_get_events(void) {
    // Drain event buffer and build variable-length response:
    //   B0      = count N
    //   B1..BN*8 = N × 8-byte event records
    gpu_event_t evts[EVENT_BUFFER_CAPACITY];
    uint8_t n = event_buffer_drain(evts, EVENT_BUFFER_CAPACITY);

    uint8_t *p = s_response_buf;
    *p++ = n;
    for (uint8_t i = 0; i < n; i++) {
        *p++ = evts[i].event_type;
        *p++ = evts[i].reserved;
        *p++ = (uint8_t)(evts[i].timestamp_ms & 0xFF);
        *p++ = (uint8_t)(evts[i].timestamp_ms >> 8);
        *p++ = (uint8_t)(evts[i].payload);
        *p++ = (uint8_t)(evts[i].payload >> 8);
        *p++ = (uint8_t)(evts[i].payload >> 16);
        *p++ = (uint8_t)(evts[i].payload >> 24);
    }
    s_response_len = (uint32_t)(1u + (uint32_t)n * 8u);
}

static void handle_get_font_metadata(const uint8_t *payload, uint16_t len) {
    // Payload: font_id(1B)
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint8_t font_id = payload[0];
    if (font_id == 0) {
        // 8×8 bitmap font
        uint8_t buf[7];
        put_u16le(buf + 0, 8);    // char_w
        put_u16le(buf + 2, 8);    // char_h
        put_u16le(buf + 4, 7);    // baseline
        buf[6] = 0;               // not scalable
        dispatch_set_response(buf, 7);
    } else {
        // Unknown font
        uint8_t buf[7] = {0};
        dispatch_set_response(buf, 7);
        coprocessor_set_error(ERR_INVALID_PARAM);
    }
}

// =============================================================================
// Drawing state command handlers
// =============================================================================
static void handle_set_chroma_key(const uint8_t *payload, uint16_t len) {
    if (len < 2) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    g_state.chroma_key_color = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    coprocessor_set_error(ERR_OK);
}

static void handle_enable_transparency(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    g_state.chroma_key_enabled = (payload[0] != 0);
    coprocessor_set_error(ERR_OK);
}

static void handle_swap_buffers(void) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    // Deferred: flip happens at next VBLANK in scanline callback (line==0)
    g_state.swap_pending = true;
    coprocessor_set_error(ERR_OK);
}

static void handle_swap_buffers_immediate(void) {
    if (!g_fb_back) { coprocessor_set_error(ERR_NOT_ACTIVE); return; }
    // Immediate: flip now (may tear)
    display_rp2350_swap_buffers();
    coprocessor_set_error(ERR_OK);
}

static void handle_enable_frame_stats(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    bool enabling = (payload[0] != 0);
    if (enabling && !g_state.frame_stats_enabled) {
        // Reset counters when re-enabling
        g_state.frame_count    = 0;
        g_state.last_render_ms = 0;
        g_state.ring_peak_pct  = 0;
        g_state.missed_frames  = 0;
    }
    g_state.frame_stats_enabled = enabling;
    coprocessor_set_error(ERR_OK);
}

// Phase 2: scissor, pixel format, frame lifecycle

static void handle_push_clip_rect(const uint8_t *payload, uint16_t len) {
    // Payload: x(2B LE), y(2B LE), w(2B LE), h(2B LE) — 8 bytes (spec §5.2)
    if (len < 8) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    int16_t x = (int16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8));
    int16_t y = (int16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8));
    int16_t w = (int16_t)((uint16_t)payload[4] | ((uint16_t)payload[5] << 8));
    int16_t h = (int16_t)((uint16_t)payload[6] | ((uint16_t)payload[7] << 8));
    if (!scissor_push(x, y, w, h)) return;  // error already set by scissor_push
    coprocessor_set_error(ERR_OK);
}

static void handle_pop_clip_rect(void) {
    if (!scissor_pop()) return;  // error already set by scissor_pop
    coprocessor_set_error(ERR_OK);
}

static void handle_set_pixel_format(const uint8_t *payload, uint16_t len) {
    // Payload: format_enum(1B) (spec §5.2)
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    uint8_t fmt = payload[0];

    // Validate: format high nibble must match active bpp_class
    //   0x0X = 8bpp, 0x1X = 16bpp, 0x2X = 4bpp, 0x3X = 24bpp
    uint8_t fmt_nibble = fmt >> 4;
    uint8_t state_nibble;
    switch (g_state.bpp_class) {
        case  8: state_nibble = 0; break;
        case 16: state_nibble = 1; break;
        case  4: state_nibble = 2; break;
        case 24: state_nibble = 3; break;
        default: coprocessor_set_error(ERR_NOT_ACTIVE); return;
    }
    if (fmt_nibble != state_nibble) {
        coprocessor_set_error(ERR_INVALID_PARAM);
        return;
    }

    if (!display_rp2350_set_pixel_format(fmt)) {
        coprocessor_set_error(ERR_UNSUPPORTED);
        return;
    }
    g_state.active_pixel_format = fmt;
    coprocessor_set_error(ERR_OK);
}

static void handle_begin_frame(void) {
    // Missed frame detection: if a previous deferred swap is still pending,
    // it means the VBLANK didn't fire before the next frame started.
    if (g_state.swap_pending && g_state.in_frame) {
        if (g_state.missed_frames < 255u) g_state.missed_frames++;
    }

    // Reset per-frame stats accumulators
    g_state.ring_peak_pct = 0;
    g_state.in_frame      = true;

    if (g_state.frame_stats_enabled) {
        g_state.frame_start_us = time_us_64();
    }
    coprocessor_set_error(ERR_OK);
}

static void handle_end_frame(void) {
    // Compute render time
    if (g_state.frame_stats_enabled && g_state.in_frame) {
        uint64_t delta_us  = time_us_64() - g_state.frame_start_us;
        g_state.last_render_ms = (uint16_t)(delta_us / 1000u);
    }

    // Increment frame counter
    g_state.frame_count++;

    // Double-buffer: schedule VBLANK swap (same as SWAP_BUFFERS)
    if (g_fb_back && g_fb_back != g_fb_front) {
        g_state.swap_pending = true;
    }

#if FEATURE_DEFERRED_DRAW
    // Single-deferred mode: flush the deferred command queue now
    if (g_deferred_queue && g_deferred_queue_write > 0) {
        uint32_t pos = 0;
        while (pos + 3u <= g_deferred_queue_write) {
            uint8_t  op   = g_deferred_queue[pos];
            uint16_t plen = (uint16_t)g_deferred_queue[pos+1] |
                            ((uint16_t)g_deferred_queue[pos+2] << 8);
            if (pos + 3u + plen > g_deferred_queue_write) break;
            dispatch_command(op, g_deferred_queue + pos + 3, plen);
            pos += 3u + plen;
        }
        g_deferred_queue_write = 0;  // reset queue
    }
#endif

    // Push EVT_FRAME_COMPLETE to event buffer
    event_buffer_push(EVT_FRAME_COMPLETE, g_state.frame_count);

    g_state.in_frame = false;
    coprocessor_set_error(ERR_OK);
}


static void handle_set_dither_mode(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    g_state.dither_mode = payload[0];
    coprocessor_set_error(ERR_OK);
}

static void handle_set_blend_mode(const uint8_t *payload, uint16_t len) {
    if (len < 1) { coprocessor_set_error(ERR_INVALID_PARAM); return; }
    g_state.blend_mode = payload[0];
    coprocessor_set_error(ERR_OK);
}

// =============================================================================
// dispatch_command — main entry point from packets.c
// =============================================================================
void dispatch_command(uint8_t opcode, const uint8_t *payload, uint16_t len) {
    // -------------------------------------------------------------------------
    // Query opcodes are accepted in ALL states (spec §5.5)
    // -------------------------------------------------------------------------
    switch (opcode) {
        case OP_GET_STATUS:         handle_get_status();                        return;
        case OP_GET_PROFILE:        handle_get_profile();                       return;
        case OP_GET_VRAM_FREE:      handle_get_vram_free();                     return;
        case OP_GET_VRAM_USED:      handle_get_vram_used();                     return;
        case OP_GET_VM_STATE:       handle_get_vm_state();                      return;
        case OP_GET_SRAM_FREE:      handle_get_sram_free();                     return;
        case OP_GET_VERSION:        handle_get_version();                       return;
        case OP_GET_CAPABILITIES:   handle_get_capabilities();                  return;
        case OP_GET_FRAME_STATS:    handle_get_frame_stats();                   return;
        case OP_GET_EVENTS:         handle_get_events();                        return;
        case OP_GET_FONT_METADATA:  handle_get_font_metadata(payload, len);     return;
        default: break;
    }

    // -------------------------------------------------------------------------
    // SYSTEM_CONFIG accepted in any state (spec §3.5)
    // -------------------------------------------------------------------------
    if (opcode == OP_SYSTEM_CONFIG) {
        handle_system_config(payload, len);
        return;
    }

    // -------------------------------------------------------------------------
    // SOFT_RESET accepted in any state
    // -------------------------------------------------------------------------
    if (opcode == OP_SOFT_RESET) {
        handle_soft_reset();
        return;
    }

    // -------------------------------------------------------------------------
    // All other commands: require ACTIVE state
    // -------------------------------------------------------------------------
    if (g_state.state != GPU_STATE_ACTIVE) {
        coprocessor_set_error(ERR_NOT_ACTIVE);
        return;
    }

    // -------------------------------------------------------------------------
    // Drawing state commands (no framebuffer access)
    // -------------------------------------------------------------------------
    switch (opcode) {
        case OP_SET_CHROMA_KEY:     handle_set_chroma_key(payload, len);         return;
        case OP_ENABLE_TRANSPARENCY:handle_enable_transparency(payload, len);    return;
        case OP_SET_DITHER_MODE:    handle_set_dither_mode(payload, len);        return;
        case OP_SET_BLEND_MODE:     handle_set_blend_mode(payload, len);         return;
        case OP_ENABLE_FRAME_STATS: handle_enable_frame_stats(payload, len);     return;
        case OP_SWAP_BUFFERS:       handle_swap_buffers();                       return;
        case OP_SWAP_BUFFERS_IMM:   handle_swap_buffers_immediate();             return;
        // Phase 2 drawing-state commands (require ACTIVE but not framebuffer)
        case OP_PUSH_CLIP_RECT:     handle_push_clip_rect(payload, len);         return;
        case OP_POP_CLIP_RECT:      handle_pop_clip_rect();                      return;
        case OP_SET_PIXEL_FORMAT:   handle_set_pixel_format(payload, len);       return;
        case OP_BEGIN_FRAME:        handle_begin_frame();                        return;
        case OP_END_FRAME:          handle_end_frame();                          return;
        default: break;
    }

    // -------------------------------------------------------------------------
    // Drawing commands (require active framebuffer)
    // -------------------------------------------------------------------------
    if (!g_fb_back) {
        coprocessor_set_error(ERR_NOT_ACTIVE);
        return;
    }

#if FEATURE_DISPLAY_LIST
    // -------------------------------------------------------------------------
    // Display list recording intercept:
    // If recording is active, serialise this command into VRAM instead of
    // executing it. Streaming commands and management opcodes are exempted
    // inside dl_record_command() itself.
    // -------------------------------------------------------------------------
    if (dl_is_recording()) {
        // BEGIN_DISPLAY_LIST during an active recording → forbidden; let the
        // handler report ERR_INVALID_PARAM by falling through to normal dispatch.
        if (opcode != OP_BEGIN_DISPLAY_LIST &&
            opcode != OP_END_DISPLAY_LIST) {
            if (dl_record_command(opcode, payload, len)) return;
            // dl_record_command returned false → not recorded; fall through
            // to execute normally (management / query opcodes).
        }
    }
#endif

#if FEATURE_DEFERRED_DRAW
    // -------------------------------------------------------------------------
    // Single-deferred mode intercept:
    // Small fixed-payload draw commands are enqueued rather than executed
    // immediately during active scanout. Streaming commands bypass this queue.
    // Only active when a BUFFERING_SINGLE_DEFERRED profile is running.
    // -------------------------------------------------------------------------
    if (g_deferred_queue != NULL &&
        opcode != OP_BLIT_SPRITE &&
        opcode != OP_UPLOAD_VRAM &&
        opcode != OP_LOAD_PROCEDURE) {
        // Enqueue: [opcode(1B)][len_lo(1B)][len_hi(1B)][payload(len)]
        uint32_t needed = 3u + (uint32_t)len;
        if (g_deferred_queue_write + needed <= g_deferred_queue_size) {
            uint8_t *p = g_deferred_queue + g_deferred_queue_write;
            p[0] = opcode;
            p[1] = (uint8_t)(len & 0xFF);
            p[2] = (uint8_t)(len >> 8);
            if (len > 0 && payload) __builtin_memcpy(p + 3, payload, len);
            g_deferred_queue_write += needed;
        } else {
            coprocessor_set_error(ERR_OVERFLOW);
        }
        return;
    }
#endif

    switch (opcode) {
        case OP_DRAW_PRIMITIVE:     handle_draw_primitive(payload, len);         return;
        case OP_FILL_SCREEN:        handle_fill_screen(payload, len);            return;
        // H3 NOTE — BLIT_SPRITE streaming limitation:
        // The spec (§5.7) envisions BLIT_SPRITE as a "streamed" command whose
        // pixel data drains directly from the ring buffer, allowing sprites up
        // to 255×255 pixels (65,025 bytes at 1bpp). This implementation instead
        // accumulates the ENTIRE payload into a 4 KB scratch buffer before
        // dispatching (packets.c). Payloads exceeding MAX_PAYLOAD_SIZE (4096 B)
        // are rejected with ERR_PAYLOAD_TOO_LARGE in the parser — so the
        // practical ceiling is ~4089 bytes of pixel data (6-byte header leaves
        // ~4089 B), approximately 63×63 px at 1bpp or ~45×45 px at 2bpp.
        // Unlike UPLOAD_VRAM / LOAD_PROCEDURE, BLIT_SPRITE has no byte_offset
        // field in its wire format, so there is no chunking workaround.
        // To support large BLIT_SPRITEs without PIO streaming: upload the
        // sprite data via UPLOAD_VRAM (chunked), then use DRAW_VRAM_SPRITE.
        case OP_BLIT_SPRITE:        handle_blit_sprite(payload, len);            return;
        case OP_DRAW_VRAM_SPRITE:   handle_draw_vram_sprite(payload, len);       return;
        case OP_RENDER_TEXT:        handle_render_text(payload, len);            return;
        case OP_UPLOAD_VRAM:        handle_upload_vram(payload, len);            return;

        // Phase 3: region ops
        case OP_COPY_REGION:        handle_copy_region(payload, len);            return;
        case OP_REPLACE_COLOR:      handle_replace_color(payload, len);          return;
        case OP_SCROLL_SCREEN:      handle_scroll_screen(payload, len);          return;
        case OP_DRAW_TILEMAP:       handle_draw_tilemap(payload, len);           return;

        // Phase 3: blit / asset
        case OP_DRAW_9PATCH:        handle_draw_9patch(payload, len);            return;
#if FEATURE_CAPTURE_REGION
        case OP_CAPTURE_REGION:     handle_capture_region(payload, len);         return;
#endif

        // Phase 3: named VRAM
#if FEATURE_NAMED_VRAM
        case OP_VRAM_ALLOC_NAMED:   handle_vram_alloc_named(payload, len);       return;
        case OP_VRAM_LOOKUP:        handle_vram_lookup(payload, len);            return;
        case OP_VRAM_FREE_NAMED:    handle_vram_free_named(payload, len);        return;
#endif

        // Phase 3: display lists
#if FEATURE_DISPLAY_LIST
        case OP_BEGIN_DISPLAY_LIST: handle_begin_display_list(payload, len);     return;
        case OP_END_DISPLAY_LIST:   handle_end_display_list();                   return;
        case OP_EXEC_DISPLAY_LIST:  handle_exec_display_list(payload, len);      return;
#endif

        // VM (Phase 4+)
        case OP_LOAD_PROCEDURE:
        case OP_EXEC_PROCEDURE:
        case OP_VM_RESET:
        case OP_SCHEDULE_PROCEDURE:
        case OP_UNSCHEDULE_PROCEDURE:
        case OP_SET_VM_MODE:
            coprocessor_set_error(ERR_FEATURE_UNAVAILABLE);
            return;

        default:
            coprocessor_set_error(ERR_UNKNOWN_OPCODE);
            return;
    }
}
