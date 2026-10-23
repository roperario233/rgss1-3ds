/*
 * values.c -- RGSS1's value classes in C: Color, Tone, Rect, Table.
 *
 * Engine classes, not stand-ins: the game reads and writes them every frame
 * (Table#[] in passability and bush checks, Color/Tone/Rect on every sprite),
 * and the Ruby versions cost ~10% of a busy map's frame (M2 part 3 profile).
 *
 * Semantics follow the RGSS1 reference:
 *   Color  red/green/blue/alpha, Float, clamped to 0..255
 *   Tone   red/green/blue -255..255, gray 0..255, Float
 *   Rect   x/y/width/height, Integer
 *   Table  1-3 dimensions of signed 16-bit cells; [] is nil out of range
 * Marshal formats (_dump/_load) match RGSS's: Color/Tone are 4 little-endian
 * doubles, Table is five int32 (dim, x, y, z, count) then int16 cells.
 */

#include <stdint.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>

#include "ttm_values.h"

/* ---- shared helpers ------------------------------------------------------ */

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static mrb_int to_int(mrb_state* mrb, mrb_value v)
{
    if (mrb_integer_p(v)) return mrb_integer(v);
    if (mrb_float_p(v)) return (mrb_int)mrb_float(v);
    return mrb_as_int(mrb, v);
}

static double to_dbl(mrb_state* mrb, mrb_value v)
{
    if (mrb_float_p(v)) return mrb_float(v);
    if (mrb_integer_p(v)) return (double)mrb_integer(v);
    return mrb_as_float(mrb, v);
}

static void put_le_double(uint8_t* p, double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(u >> (8 * i));
}

static double get_le_double(const uint8_t* p)
{
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= (uint64_t)p[i] << (8 * i);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static int32_t get_le_i32(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static void put_le_i32(uint8_t* p, int32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)((uint32_t)v >> (8 * i));
}

/* (re)attach a fresh struct to self, freeing any previous one */
static void* reinit(mrb_state* mrb, mrb_value self, const mrb_data_type* t, size_t size)
{
    void* old = DATA_PTR(self);
    if (old) mrb_free(mrb, old);
    mrb_data_init(self, NULL, t);
    void* p = mrb_malloc(mrb, size);
    memset(p, 0, size);
    mrb_data_init(self, p, t);
    return p;
}

/* ---- Color / Tone: four clamped doubles ------------------------------------ */

typedef ttm_quad_t quad_t;

const mrb_data_type ttm_color_type = { "Color", mrb_free };
const mrb_data_type ttm_tone_type = { "Tone", mrb_free };
#define color_type ttm_color_type
#define tone_type ttm_tone_type

/* limits per component: Color 0..255 x4; Tone -255..255 x3, gray 0..255 */
static const double color_lo[4] = { 0, 0, 0, 0 }, color_hi[4] = { 255, 255, 255, 255 };
static const double tone_lo[4] = { -255, -255, -255, 0 }, tone_hi[4] = { 255, 255, 255, 255 };

static void quad_set(mrb_state* mrb, quad_t* q, const mrb_data_type* t, mrb_int argc,
                     const mrb_value* argv, double fourth_default)
{
    const double* lo = (t == &color_type) ? color_lo : tone_lo;
    const double* hi = (t == &color_type) ? color_hi : tone_hi;
    if (argc == 1 && mrb_data_p(argv[0]) && DATA_TYPE(argv[0]) == t) {
        *q = *(quad_t*)DATA_PTR(argv[0]);
        return;
    }
    if (argc != 0 && argc != 3 && argc != 4)
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "wrong number of arguments (%d for 3..4)", (int)argc);
    for (int i = 0; i < 4; ++i) {
        double d = (i < argc) ? to_dbl(mrb, argv[i]) : (i == 3 ? fourth_default : 0.0);
        q->v[i] = clampd(d, lo[i], hi[i]);
    }
}

#define QUAD(self, t) ((quad_t*)mrb_data_get_ptr(mrb, (self), (t)))

