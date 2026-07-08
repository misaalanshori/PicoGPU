#pragma once
// event_buffer.h — Unsolicited GPU event ring buffer (spec §5.5, TIP §7 Phase 2 item 7)
// 16-entry circular ring; events are pushed by firmware, drained by GET_EVENTS (0xE9).
// If the buffer is full when a new event arrives, the oldest entry is dropped and
// an EVT_BUFFER_OVERFLOW sentinel is written in its place.

#include <stdint.h>

// ---------------------------------------------------------------------------
// Event type codes (spec §5.5 GET_EVENTS table)
// ---------------------------------------------------------------------------
#define EVT_FRAME_COMPLETE    0x01u  // Pushed on every END_FRAME
#define EVT_VM_PROC_DONE      0x02u  // VM procedure execution finished
#define EVT_VRAM_NEARLY_FULL  0x03u  // VRAM usage crossed 80% threshold
#define EVT_ERROR             0x04u  // Error condition (error code in payload byte 0)
#define EVT_BUFFER_OVERFLOW   0xFFu  // Sentinel: events were dropped

// ---------------------------------------------------------------------------
// Event record — 8 bytes, matches GET_EVENTS wire format exactly
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  event_type;    // EVT_* code
    uint8_t  reserved;      // always 0x00
    uint16_t timestamp_ms;  // low 16 bits of (time_us_64() / 1000) at push time
    uint32_t payload;       // event-specific data
} gpu_event_t;              // 8 bytes

// Buffer capacity: 16 entries × 8 bytes = 128 bytes SRAM
#define EVENT_BUFFER_CAPACITY 16u

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Reset the event ring to empty.
void event_buffer_init(void);

// Push an event. If the buffer is full, the oldest entry is replaced with an
// EVT_BUFFER_OVERFLOW sentinel and the new event is discarded (drop-oldest policy).
void event_buffer_push(uint8_t type, uint32_t payload);

// Drain up to max_count events into out[]. Returns the number actually drained.
// Drained events are removed from the ring.
uint8_t event_buffer_drain(gpu_event_t *out, uint8_t max_count);

// Return the number of events currently in the buffer (0–16).
uint8_t event_buffer_count(void);
