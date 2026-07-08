// scissor.c — 8-deep scissor (clip rect) stack (spec §6.3)
// All pixel writes in effects.c call scissor_test() as the first gate.
// PUSH intersects the incoming rect with the current top; child clips are always
// as tight as or tighter than their parent. Empty stack = full-screen.

#include "scissor.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"

#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
#define SCISSOR_STACK_DEPTH 8

static clip_rect_t s_stack[SCISSOR_STACK_DEPTH];
static int8_t      s_depth = 0;   // 0 = empty (full-screen)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Compute the intersection of two rects.
// Returns a degenerate rect (w=0 or h=0) if they do not overlap.
static clip_rect_t rect_intersect(const clip_rect_t *a, const clip_rect_t *b) {
    int16_t ax1 = a->x + a->w;   // a right  (exclusive)
    int16_t ay1 = a->y + a->h;   // a bottom (exclusive)
    int16_t bx1 = b->x + b->w;   // b right  (exclusive)
    int16_t by1 = b->y + b->h;   // b bottom (exclusive)

    int16_t ix0 = (a->x > b->x) ? a->x : b->x;
    int16_t iy0 = (a->y > b->y) ? a->y : b->y;
    int16_t ix1 = (ax1 < bx1)   ? ax1  : bx1;
    int16_t iy1 = (ay1 < by1)   ? ay1  : by1;

    clip_rect_t r;
    r.x = ix0;
    r.y = iy0;
    r.w = (ix1 > ix0) ? (ix1 - ix0) : 0;
    r.h = (iy1 > iy0) ? (iy1 - iy0) : 0;
    return r;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

void scissor_init(void) {
    s_depth = 0;
}

bool scissor_push(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (s_depth >= SCISSOR_STACK_DEPTH) {
        coprocessor_set_error(ERR_CLIP_STACK_OVERFLOW);
        return false;
    }

    clip_rect_t incoming = { x, y, w, h };

    if (s_depth == 0) {
        // No parent — incoming rect is the clip as-is
        s_stack[0] = incoming;
    } else {
        // Intersect with current top so child can never escape parent
        s_stack[s_depth] = rect_intersect(&s_stack[s_depth - 1], &incoming);
    }
    s_depth++;
    return true;
}

bool scissor_pop(void) {
    if (s_depth <= 0) {
        coprocessor_set_error(ERR_CLIP_STACK_UNDERFLOW);
        return false;
    }
    s_depth--;
    return true;
}

bool scissor_test(int16_t x, int16_t y) {
    if (s_depth == 0) return true;  // no clip = full screen passes
    const clip_rect_t *c = &s_stack[s_depth - 1];
    if (c->w == 0 || c->h == 0) return false;  // degenerate clip blocks everything
    return (x >= c->x) && (x < c->x + c->w) &&
           (y >= c->y) && (y < c->y + c->h);
}

bool scissor_clip_hspan(int16_t *x0, int16_t *x1, int16_t y) {
    if (s_depth == 0) return true;  // no clip; caller uses x0/x1 unchanged
    const clip_rect_t *c = &s_stack[s_depth - 1];

    if (c->w == 0 || c->h == 0) return false;
    if (y < c->y || y >= c->y + c->h) return false;

    int16_t cx1 = c->x + c->w - 1;
    if (*x0 < c->x)  *x0 = c->x;
    if (*x1 > cx1)   *x1 = cx1;
    return (*x0 <= *x1);
}

const clip_rect_t *scissor_top(void) {
    if (s_depth == 0) return NULL;
    return &s_stack[s_depth - 1];
}

int8_t scissor_depth(void) {
    return s_depth;
}