static mrb_value color_init(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    quad_t* q = (quad_t*)reinit(mrb, self, &color_type, sizeof(quad_t));
    quad_set(mrb, q, &color_type, argc, argv, 255.0);
    return self;
}

static mrb_value tone_init(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    quad_t* q = (quad_t*)reinit(mrb, self, &tone_type, sizeof(quad_t));
    quad_set(mrb, q, &tone_type, argc, argv, 0.0);
    return self;
}

static const mrb_data_type* quad_type_of(mrb_value self)
{
    return DATA_TYPE(self) == &tone_type ? &tone_type : &color_type;
}

static mrb_value quad_set_m(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    const mrb_data_type* t = quad_type_of(self);
    quad_set(mrb, QUAD(self, t), t, argc, argv, t == &color_type ? 255.0 : 0.0);
    return self;
}

static mrb_value quad_get(mrb_state* mrb, mrb_value self, int i)
{
    return mrb_float_value(mrb, QUAD(self, quad_type_of(self))->v[i]);
}

static mrb_value quad_put(mrb_state* mrb, mrb_value self, int i)
{
    mrb_value v;
    mrb_get_args(mrb, "o", &v);
    const mrb_data_type* t = quad_type_of(self);
    const double* lo = (t == &color_type) ? color_lo : tone_lo;
    const double* hi = (t == &color_type) ? color_hi : tone_hi;
    QUAD(self, t)->v[i] = clampd(to_dbl(mrb, v), lo[i], hi[i]);
    return v;
}

static mrb_value q_get0(mrb_state* mrb, mrb_value s) { return quad_get(mrb, s, 0); }
static mrb_value q_get1(mrb_state* mrb, mrb_value s) { return quad_get(mrb, s, 1); }
static mrb_value q_get2(mrb_state* mrb, mrb_value s) { return quad_get(mrb, s, 2); }
static mrb_value q_get3(mrb_state* mrb, mrb_value s) { return quad_get(mrb, s, 3); }
static mrb_value q_put0(mrb_state* mrb, mrb_value s) { return quad_put(mrb, s, 0); }
static mrb_value q_put1(mrb_state* mrb, mrb_value s) { return quad_put(mrb, s, 1); }
static mrb_value q_put2(mrb_state* mrb, mrb_value s) { return quad_put(mrb, s, 2); }
static mrb_value q_put3(mrb_state* mrb, mrb_value s) { return quad_put(mrb, s, 3); }

static mrb_value quad_eq(mrb_state* mrb, mrb_value self)
{
    mrb_value o;
    mrb_get_args(mrb, "o", &o);
    const mrb_data_type* t = quad_type_of(self);
    if (!mrb_data_p(o) || DATA_TYPE(o) != t) return mrb_false_value();
    return mrb_bool_value(memcmp(DATA_PTR(self), DATA_PTR(o), sizeof(quad_t)) == 0);
}

static mrb_value quad_copy(mrb_state* mrb, mrb_value self)
{
    mrb_value o;
    mrb_get_args(mrb, "o", &o);
    if (!mrb_data_p(o)) mrb_raise(mrb, E_TYPE_ERROR, "initialize_copy: wrong type");
    const mrb_data_type* t = DATA_TYPE(o);
    quad_t* q = (quad_t*)reinit(mrb, self, t, sizeof(quad_t));
    *q = *(quad_t*)DATA_PTR(o);
    return self;
}

static mrb_value quad_dump(mrb_state* mrb, mrb_value self)
{
    quad_t* q = QUAD(self, quad_type_of(self));
    mrb_value s = mrb_str_new(mrb, NULL, 32);
    for (int i = 0; i < 4; ++i) put_le_double((uint8_t*)RSTRING_PTR(s) + 8 * i, q->v[i]);
    return s;
}

