#ifndef _LIFE_H
#define _LIFE_H

#include <hagl.h>

void life_init(hagl_backend_t const *display);
void life_animate(hagl_backend_t const *display);
void life_render(hagl_backend_t const *display);
void life_close();

#endif
