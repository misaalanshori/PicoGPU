#pragma once
// dispatch.h — Central opcode router for validated packets
// Called by packets.c after CRC passes. Routes to handler functions.
// Also manages the MISO TX buffer for query responses.

#include <stdint.h>

// Initialize dispatch layer (clears TX buffer).
void dispatch_init(void);

// Process a validated command: check state guards, route to handler, update status.
// Called by packets.c for every valid CRC-checked packet.
void dispatch_command(uint8_t opcode, const uint8_t *payload, uint16_t len);

// Queue a query response into the MISO TX buffer.
// Subsequent spi_slave_send_response() call will transmit it.
void dispatch_set_response(const uint8_t *data, uint32_t len);

// Retrieve the last queued response (for spi_slave to transmit).
const uint8_t *dispatch_get_response(uint32_t *out_len);