static mrb_value quad_load(mrb_state* mrb, mrb_value klass, const mrb_data_type* t)
{
    mrb_value s;
    mrb_get_args(mrb, "S", &s);
    if (RSTRING_LEN(s) < 32) mrb_raise(mrb, E_ARGUMENT_ERROR, "_load: data too short");
    quad_t* q = (quad_t*)mrb_malloc(mrb, sizeof(quad_t));
    for (int i = 0; i < 4; ++i) q->v[i] = get_le_double((const uint8_t*)RSTRING_PTR(s) + 8 * i);
    return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_ptr(klass), q, t));
}

static mrb_value color_load(mrb_state* mrb, mrb_value k) { return quad_load(mrb, k, &color_type); }
static mrb_value tone_load(mrb_state* mrb, mrb_value k) { return quad_load(mrb, k, &tone_type); }

/* ---- Rect ---------------------------------------------------------------------- */

typedef ttm_rect_t rect_t;
const mrb_data_type ttm_rect_type = { "Rect", mrb_free };
#define rect_type ttm_rect_type
#define RECT(self) ((rect_t*)mrb_data_get_ptr(mrb, (self), &rect_type))

static void rect_set_args(mrb_state* mrb, rect_t* r, mrb_int argc, const mrb_value* argv)
{
    if (argc == 1 && mrb_data_p(argv[0]) && DATA_TYPE(argv[0]) == &rect_type) {
        *r = *(rect_t*)DATA_PTR(argv[0]);
        return;
    }
    if (argc != 0 && argc != 4)
        mrb_raisef(mrb, E_ARGUMENT_ERROR, "wrong number of arguments (%d for 4)", (int)argc);
    r->x = argc ? to_int(mrb, argv[0]) : 0;
    r->y = argc ? to_int(mrb, argv[1]) : 0;
    r->w = argc ? to_int(mrb, argv[2]) : 0;
    r->h = argc ? to_int(mrb, argv[3]) : 0;
}

static mrb_value rect_init(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    rect_t* r = (rect_t*)reinit(mrb, self, &rect_type, sizeof(rect_t));
    rect_set_args(mrb, r, argc, argv);
    return self;
}

static mrb_value rect_set(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    rect_set_args(mrb, RECT(self), argc, argv);
    return self;
}

static mrb_value rect_empty(mrb_state* mrb, mrb_value self)
{
    rect_t* r = RECT(self);
    r->x = r->y = r->w = r->h = 0;
    return self;
}

#define RECT_FIELD(name, f)                                                       \
    static mrb_value rect_get_##name(mrb_state* mrb, mrb_value self)             \
    { return mrb_int_value(mrb, RECT(self)->f); }                                 \
    static mrb_value rect_put_##name(mrb_state* mrb, mrb_value self)             \
    { mrb_value v; mrb_get_args(mrb, "o", &v); RECT(self)->f = to_int(mrb, v); return v; }
RECT_FIELD(x, x)
RECT_FIELD(y, y)
RECT_FIELD(width, w)
RECT_FIELD(height, h)

static mrb_value rect_eq(mrb_state* mrb, mrb_value self)
{
    mrb_value o;
    mrb_get_args(mrb, "o", &o);
    if (!mrb_data_p(o) || DATA_TYPE(o) != &rect_type) return mrb_false_value();
    return mrb_bool_value(memcmp(DATA_PTR(self), DATA_PTR(o), sizeof(rect_t)) == 0);
}

static mrb_value rect_copy(mrb_state* mrb, mrb_value self)
{
    mrb_value o;
    mrb_get_args(mrb, "o", &o);
    rect_t* r = (rect_t*)reinit(mrb, self, &rect_type, sizeof(rect_t));
    *r = *(rect_t*)mrb_data_get_ptr(mrb, o, &rect_type);
    return self;
}

static mrb_value rect_to_a(mrb_state* mrb, mrb_value self)
{
    rect_t* r = RECT(self);
    mrb_value v[4] = { mrb_int_value(mrb, r->x), mrb_int_value(mrb, r->y),
                       mrb_int_value(mrb, r->w), mrb_int_value(mrb, r->h) };
    return mrb_ary_new_from_values(mrb, 4, v);
}

/* ---- Table ----------------------------------------------------------------------- */

typedef ttm_table_t table_t;

