#ifndef _HAGL_GD_HAL_H
#define _HAGL_GD_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <hagl/backend.h>
#include <hagl/color.h>

/* HAL must provide display dimensions and depth. This HAL */
/* defaults to 320x240. Alternative dimensions can be passed */
/* using compiler flags. */
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH   (320)
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT  (180)
#endif
#define DISPLAY_DEPTH   (8)

/** HAL must provide typedef for colors. This HAL uses RGB888. */
typedef uint8_t hagl_color_t;

/**
 * @brief Initialize the backend
 */
void
hagl_hal_init(hagl_backend_t *backend);

/**
 * @brief Convert RGB to HAL color type
 *
 * This is used for HAL implementations which use some other pixel
 * format than RGB565.
 */
// static inline hagl_color_t hagl_hal_color(uint8_t r, uint8_t g, uint8_t b) {
//     return (r << 16) | (g << 8) | (b);
// }

#ifdef __cplusplus
}
#endif
#endif /* _HAGL_GD_HAL_H */