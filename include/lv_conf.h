#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

#define LV_STDINT_INCLUDE       <stdint.h>
#define LV_STDDEF_INCLUDE       <stddef.h>
#define LV_STDBOOL_INCLUDE      <stdbool.h>
#define LV_INTTYPES_INCLUDE     <inttypes.h>
#define LV_LIMITS_INCLUDE       <limits.h>
#define LV_STDARG_INCLUDE       <stdarg.h>

#define LV_MEM_SIZE (64 * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_MEM_ADR 0
 
#define LV_DEF_REFR_PERIOD  16      /* [ms] target ~60 FPS update rate */
#define LV_DPI_DEF 130
 
#define LV_USE_OS   LV_OS_NONE
 
#define LV_DRAW_BUF_STRIDE_ALIGN                1
#define LV_DRAW_BUF_ALIGN                       4
#define LV_DRAW_TRANSFORM_USE_MATRIX            0
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE    (24 * 1024)
 
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_ACCESS 0
#define LV_USE_ASSERT_OBJ       0
 
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Widgets */
#define LV_USE_BUTTON           1
#define LV_USE_LABEL            1
#define LV_USE_IMAGE            1
#define LV_USE_BAR              1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TABVIEW          1
#define LV_USE_ROLLER           1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1

/* Themes */
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_GROW   1

/* Fonts */
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_18    1
#define LV_FONT_MONTSERRAT_24    1
#define LV_FONT_DEFAULT          &lv_font_montserrat_14

#endif /*LV_CONF_H*/
