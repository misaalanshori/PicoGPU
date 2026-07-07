#pragma once
#include <stdint.h>
#include <stdbool.h>

// CRC-16/CCITT: poly 0x1021, init 0xFFFF, no reflection, no XOR out
uint16_t crc16_update(uint16_t crc, uint8_t byte);
uint16_t crc16_compute(const uint8_t *data, uint32_t len);

// Process ring buffer bytes, parse packets, call dispatch_command() for each valid packet.
// Returns number of commands dispatched.
uint32_t packets_process(void);

// Initialize packet parser state machine (also builds CRC table)
void packets_init(void);
