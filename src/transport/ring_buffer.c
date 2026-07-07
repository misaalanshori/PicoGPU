#include "ring_buffer.h"
#include "hardware/dma.h"
#include <stddef.h>

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

// 4096-byte aligned so DMA hardware ring-wrap works without CPU intervention.
static uint8_t __attribute__((aligned(4096))) g_ring_buf[RING_BUFFER_SIZE];

// Write index updated by reading the DMA channel's current write address.
// This gives a zero-copy, interrupt-free path for normal reads.
static uint32_t g_rb_read_idx;

// DMA channel used by spi_slave.c — kept here so rb_available() can query it.
// spi_slave.c sets this via spi_slave_init() before any reads occur.
#define SPI_DMA_CHAN 0

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Read the DMA's current write address and convert to a ring index.
static inline uint32_t _dma_write_idx(void)
{
    uint32_t wa = dma_hw->ch[SPI_DMA_CHAN].write_addr;
    return (wa - (uint32_t)g_ring_buf) & (RING_BUFFER_SIZE - 1u);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void rb_init(void)
{
    g_rb_read_idx = 0;
    // Buffer contents don't matter; DMA will overwrite from the start.
}

uint8_t *rb_get_buffer(void)
{
    return g_ring_buf;
}

bool rb_read_byte(uint8_t *out)
{
    uint32_t write_idx = _dma_write_idx();
    if (g_rb_read_idx == write_idx) {
        return false;  // buffer empty
    }
    *out = g_ring_buf[g_rb_read_idx & (RING_BUFFER_SIZE - 1u)];
    g_rb_read_idx = (g_rb_read_idx + 1u) & (RING_BUFFER_SIZE - 1u);
    return true;
}

uint32_t rb_available(void)
{
    uint32_t write_idx = _dma_write_idx();
    return (write_idx - g_rb_read_idx) & (RING_BUFFER_SIZE - 1u);
}

bool rb_is_busy(void)
{
    return rb_available() >= RING_BUFFER_BUSY_THRESHOLD;
}

// Called from DMA IRQ0 if the caller chooses to use interrupt-based tracking.
// For the hardware-ring-wrap approach the DMA write address is read directly,
// so this is a no-op stub kept for API compatibility.
void rb_dma_completion_handler(void)
{
    // No-op: write position derived live from dma_hw->ch[SPI_DMA_CHAN].write_addr
}
