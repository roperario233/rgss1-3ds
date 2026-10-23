/*
 * viewport.c -- RGSS1 Viewport: a clipping rectangle with its own z, scroll
 * offset (ox, oy), colour, tone and flash. Sprites and windows created in it
 * are drawn inside it, in its slot of the screen's z order (graphics.c).
 * Tone, colour and flash are drawn over its rect once its contents are
 * (graphics.c, viewport_effects).
 */

#include <stdlib.h>

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/variable.h>

#include "gfx.h"

static void viewport_free(mrb_state* mrb, void* p)
{
    ttm_viewport* v = (ttm_viewport*)p;
    if (!v) return;
    if (!v->d.disposed) ttm_unregister(&v->d);
    free(v);
}

static const mrb_data_type viewport_type = { "Viewport", viewport_free };

static ttm_viewport* get(mrb_state* mrb, mrb_value self)
{
    ttm_viewport* v = (ttm_viewport*)mrb_data_get_ptr(mrb, self, &viewport_type);
    if (!v) mrb_raise(mrb, mrb_class_get(mrb, "RGSSError"), "uninitialized viewport");
    return v;
}

#define IV(name) mrb_intern_lit(mrb, name)

static mrb_value viewport_init(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    mrb_value rect;
    if (argc == 1 && ttm_rect_ptr(argv[0])) {
        ttm_rect_t* r = ttm_rect_ptr(argv[0]);
        rect = ttm_rect_new(mrb, r->x, r->y, r->w, r->h);
    } else if (argc == 4) {
        rect = ttm_rect_new(mrb, ttm_to_int(mrb, argv[0]), ttm_to_int(mrb, argv[1]),
                            ttm_to_int(mrb, argv[2]), ttm_to_int(mrb, argv[3]));
    } else {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Viewport.new(x, y, width, height) or (rect)");
    }
    ttm_viewport* v = (ttm_viewport*)calloc(1, sizeof(ttm_viewport));
    mrb_data_init(self, v, &viewport_type);
    ttm_register(&v->d, TTM_VIEWPORT, (struct RData*)mrb_obj_ptr(self));
    mrb_iv_set(mrb, self, IV("@rect"), rect);
    return self;
}

static mrb_value viewport_dispose(mrb_state* mrb, mrb_value self)
{
    ttm_viewport* v = get(mrb, self);
    if (!v->d.disposed) ttm_unregister(&v->d);
    return mrb_nil_value();
}
static mrb_value viewport_disposed(mrb_state* mrb, mrb_value self) { return mrb_bool_value(get(mrb, self)->d.disposed); }

static mrb_value viewport_rect(mrb_state* mrb, mrb_value self) { return mrb_iv_get(mrb, self, IV("@rect")); }
static mrb_value viewport_set_rect(mrb_state* mrb, mrb_value self)
{
    mrb_value r;
    mrb_get_args(mrb, "o", &r);
    mrb_iv_set(mrb, self, IV("@rect"), r);
    return r;
}

#define INT_PROP(field, expr_get, expr_set)                                             \
    static mrb_value viewport_get_##field(mrb_state* mrb, mrb_value self)              \
    { ttm_viewport* v = get(mrb, self); return expr_get; }                               \
    static mrb_value viewport_set_##field(mrb_state* mrb, mrb_value self)              \
    { mrb_value x; mrb_get_args(mrb, "o", &x); ttm_viewport* v = get(mrb, self); expr_set; return x; }

INT_PROP(z, mrb_int_value(mrb, v->d.z), ttm_set_z(&v->d, (int)ttm_to_int(mrb, x)))
INT_PROP(ox, mrb_int_value(mrb, v->ox), v->ox = (int)ttm_to_int(mrb, x))
INT_PROP(oy, mrb_int_value(mrb, v->oy), v->oy = (int)ttm_to_int(mrb, x))
INT_PROP(visible, mrb_bool_value(v->d.visible), v->d.visible = mrb_test(x))

/* color/tone: created on first use, kept as ivars */
static mrb_value lazy_iv(mrb_state* mrb, mrb_value self, const char* iv, const char* klass)
{
    mrb_sym s = mrb_intern_cstr(mrb, iv);
    mrb_value v = mrb_iv_get(mrb, self, s);
    if (mrb_nil_p(v)) {
        v = mrb_funcall(mrb, mrb_obj_value(mrb_class_get(mrb, klass)), "new", 4, mrb_fixnum_value(0),
                        mrb_fixnum_value(0), mrb_fixnum_value(0), mrb_fixnum_value(0));
        mrb_iv_set(mrb, self, s, v);
    }
    return v;
}
static mrb_value viewport_color(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@color", "Color"); }
static mrb_value viewport_tone(mrb_state* mrb, mrb_value self) { return lazy_iv(mrb, self, "@tone", "Tone"); }
static mrb_value viewport_set_color(mrb_state* mrb, mrb_value self)
{
    mrb_value c;
    mrb_get_args(mrb, "o", &c);
    mrb_iv_set(mrb, self, IV("@color"), c);
    return c;
}
static mrb_value viewport_set_tone(mrb_state* mrb, mrb_value self)
{
    mrb_value t;
    mrb_get_args(mrb, "o", &t);
    mrb_iv_set(mrb, self, IV("@tone"), t);
    return t;
}

static mrb_value viewport_flash(mrb_state* mrb, mrb_value self)
{
    mrb_value color;
    mrb_int duration;
    mrb_get_args(mrb, "oi", &color, &duration);
    ttm_viewport* v = get(mrb, self);
    ttm_color_bytes(color, v->flash);
    v->flash_duration = v->flash_count = duration > 0 ? (int)duration : 0;
    return mrb_nil_value();
}

static mrb_value viewport_update(mrb_state* mrb, mrb_value self)
{
    ttm_viewport* v = get(mrb, self);
    if (v->flash_count > 0) v->flash_count--;
    return mrb_nil_value();
}

void ttm_init_viewport(mrb_state* mrb)
{
    struct RClass* c = mrb_define_class(mrb, "Viewport", mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    mrb_define_method(mrb, c, "initialize", viewport_init, MRB_ARGS_ANY());
    mrb_define_method(mrb, c, "dispose", viewport_dispose, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "disposed?", viewport_disposed, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "rect", viewport_rect, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "rect=", viewport_set_rect, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "z", viewport_get_z, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "z=", viewport_set_z, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "ox", viewport_get_ox, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "ox=", viewport_set_ox, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "oy", viewport_get_oy, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "oy=", viewport_set_oy, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "visible", viewport_get_visible, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "visible=", viewport_set_visible, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "color", viewport_color, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "color=", viewport_set_color, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "tone", viewport_tone, MRB_ARGS_NONE());
    mrb_define_method(mrb, c, "tone=", viewport_set_tone, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, c, "flash", viewport_flash, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, c, "update", viewport_update, MRB_ARGS_NONE());
}