static void table_free(mrb_state* mrb, void* p)
{
    table_t* t = (table_t*)p;
    if (t) mrb_free(mrb, t->data);
    mrb_free(mrb, t);
}
const mrb_data_type ttm_table_type = { "Table", table_free };
#define table_type ttm_table_type
#define TABLE(self) ((table_t*)mrb_data_get_ptr(mrb, (self), &table_type))

static void table_alloc(mrb_state* mrb, table_t* t, int32_t x, int32_t y, int32_t z, int dim)
{
    if (x < 0 || y < 0 || z < 0) mrb_raise(mrb, E_ARGUMENT_ERROR, "negative table size");
    t->dim = dim;
    t->xs = x; t->ys = y; t->zs = z;
    size_t n = (size_t)x * (size_t)y * (size_t)z;
    t->data = (int16_t*)mrb_malloc(mrb, n ? n * sizeof(int16_t) : 1);
    memset(t->data, 0, n * sizeof(int16_t));
}

static mrb_value table_init(mrb_state* mrb, mrb_value self)
{
    mrb_int x, y = 1, z = 1;
    mrb_int argc = mrb_get_args(mrb, "i|ii", &x, &y, &z);
    table_t* t = (table_t*)DATA_PTR(self);
    if (t) table_free(mrb, t);
    mrb_data_init(self, NULL, &table_type);
    t = (table_t*)mrb_malloc(mrb, sizeof(table_t));
    t->data = NULL;
    mrb_data_init(self, t, &table_type);
    table_alloc(mrb, t, (int32_t)x, (int32_t)y, (int32_t)z, (int)argc);
    return self;
}

static mrb_value table_resize(mrb_state* mrb, mrb_value self)
{
    mrb_int x, y = 1, z = 1;
    mrb_int argc = mrb_get_args(mrb, "i|ii", &x, &y, &z);
    table_t* t = TABLE(self);
    table_t old = *t;
    table_alloc(mrb, t, (int32_t)x, (int32_t)y, (int32_t)z, (int)argc);
    int32_t mx = old.xs < t->xs ? old.xs : t->xs;
    int32_t my = old.ys < t->ys ? old.ys : t->ys;
    int32_t mz = old.zs < t->zs ? old.zs : t->zs;
    for (int32_t k = 0; k < mz; ++k)
        for (int32_t j = 0; j < my; ++j)
            memcpy(t->data + ((size_t)k * t->ys + j) * t->xs,
                   old.data + ((size_t)k * old.ys + j) * old.xs, (size_t)mx * sizeof(int16_t));
    mrb_free(mrb, old.data);
    return self;
}

static mrb_value table_xsize(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, TABLE(self)->xs); }
static mrb_value table_ysize(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, TABLE(self)->ys); }
static mrb_value table_zsize(mrb_state* mrb, mrb_value self) { return mrb_int_value(mrb, TABLE(self)->zs); }

static mrb_value table_aref(mrb_state* mrb, mrb_value self)
{
    mrb_int x, y = 0, z = 0;
    mrb_get_args(mrb, "i|ii", &x, &y, &z);
    table_t* t = TABLE(self);
    if (x < 0 || y < 0 || z < 0 || x >= t->xs || y >= t->ys || z >= t->zs) return mrb_nil_value();
    return mrb_int_value(mrb, t->data[((size_t)z * t->ys + (size_t)y) * t->xs + (size_t)x]);
}

static mrb_value table_aset(mrb_state* mrb, mrb_value self)
{
    const mrb_value* argv;
    mrb_int argc;
    mrb_get_args(mrb, "*", &argv, &argc);
    if (argc < 2 || argc > 4) mrb_raise(mrb, E_ARGUMENT_ERROR, "wrong number of arguments");
    table_t* t = TABLE(self);
    mrb_int x = to_int(mrb, argv[0]);
    mrb_int y = argc > 2 ? to_int(mrb, argv[1]) : 0;
    mrb_int z = argc > 3 ? to_int(mrb, argv[2]) : 0;
    mrb_value v = argv[argc - 1];
    if (x < 0 || y < 0 || z < 0 || x >= t->xs || y >= t->ys || z >= t->zs) return v;
    t->data[((size_t)z * t->ys + (size_t)y) * t->xs + (size_t)x] = (int16_t)to_int(mrb, v);
    return v;
}

