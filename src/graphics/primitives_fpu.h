#pragma once
#include <stdint.h>
#include "feature_flags.h"

#if FEATURE_FPU_PRIMITIVES
void handle_draw_primitive_fpu(const uint8_t *payload, uint16_t len);
#endif
