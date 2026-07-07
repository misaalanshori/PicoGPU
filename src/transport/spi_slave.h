#pragma once
#include <stdint.h>
#include <stdbool.h>

// Initialize PIO1 SM0 as SPI slave receiver + DMA chain to ring buffer.
// Sets up BUSY pin output.
void spi_slave_init(void);

// Send a response over MISO (software bit-bang, synchronized to SCK).
// Called after host performs BUSY-LOW + 50us delay and reasserts CS for MISO read.
void spi_slave_send_response(const uint8_t *data, uint32_t len);

// Assert/deassert BUSY pin (GP5). Called by ring_buffer and profiles.c.
void spi_set_busy(bool busy);
