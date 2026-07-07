#pragma once
#include <stdint.h>
#include <stdbool.h>

#define RING_BUFFER_SIZE           4096u
#define RING_BUFFER_BUSY_THRESHOLD 3276u  // 80% of 4096

// 4KB ring buffer. Written by DMA (PIO RX FIFO drain). Read by Core 0 packet parser.
// Power-of-two size enables DMA hardware ring-wrap with zero CPU overhead.

void     rb_init(void);
bool     rb_read_byte(uint8_t *out);
uint32_t rb_available(void);
bool     rb_is_busy(void);  // true when >= 80% full

// Called by PIO DMA completion handler to notify of new data
void     rb_dma_completion_handler(void);

// For DMA configuration: returns the ring buffer base address
uint8_t *rb_get_buffer(void);
