// dispatch.c — Central opcode router (spec §5.2-5.5, TIP §5.2)
// Receives validated (opcode, payload, len) tuples from packets.c.
// Enforces coprocessor state guard (UNINIT/INIT/ACTIVE) before routing.
// Handles all query opcodes in-line; routes drawing commands to subsystems.

#include "dispatch.h"
#include "packets.h"
#include "../state/coprocessor_state.h"
#include "../memory/profiles.h"
#include "../memory/arena.h"
#include "../graphics/effects.h"
#include "../graphics/primitives.h"
#include "../graphics/blit.h"
#include "../graphics/text.h"
#include "../assets/vram.h"
#include "../hal/rp2350/display_rp2350.h"
#include "../transport/spi_slave.h"
#include "error_codes.h"
#include "opcodes.h"
#include "feature_flags.h"

#include <string.h>
#include <stdint.h>

// MISO response buffer (spec §5.5) — max response size is GET_EVENTS (variable, capped at 256B)
#define RESPONSE_BUF_SIZE 256u
static uint8_t  s_response_buf[RESPONSE_BUF_SIZE];
static uint32_t s_response_len = 0;

void dispatch_init(void) {
    s_response_len = 0;
    memset(s_response_buf, 0, sizeof(s_response_buf));
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
    // Phase 0/1: event buffer not implemented; return 0 events
    uint8_t buf[1] = { 0x00 };
    dispatch_set_response(buf, 1);
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
    g_state.frame_stats_enabled = (payload[0] != 0);
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
        default: break;
    }

    // -------------------------------------------------------------------------
    // Drawing commands (require active framebuffer)
    // -------------------------------------------------------------------------
    if (!g_fb_back) {
        coprocessor_set_error(ERR_NOT_ACTIVE);
        return;
    }

    switch (opcode) {
        case OP_DRAW_PRIMITIVE:     handle_draw_primitive(payload, len);         return;
        case OP_FILL_SCREEN:        handle_fill_screen(payload, len);            return;
        case OP_BLIT_SPRITE:        handle_blit_sprite(payload, len);            return;
        case OP_DRAW_VRAM_SPRITE:   handle_draw_vram_sprite(payload, len);       return;
        case OP_RENDER_TEXT:        handle_render_text(payload, len);            return;
        case OP_UPLOAD_VRAM:        handle_upload_vram(payload, len);            return;

        // Stubs for Phase 2+ opcodes — return ERR_FEATURE_UNAVAILABLE
        case OP_COPY_REGION:
        case OP_REPLACE_COLOR:
        case OP_SCROLL_SCREEN:
        case OP_DRAW_TILEMAP:
        case OP_DRAW_9PATCH:
        case OP_CAPTURE_REGION:
        case OP_PUSH_CLIP_RECT:
        case OP_POP_CLIP_RECT:
        case OP_BEGIN_DISPLAY_LIST:
        case OP_END_DISPLAY_LIST:
        case OP_EXEC_DISPLAY_LIST:
        case OP_VRAM_ALLOC_NAMED:
        case OP_VRAM_LOOKUP:
        case OP_VRAM_FREE_NAMED:
        case OP_LOAD_PROCEDURE:
        case OP_EXEC_PROCEDURE:
        case OP_VM_RESET:
        case OP_SCHEDULE_PROCEDURE:
        case OP_UNSCHEDULE_PROCEDURE:
        case OP_BEGIN_FRAME:
        case OP_END_FRAME:
        case OP_SET_PIXEL_FORMAT:
        case OP_SET_VM_MODE:
            coprocessor_set_error(ERR_FEATURE_UNAVAILABLE);
            return;

        default:
            coprocessor_set_error(ERR_UNKNOWN_OPCODE);
            return;
    }
}
