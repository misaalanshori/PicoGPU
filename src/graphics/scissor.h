#pragma once
// scissor.h — Clip rect (scissor) stack (spec §6.3, §5.2)
// 8-deep stack; all pixel writes test against the top rect.
// Empty stack = full-screen (no clipping).

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Clip rect type
// ---------------------------------------------------------------------------
typedef struct {
    int16_t x, y;   // top-left (inclusive)
    int16_t w, h;   // width and height; 0 on either axis = degenerate (clips all)
} clip_rect_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Reset stack to depth 0 (full-screen, no clipping).
void scissor_init(void);

// Intersect (x,y,w,h) with current top-of-stack, push result.
// Returns false and sets ERR_CLIP_STACK_OVERFLOW if stack is full (depth == 8).
bool scissor_push(int16_t x, int16_t y, int16_t w, int16_t h);

// Pop the top clip rect. Returns false and sets ERR_CLIP_STACK_UNDERFLOW on empty stack.
bool scissor_pop(void);

// Returns true if (x, y) is inside the current clip rect (or stack is empty).
bool scissor_test(int16_t x, int16_t y);

// Clip a horizontal span [*x0 .. *x1] to the current clip rect row y.
// Modifies *x0 and *x1 in-place. Returns false (caller should skip) if span is
// empty after clipping or y is outside the current clip rect.
bool scissor_clip_hspan(int16_t *x0, int16_t *x1, int16_t y);

// Return pointer to the top clip rect, or NULL if stack is empty.
const clip_rect_t *scissor_top(void);

// Current stack depth (0 = no clipping active).
int8_t scissor_depth(void);
