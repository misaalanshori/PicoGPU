#pragma once
#include <stdint.h>
#include "feature_flags.h"

// DRAW_9PATCH (0x52) — 18-byte payload
void handle_draw_9patch(const uint8_t *payload, uint16_t len);