static mrb_value table_copy(mrb_state* mrb, mrb_value self)
{
    mrb_value o;
    mrb_get_args(mrb, "o", &o);
    table_t* src = (table_t*)mrb_data_get_ptr(mrb, o, &table_type);
    table_t* t = (table_t*)DATA_PTR(self);
    if (t) table_free(mrb, t);
    mrb_data_init(self, NULL, &table_type);
    t = (table_t*)mrb_malloc(mrb, sizeof(table_t));
    t->data = NULL;
    mrb_data_init(self, t, &table_type);
    table_alloc(mrb, t, src->xs, src->ys, src->zs, src->dim);
    memcpy(t->data, src->data, (size_t)src->xs * src->ys * src->zs * sizeof(int16_t));
    return self;
}

static mrb_value table_dump(mrb_state* mrb, mrb_value self)
{
    table_t* t = TABLE(self);
    size_t n = (size_t)t->xs * t->ys * t->zs;
    mrb_value s = mrb_str_new(mrb, NULL, 20 + n * 2);
    uint8_t* p = (uint8_t*)RSTRING_PTR(s);
    put_le_i32(p, t->dim); put_le_i32(p + 4, t->xs); put_le_i32(p + 8, t->ys);
    put_le_i32(p + 12, t->zs); put_le_i32(p + 16, (int32_t)n);
    for (size_t i = 0; i < n; ++i) {
        p[20 + 2 * i] = (uint8_t)((uint16_t)t->data[i]);
        p[21 + 2 * i] = (uint8_t)((uint16_t)t->data[i] >> 8);
    }
    return s;
}

static mrb_value table_load(mrb_state* mrb, mrb_value klass)
{
    mrb_value s;
    mrb_get_args(mrb, "S", &s);
    const uint8_t* p = (const uint8_t*)RSTRING_PTR(s);
    mrb_int len = RSTRING_LEN(s);
    if (len < 20) mrb_raise(mrb, E_ARGUMENT_ERROR, "Table._load: data too short");
    int32_t dim = get_le_i32(p), x = get_le_i32(p + 4), y = get_le_i32(p + 8), z = get_le_i32(p + 12);
    int32_t n = get_le_i32(p + 16);
    if (n < 0 || (int64_t)x * y * z != n || 20 + (int64_t)n * 2 > len)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Table._load: inconsistent data");
    table_t* t = (table_t*)mrb_malloc(mrb, sizeof(table_t));
    t->data = NULL;
    struct RData* d = mrb_data_object_alloc(mrb, mrb_class_ptr(klass), t, &table_type);
    table_alloc(mrb, t, x, y, z, dim);
    for (int32_t i = 0; i < n; ++i)
        t->data[i] = (int16_t)((uint16_t)p[20 + 2 * i] | ((uint16_t)p[21 + 2 * i] << 8));
    return mrb_obj_value(d);
}

/* ---- constructors for other engine gems ------------------------------------------ */

mrb_value ttm_rect_new(mrb_state* mrb, mrb_int x, mrb_int y, mrb_int w, mrb_int h)
{
    rect_t* r = (rect_t*)mrb_malloc(mrb, sizeof(rect_t));
    r->x = x; r->y = y; r->w = w; r->h = h;
    return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_get(mrb, "Rect"), r, &rect_type));
}

mrb_value ttm_color_new(mrb_state* mrb, double r, double g, double b, double a)
{
    quad_t* q = (quad_t*)mrb_malloc(mrb, sizeof(quad_t));
    q->v[0] = clampd(r, 0, 255); q->v[1] = clampd(g, 0, 255);
    q->v[2] = clampd(b, 0, 255); q->v[3] = clampd(a, 0, 255);
    return mrb_obj_value(mrb_data_object_alloc(mrb, mrb_class_get(mrb, "Color"), q, &color_type));
}

