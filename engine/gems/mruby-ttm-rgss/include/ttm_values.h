/*
 * ttm_values.h -- the C side of RGSS's value classes (src/values.c), for other
 * engine gems (mruby-ttm-gfx reads a sprite's Rect, Color and Tone directly).
 * A gem's include/ directory is exported to the gems that depend on it.
 */
#ifndef TTM_VALUES_H
#define TTM_VALUES_H

#include <stdint.h>
#include <mruby.h>
#include <mruby/data.h>

typedef struct { double v[4]; } ttm_quad_t;            /* Color: r g b a; Tone: r g b gray */
typedef struct { mrb_int x, y, w, h; } ttm_rect_t;
typedef struct { int32_t dim, xs, ys, zs; int16_t* data; } ttm_table_t;

extern const mrb_data_type ttm_color_type;
extern const mrb_data_type ttm_tone_type;
extern const mrb_data_type ttm_rect_type;
extern const mrb_data_type ttm_table_type;

/* the struct behind a value, or NULL if v is not one (nil, wrong class) */
static inline ttm_rect_t* ttm_rect_ptr(mrb_value v)
{
    return (mrb_data_p(v) && DATA_TYPE(v) == &ttm_rect_type) ? (ttm_rect_t*)DATA_PTR(v) : NULL;
}
static inline ttm_quad_t* ttm_color_ptr(mrb_value v)
{
    return (mrb_data_p(v) && DATA_TYPE(v) == &ttm_color_type) ? (ttm_quad_t*)DATA_PTR(v) : NULL;
}
static inline ttm_quad_t* ttm_tone_ptr(mrb_value v)
{
    return (mrb_data_p(v) && DATA_TYPE(v) == &ttm_tone_type) ? (ttm_quad_t*)DATA_PTR(v) : NULL;
}
static inline ttm_table_t* ttm_table_ptr(mrb_value v)
{
    return (mrb_data_p(v) && DATA_TYPE(v) == &ttm_table_type) ? (ttm_table_t*)DATA_PTR(v) : NULL;
}

/* new Ruby objects from C */
mrb_value ttm_rect_new(mrb_state* mrb, mrb_int x, mrb_int y, mrb_int w, mrb_int h);
mrb_value ttm_color_new(mrb_state* mrb, double r, double g, double b, double a);

#endif
