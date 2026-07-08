// event_buffer.c — 16-entry GPU event ring buffer (spec §5.5, TIP §7 Phase 2)
// Pushed by firmware subsystems (END_FRAME, VM, VRAM monitor).
// Drained by GET_EVENTS (0xE9) from the host via dispatch.c.

#include "event_buffer.h"
#include "pico/time.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Ring state (128 bytes SRAM total)
// ---------------------------------------------------------------------------
static gpu_event_t s_buf[EVENT_BUFFER_CAPACITY];
static uint8_t     s_head  = 0;   // index of oldest entry (read head)
static uint8_t     s_tail  = 0;   // index of next write slot
static uint8_t     s_count = 0;   // number of valid entries

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static inline uint16_t now_ms(void) {
    return (uint16_t)(time_us_64() / 1000u);
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void event_buffer_init(void) {
    memset(s_buf, 0, sizeof(s_buf));
    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
}

void event_buffer_push(uint8_t type, uint32_t payload) {
    if (s_count >= EVENT_BUFFER_CAPACITY) {
        // Buffer full: drop oldest entry, write OVERFLOW sentinel in its slot
        gpu_event_t overflow = {
            .event_type   = EVT_BUFFER_OVERFLOW,
            .reserved     = 0,
            .timestamp_ms = now_ms(),
            .payload      = 0,
        };
        s_buf[s_head] = overflow;
        // Advance head (oldest is now the overflow sentinel we just wrote)
        s_head = (s_head + 1u) & (EVENT_BUFFER_CAPACITY - 1u);
        // tail stays where it is (we consumed one slot and overwrote it)
        // count stays at capacity
        return;
    }

    gpu_event_t ev = {
        .event_type   = type,
        .reserved     = 0,
        .timestamp_ms = now_ms(),
        .payload      = payload,
    };
    s_buf[s_tail] = ev;
    s_tail  = (s_tail  + 1u) & (EVENT_BUFFER_CAPACITY - 1u);
    s_count++;
}

uint8_t event_buffer_drain(gpu_event_t *out, uint8_t max_count) {
    uint8_t n = (s_count < max_count) ? s_count : max_count;
    for (uint8_t i = 0; i < n; i++) {
        out[i] = s_buf[s_head];
        s_head  = (s_head + 1u) & (EVENT_BUFFER_CAPACITY - 1u);
        s_count--;
    }
    return n;
}

uint8_t event_buffer_count(void) {
    return s_count;
}