/* ---- registration ------------------------------------------------------------------ */

static struct RClass* data_class(mrb_state* mrb, const char* name)
{
    struct RClass* c = mrb_define_class(mrb, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(c, MRB_TT_CDATA);
    return c;
}

void ttm_init_values(mrb_state* mrb)
{
    struct RClass* color = data_class(mrb, "Color");
    struct RClass* tone = data_class(mrb, "Tone");
    struct RClass* rect = data_class(mrb, "Rect");
    struct RClass* table = data_class(mrb, "Table");

    mrb_define_method(mrb, color, "initialize", color_init, MRB_ARGS_ANY());
    mrb_define_method(mrb, tone, "initialize", tone_init, MRB_ARGS_ANY());
    struct RClass* quads[2] = { color, tone };
    for (int i = 0; i < 2; ++i) {
        struct RClass* c = quads[i];
        mrb_define_method(mrb, c, "set", quad_set_m, MRB_ARGS_ANY());
        mrb_define_method(mrb, c, "red", q_get0, MRB_ARGS_NONE());
        mrb_define_method(mrb, c, "green", q_get1, MRB_ARGS_NONE());
        mrb_define_method(mrb, c, "blue", q_get2, MRB_ARGS_NONE());
        mrb_define_method(mrb, c, i == 0 ? "alpha" : "gray", q_get3, MRB_ARGS_NONE());
        mrb_define_method(mrb, c, "red=", q_put0, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, "green=", q_put1, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, "blue=", q_put2, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, i == 0 ? "alpha=" : "gray=", q_put3, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, "==", quad_eq, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, "initialize_copy", quad_copy, MRB_ARGS_REQ(1));
        mrb_define_method(mrb, c, "_dump", quad_dump, MRB_ARGS_OPT(1));
    }
    mrb_define_class_method(mrb, color, "_load", color_load, MRB_ARGS_REQ(1));
    mrb_define_class_method(mrb, tone, "_load", tone_load, MRB_ARGS_REQ(1));

    mrb_define_method(mrb, rect, "initialize", rect_init, MRB_ARGS_ANY());
    mrb_define_method(mrb, rect, "set", rect_set, MRB_ARGS_ANY());
    mrb_define_method(mrb, rect, "empty", rect_empty, MRB_ARGS_NONE());
    mrb_define_method(mrb, rect, "x", rect_get_x, MRB_ARGS_NONE());
    mrb_define_method(mrb, rect, "y", rect_get_y, MRB_ARGS_NONE());
    mrb_define_method(mrb, rect, "width", rect_get_width, MRB_ARGS_NONE());
    mrb_define_method(mrb, rect, "height", rect_get_height, MRB_ARGS_NONE());
    mrb_define_method(mrb, rect, "x=", rect_put_x, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "y=", rect_put_y, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "width=", rect_put_width, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "height=", rect_put_height, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "==", rect_eq, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "initialize_copy", rect_copy, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, rect, "to_a", rect_to_a, MRB_ARGS_NONE());

    mrb_define_method(mrb, table, "initialize", table_init, MRB_ARGS_ARG(1, 2));
    mrb_define_method(mrb, table, "resize", table_resize, MRB_ARGS_ARG(1, 2));
    mrb_define_method(mrb, table, "xsize", table_xsize, MRB_ARGS_NONE());
    mrb_define_method(mrb, table, "ysize", table_ysize, MRB_ARGS_NONE());
    mrb_define_method(mrb, table, "zsize", table_zsize, MRB_ARGS_NONE());
    mrb_define_method(mrb, table, "[]", table_aref, MRB_ARGS_ARG(1, 2));
    mrb_define_method(mrb, table, "[]=", table_aset, MRB_ARGS_ARG(2, 2));
    mrb_define_method(mrb, table, "initialize_copy", table_copy, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, table, "_dump", table_dump, MRB_ARGS_OPT(1));
    mrb_define_class_method(mrb, table, "_load", table_load, MRB_ARGS_REQ(1));
}
