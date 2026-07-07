#pragma once
// primitives.h — DRAW_PRIMITIVE (opcode 0x30) handler
// Integer-only sub-opcodes 0x01–0x0E, 0x10, 0x14 (spec §6.1).
// FPU sub-opcodes 0x0F, 0x11–0x13 are in primitives_fpu.c (Phase 2).

#include <stdint.h>

// Master handler — called by dispatch.c for opcode 0x30
void handle_draw_primitive(const uint8_t *payload, uint16_t len);

// FILL_SCREEN handler (opcode 0x31) — fast memset fill of entire back buffer
// Payload: color(2B LE)
void handle_fill_screen(const uint8_t *payload, uint16_t len);
