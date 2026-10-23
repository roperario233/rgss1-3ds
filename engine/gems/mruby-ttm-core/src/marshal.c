/*
 * marshal.c -- Marshal.load and Marshal.dump for mruby: Ruby's marshal format
 * 4.8, which every RPG Maker XP data file (.rxdata) and save file uses.
 *
 * load -- supported: nil true false, Fixnum (i), Bignum (l), Float (f), Symbol
 * (: ;), String ("), Array ([), Hash ({ }), object (o), user _load (u), user
 * marshal_load (U), Struct (S), class/module (c m), extended (e), Regexp (/),
 * object links (@) and instance variables (I). It takes either the bytes as a
 * String or an open file, which is what RGSS passes.
 *
 * dump -- the writer, in the same format. It covers nil true false, Fixnum (i),
 * Bignum (l, at any size), Float (f, Ruby's own text), Symbol, String, Array,
 * Hash (with a default), Struct (S), object (o), user _dump (u), class/module
 * (c m) and object links (@), and it reproduces CRuby's bytes for all but the
 * single 1.8-ism listed next to it below; engine/pc/marshal_roundtrip.rb checks
 * that against bytes the real Ruby wrote.
 *
 * Every object the stream defines is registered in a link table in the order
 * the stream defines it (as Ruby's r_entry does), so '@' back-references --
 * shared and self-referencing objects -- resolve to the same object.
 *
 * GC: the link table holds every object created, and it is itself held in the
 * GC arena, so the arena is rewound after each child to keep it from growing
 * with the size of the data.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/variable.h>

typedef struct {
    mrb_state* mrb;
    const uint8_t* p;
    const uint8_t* end;
    mrb_value symbols;   /* Array of Symbol values, for ';' links */
    mrb_value objects;   /* Array of objects, for '@' links */
    int arena;
} reader;

static mrb_value r_object(reader* r);

static uint8_t r_byte(reader* r)
{
    mrb_state* mrb = r->mrb;
    if (r->p >= r->end) mrb_raise(mrb, E_ARGUMENT_ERROR, "marshal data too short");
    return *r->p++;
}

/* Ruby's w_long encoding */
static int64_t r_long(reader* r)
{
    int c = (int8_t)r_byte(r);
    if (c == 0) return 0;
    if (c > 0) {
        if (4 < c && c < 128) return c - 5;
        int64_t x = 0;
        for (int i = 0; i < c; ++i) x |= (int64_t)r_byte(r) << (8 * i);
        return x;
    }
    if (-129 < c && c < -4) return c + 5;
    c = -c;
    int64_t x = -1;
    for (int i = 0; i < c; ++i) {
        x &= ~((int64_t)0xff << (8 * i));
        x |= (int64_t)r_byte(r) << (8 * i);
    }
    return x;
}

static const uint8_t* r_bytes(reader* r, size_t* len)
{
    mrb_state* mrb = r->mrb;
    int64_t n = r_long(r);
    if (n < 0 || (int64_t)(r->end - r->p) < n)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "marshal data too short");
    const uint8_t* s = r->p;
    r->p += n;
    *len = (size_t)n;
    return s;
}

static mrb_int r_register(reader* r, mrb_value v)
{
    mrb_ary_push(r->mrb, r->objects, v);
    return RARRAY_LEN(r->objects) - 1;
}

static mrb_value r_symbol(reader* r)
{
    mrb_state* mrb = r->mrb;
    uint8_t t = r_byte(r);
    if (t == ':') {
        size_t len;
        const uint8_t* s = r_bytes(r, &len);
        mrb_value sym = mrb_symbol_value(mrb_intern(r->mrb, (const char*)s, len));
        mrb_ary_push(r->mrb, r->symbols, sym);
        return sym;
    }
    if (t == ';') {
        int64_t i = r_long(r);
        if (i < 0 || i >= RARRAY_LEN(r->symbols))
            mrb_raise(mrb, E_ARGUMENT_ERROR, "bad symbol link");
        return mrb_ary_ref(r->mrb, r->symbols, (mrb_int)i);
    }
    if (t == 'I') {            /* a symbol with an encoding ivar (Ruby 1.9+ dumps) */
        mrb_value sym = r_symbol(r);
        int64_t n = r_long(r);
        for (int64_t i = 0; i < n; ++i) {
            r_symbol(r);
            r_object(r);
        }
        return sym;
    }
    mrb_raisef(mrb, E_TYPE_ERROR, "marshal: expected a symbol, got type 0x%d", (int)t);
    return mrb_nil_value();
}

/* "RPG::Event::Page" -> the class, or ArgumentError like Ruby's Marshal */
static mrb_value path2class(reader* r, mrb_value name_sym)
{
    mrb_state* mrb = r->mrb;
    mrb_int len;
    const char* name = mrb_sym_name_len(mrb, mrb_symbol(name_sym), &len);
    mrb_value mod = mrb_obj_value(mrb->object_class);
    const char* p = name;
    const char* end = name + len;
    while (p < end) {
        const char* sep = p;
        while (sep < end && !(sep[0] == ':' && sep + 1 < end && sep[1] == ':')) ++sep;
        mrb_sym part = mrb_intern(mrb, p, (size_t)(sep - p));
        if (!mrb_const_defined(mrb, mod, part)) {
            /* %s on mrb_sym_name_len's pointer would run past the name: the
             * symbol pool has no NUL after it (it printed the next symbol too) */
            mrb_raisef(mrb, E_ARGUMENT_ERROR, "undefined class/module %S",
                       mrb_str_new(mrb, name, (size_t)len));
        }
        mod = mrb_const_get(mrb, mod, part);
        p = sep < end ? sep + 2 : end;
    }
    return mod;
}

static mrb_value r_float(reader* r)
{
    size_t len;
    const uint8_t* s = r_bytes(r, &len);
    char buf[64];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = '\0';                 /* 1.8 may append mantissa bytes after a NUL */
    double d;
    if (strcmp(buf, "inf") == 0) d = INFINITY;
    else if (strcmp(buf, "-inf") == 0) d = -INFINITY;
    else if (strcmp(buf, "nan") == 0) d = NAN;
    else d = strtod(buf, NULL);
    return mrb_float_value(r->mrb, d);
}

static mrb_value r_bignum(reader* r)
{
    mrb_state* mrb = r->mrb;
    uint8_t sign = r_byte(r);
    int64_t shorts = r_long(r);
    if (shorts < 0 || (int64_t)(r->end - r->p) < shorts * 2)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "marshal data too short");
    size_t nbytes = (size_t)shorts * 2;
    const uint8_t* b = r->p;
    r->p += nbytes;

    /* little-endian magnitude -> hex digits, most significant first */
    mrb_value hex = mrb_str_new(mrb, NULL, 0);
    static const char digits[] = "0123456789abcdef";
    int started = 0;
    for (size_t i = nbytes; i-- > 0;) {
        if (!started && b[i] == 0) continue;
        started = 1;
        char two[2] = { digits[b[i] >> 4], digits[b[i] & 15] };
        mrb_str_cat(mrb, hex, two, 2);
    }
    if (!started) mrb_str_cat_lit(mrb, hex, "0");
    mrb_value v = mrb_funcall(mrb, hex, "to_i", 1, mrb_fixnum_value(16));
    if (sign == '-') v = mrb_funcall(mrb, v, "-@", 0);
    return v;
}

/* a container's children are reachable through the link table; rewind the arena */
#define REWIND(r) mrb_gc_arena_restore((r)->mrb, (r)->arena)

static mrb_value r_object(reader* r)
{
    mrb_state* mrb = r->mrb;
    uint8_t t = r_byte(r);
    switch (t) {
    case '0': return mrb_nil_value();
    case 'T': return mrb_true_value();
    case 'F': return mrb_false_value();
    case 'i': return mrb_int_value(mrb, (mrb_int)r_long(r));

    case ':':
    case ';':
        r->p--;
        return r_symbol(r);

    case '@': {
        int64_t i = r_long(r);
        if (i < 0 || i >= RARRAY_LEN(r->objects))
            mrb_raise(mrb, E_ARGUMENT_ERROR, "bad object link");
        return mrb_ary_ref(mrb, r->objects, (mrb_int)i);
    }

    case 'I': {                /* object followed by its instance variables */
        mrb_value obj = r_object(r);
        int64_t n = r_long(r);
        for (int64_t i = 0; i < n; ++i) {
            mrb_value sym = r_symbol(r);
            mrb_value val = r_object(r);
            mrb_int len;
            const char* name = mrb_sym_name_len(mrb, mrb_symbol(sym), &len);
            /* E / encoding: string encodings, meaningless to mruby's byte strings */
            if (len > 0 && name[0] == '@' && mrb_object_p(obj))
                mrb_iv_set(mrb, obj, mrb_symbol(sym), val);
            REWIND(r);
        }
        return obj;
    }

    case '"': {
        size_t len;
        const uint8_t* s = r_bytes(r, &len);
        mrb_value str = mrb_str_new(mrb, (const char*)s, len);
        r_register(r, str);
        return str;
    }

    case 'f': {
        mrb_value f = r_float(r);
        r_register(r, f);
        return f;
    }

    case 'l': {
        mrb_value v = r_bignum(r);
        r_register(r, v);
        return v;
    }

    case '[': {
        int64_t n = r_long(r);
        mrb_value ary = mrb_ary_new_capa(mrb, (mrb_int)(n < 1024 ? n : 1024));
        r_register(r, ary);
        for (int64_t i = 0; i < n; ++i) {
            mrb_ary_push(mrb, ary, r_object(r));
            REWIND(r);
        }
        return ary;
    }

    case '{':
    case '}': {
        int64_t n = r_long(r);
        mrb_value h = mrb_hash_new(mrb);
        r_register(r, h);
        for (int64_t i = 0; i < n; ++i) {
            mrb_value k = r_object(r);
            mrb_value v = r_object(r);
            mrb_hash_set(mrb, h, k, v);
            REWIND(r);
        }
        if (t == '}') mrb_funcall(mrb, h, "default=", 1, r_object(r));
        return h;
    }

    case 'o': {
        mrb_value klass = path2class(r, r_symbol(r));
        mrb_value obj = mrb_funcall(mrb, klass, "allocate", 0);
        r_register(r, obj);
        int64_t n = r_long(r);
        for (int64_t i = 0; i < n; ++i) {
            mrb_value sym = r_symbol(r);
            mrb_iv_set(mrb, obj, mrb_symbol(sym), r_object(r));
            REWIND(r);
        }
        return obj;
    }

    case 'u': {                /* Table, Color, Tone: klass._load(bytes) */
        mrb_value klass = path2class(r, r_symbol(r));
        size_t len;
        const uint8_t* s = r_bytes(r, &len);
        mrb_value data = mrb_str_new(mrb, (const char*)s, len);
        mrb_value obj = mrb_funcall(mrb, klass, "_load", 1, data);
        r_register(r, obj);
        return obj;
    }

    case 'U': {
        mrb_value klass = path2class(r, r_symbol(r));
        mrb_value obj = mrb_funcall(mrb, klass, "allocate", 0);
        r_register(r, obj);
        mrb_funcall(mrb, obj, "marshal_load", 1, r_object(r));
        return obj;
    }

    case 'S': {
        mrb_value klass = path2class(r, r_symbol(r));
        mrb_int slot = r_register(r, mrb_nil_value());   /* placeholder until built */
        int64_t n = r_long(r);
        mrb_value vals = mrb_ary_new_capa(mrb, (mrb_int)n);
        mrb_ary_set(mrb, r->objects, slot, vals);          /* keeps vals alive */
        for (int64_t i = 0; i < n; ++i) {
            r_symbol(r);
            mrb_ary_push(mrb, vals, r_object(r));
        }
        mrb_value obj = mrb_funcall_argv(mrb, klass, mrb_intern_lit(mrb, "new"),
                                         RARRAY_LEN(vals), RARRAY_PTR(vals));
        mrb_ary_set(mrb, r->objects, slot, obj);
        return obj;
    }

    case 'c':
    case 'm': {
        /* a class or module *value*. The path here is a length-prefixed string,
         * with no ':' type byte and no symbol-table entry -- unlike the class
         * path next to 'o'/'u'/'S', which is a symbol. The game's Game_System
         * holds two of these (@default_formatter = Paragrapher::Formatter,
         * @default_artist = Paragrapher::Artist), so saving it is what exercises
         * this path. */
        size_t len;
        const uint8_t* s = r_bytes(r, &len);
        mrb_value sym = mrb_symbol_value(mrb_intern(r->mrb, (const char*)s, len));
        mrb_value mod = path2class(r, sym);
        r_register(r, mod);
        return mod;
    }

    case 'e': {
        mrb_value mod = path2class(r, r_symbol(r));
        mrb_value obj = r_object(r);
        mrb_funcall(mrb, obj, "extend", 1, mod);
        return obj;
    }

    case '/': {
        size_t len;
        const uint8_t* s = r_bytes(r, &len);
        mrb_value src = mrb_str_new(mrb, (const char*)s, len);
        int opts = r_byte(r);
        mrb_value re_class = mrb_obj_value(mrb_class_get(mrb, "Regexp"));
        mrb_value re = mrb_funcall(mrb, re_class, "new", 2, src, mrb_fixnum_value(opts));
        r_register(r, re);
        return re;
    }

    default:
        mrb_raisef(mrb, E_TYPE_ERROR, "marshal: unsupported type '%c' (0x%d)", (int)t, (int)t);
    }
    return mrb_nil_value();
}

/* ---- Marshal.dump: the writer -------------------------------------------------
 *
 * Ruby's format 4.8, and the bytes are CRuby's own for everything here: the
 * fixtures CRuby wrote (work/probe_marshal.rb -> work/marshal_fixtures/) come
 * back out of this writer byte for byte, checked by engine/pc/marshal_roundtrip.rb.
 * One place is deliberately not CRuby 3.x's bytes, to stay what RGSS 1.x
 * (32-bit Ruby 1.8) writes: strings carry no encoding instance variable (1.8 has
 * none; CRuby 3.x wraps a UTF-8 string in 'I' with :E -> true). Save data is
 * byte strings, so in practice even that does not come up.
 * Everything else is literal: Ruby's w_long, its float text (shortest exact form,
 * Ruby's own scientific notation), its symbol table, and its Integer line -- an
 * Integer outside [-2**30, 2**30-1] is a Bignum ('l'), because 2**30 is where
 * CRuby 3.x itself switches to one (work/probe_marshal.rb prints both sides of
 * that boundary), and it is also where 32-bit Ruby 1.8 draws it.
 *
 * Every object the stream defines is registered in the link table in the order
 * this writer defines it, which is the order the reader registers them, so a
 * shared or self-referencing object becomes an '@' link -- including Floats and
 * Bignums, which Ruby links, and not Fixnums or Symbols, which it does not
 * (work/probe_marshal.rb prints the '@' index CRuby writes for each case).
 * Without that, a $game_map (whose events hold @map, pointing back at the map)
 * would recurse until the stack died. A repeat is found by object_id -- by
 * identity, not by equality: two equal but separate strings have to stay two
 * strings.
 *
 * The game writes a save this way (096_TtM Util.rb, 083_Scene_Save.rb: twelve
 * objects into one file opened "wb", a Marshal stream each -- plus $ams) and
 * reads it back with thirteen Marshal.load(file) calls. Structs go out as 'S'
 * (RPG::AudioFile is one in RGSS), Table/Color/Tone through their own _dump.
 */

typedef struct {
    mrb_state* mrb;
    mrb_value out;       /* the stream being built */
    mrb_value syms;      /* every symbol written, for ';' links */
    mrb_value objs;      /* the link table, for '@' links */
    mrb_value ids;       /* object_id -> link index, so a repeat is O(1) */
} writer;

static void w_object(writer* w, mrb_value obj);
static void w_class_path(writer* w, mrb_value obj);
static void w_array(writer* w, mrb_value obj);
static void w_hash(writer* w, mrb_value obj);
static void w_ivars(writer* w, mrb_value obj);

static void w_bytes(writer* w, const void* p, size_t n)
{
    if (n) mrb_str_cat(w->mrb, w->out, (const char*)p, n);
}

static void w_byte(writer* w, int b)
{
    char c = (char)b;
    w_bytes(w, &c, 1);
}

/* Ruby's w_long: 0 as a zero byte; 0..122 and -123..-1 as one byte; anything
 * else as a signed byte count and that many little-endian bytes (the reader's
 * r_long reads exactly this back). */
static void w_long(writer* w, int64_t x)
{
    if (x == 0) { w_byte(w, 0); return; }
    if (0 < x && x < 123) { w_byte(w, (int)(x + 5)); return; }
    if (-124 < x && x < 0) { w_byte(w, (int)((x - 5) & 0xff)); return; }
    uint8_t buf[8];
    int n = 0;
    int64_t v = x;
    while (v != 0 && v != -1) { buf[n++] = (uint8_t)(v & 0xff); v >>= 8; }
    w_byte(w, x < 0 ? -n : n);
    w_bytes(w, buf, (size_t)n);
}

/* A symbol is written once (':' + bytes) and then by index (';' + index), like
 * Ruby's symbol table; instance variable names go through it too. */
static void w_symbol(writer* w, mrb_value sym)
{
    mrb_state* mrb = w->mrb;
    mrb_sym s = mrb_symbol(sym);
    mrb_int n = RARRAY_LEN(w->syms);
    for (mrb_int i = 0; i < n; ++i)
        if (mrb_symbol(RARRAY_PTR(w->syms)[i]) == s) {
            w_byte(w, ';');
            w_long(w, (int64_t)i);
            return;
        }
    mrb_int len;
    const char* name = mrb_sym_name_len(mrb, s, &len);
    w_byte(w, ':');
    w_long(w, (int64_t)len);
    w_bytes(w, name, (size_t)len);
    mrb_ary_push(mrb, w->syms, sym);
}

static void w_register(writer* w, mrb_value v)
{
    mrb_state* mrb = w->mrb;
    mrb_int i = RARRAY_LEN(w->objs);
    mrb_ary_push(mrb, w->objs, v);
    mrb_hash_set(mrb, w->ids, mrb_int_value(mrb, mrb_obj_id(v)), mrb_int_value(mrb, i));
}

/* '@' + index when this exact object is already in the stream; 1 if written */
static int w_linked(writer* w, mrb_value v)
{
    mrb_state* mrb = w->mrb;
    mrb_value found = mrb_hash_get(mrb, w->ids, mrb_int_value(mrb, mrb_obj_id(v)));
    if (mrb_nil_p(found)) return 0;
    w_byte(w, '@');
    w_long(w, (int64_t)mrb_integer(found));
    return 1;
}

/* 'l': sign, word count, then the magnitude little-endian in 16-bit words --
 * exactly what r_bignum reads back, and what RGSS (32-bit Ruby) writes for any
 * Integer past 2**30. The magnitude comes from Ruby's own to_s(16), so it is
 * exact at any size and does not depend on how wide mrb_int is on this build: on
 * the 3DS (32-bit mrb_int) an Integer past 2**30 arrives as a mruby-bigint, on
 * the PC (64-bit) as an INTEGER, and both come out as the same bytes. */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static void w_bignum(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value hex = mrb_funcall(mrb, obj, "to_s", 1, mrb_fixnum_value(16));
    const char* p = RSTRING_PTR(hex);
    int len = (int)RSTRING_LEN(hex);
    int neg = len > 0 && p[0] == '-';
    if (neg) { ++p; --len; }
    int nb = (len + 1) / 2;                       /* one byte per hex pair */
    uint8_t* buf = (uint8_t*)mrb_malloc(mrb, (size_t)nb + 1);
    for (int i = 0; i < nb; ++i) {                /* least significant byte first */
        int hi = len - 2 * (i + 1);               /* odd digit count: one short */
        buf[i] = (uint8_t)((hi >= 0 ? hexval(p[hi]) << 4 : 0) | hexval(p[hi + 1]));
    }
    if (nb & 1) buf[nb++] = 0;                    /* whole words, as Ruby counts */
    w_byte(w, 'l');
    w_byte(w, neg ? '-' : '+');
    w_long(w, (int64_t)(nb / 2));
    w_bytes(w, buf, (size_t)nb);
    mrb_free(mrb, buf);
}

/* 'f': the value as text, length-prefixed -- Ruby's own dtoa text: the shortest
 * decimal that reads back as the same double, in Ruby's notation (scientific
 * when the point sits more than 3 places left of the digits or past them, no
 * '+' and no zero padding in the exponent, "0" for zero). The digits come from
 * trying %.1g..%.17g and keeping the first that strtod reads back exactly; the
 * notation is then rewritten to Ruby's rule (decpt < -3 || decpt > digits). The
 * bytes match CRuby's Marshal.dump for every value in the fixture set
 * (work/probe_marshal.rb, engine/pc/marshal_roundtrip.rb). */
static void w_float(writer* w, mrb_value obj)
{
    double d = mrb_float(obj);
    char text[64];
    if (isinf(d)) { strcpy(text, d < 0 ? "-inf" : "inf"); }
    else if (isnan(d)) { strcpy(text, "nan"); }
    else {
        char buf[64];
        int prec;
        for (prec = 1; prec <= 17; ++prec) {          /* shortest exact form */
            snprintf(buf, sizeof(buf), "%.*g", prec, d);
            if (strtod(buf, NULL) == d) break;
        }
        if (prec > 17) snprintf(buf, sizeof(buf), "%.17g", d);
        /* sign, mantissa digits and the position of the decimal point */
        const char* p = buf;
        char digits[32];
        int nd = 0, decpt = 0, exp = 0, seen_point = 0, int_digits = 0;
        int neg = (*p == '-');
        if (*p == '-' || *p == '+') ++p;
        for (; *p && *p != 'e' && *p != 'E'; ++p) {
            if (*p == '.') { seen_point = 1; continue; }
            digits[nd++] = *p;
            if (!seen_point) ++int_digits;
        }
        if (*p == 'e' || *p == 'E') exp = (int)strtol(p + 1, NULL, 10);
        decpt = int_digits + exp;
        while (nd > 1 && digits[nd - 1] == '0') --nd;  /* 100 -> 1e2 */
        if (nd == 1 && digits[0] == '0') decpt = 1;    /* zero: "0", not "0e0" */
        int n = 0;
        if (neg) text[n++] = '-';
        if (decpt < -3 || decpt > nd) {                /* Ruby's scientific rule */
            text[n++] = digits[0];
            if (nd > 1) {
                text[n++] = '.';
                memcpy(text + n, digits + 1, (size_t)(nd - 1));
                n += nd - 1;
            }
            n += snprintf(text + n, sizeof(text) - (size_t)n, "e%d", decpt - 1);
        } else if (decpt <= 0) {
            text[n++] = '0';
            text[n++] = '.';
            for (int i = 0; i < -decpt; ++i) text[n++] = '0';
            memcpy(text + n, digits, (size_t)nd);
            n += nd;
        } else {
            for (int i = 0; i < decpt; ++i) text[n++] = i < nd ? digits[i] : '0';
            if (decpt < nd) {
                text[n++] = '.';
                memcpy(text + n, digits + decpt, (size_t)(nd - decpt));
                n += nd - decpt;
            }
        }
        text[n] = '\0';
    }
    w_byte(w, 'f');
    w_long(w, (int64_t)strlen(text));
    w_bytes(w, text, strlen(text));
}

/* the path of a class or module object -- "Game_Map", "RPG::Event::Page" -- or a
 * clear error: an anonymous one has no path for the stream to name */
static mrb_value class_name(mrb_state* mrb, mrb_value cls)
{
    mrb_value name = mrb_funcall(mrb, cls, "to_s", 0);
    if (RSTRING_LEN(name) > 0 && RSTRING_PTR(name)[0] == '#')
        mrb_raisef(mrb, E_TYPE_ERROR, "marshal: can't dump an anonymous class or module (%v)", cls);
    return name;
}

/* the class path as Ruby writes it next to 'o', 'u', 'S' and 'U': a symbol, so
 * it shares the symbol table with the instance variable names in the stream */
static void w_class_path(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value name = class_name(mrb, mrb_obj_value(mrb_obj_class(mrb, obj)));
    w_symbol(w, mrb_symbol_value(mrb_intern_str(mrb, name)));
}

/* ...but next to 'c' and 'm' (a class or module *value*, like the two
 * Game_System holds) the path is a plain length-prefixed string, with no ':'
 * type byte and no entry in the symbol table -- Ruby's format, and the reason a
 * save of $game_system is where the difference shows. Here obj *is* the class. */
static void w_class_bytes(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value name = class_name(mrb, obj);
    w_byte(w, mrb_type(obj) == MRB_TT_MODULE ? 'm' : 'c');
    w_long(w, (int64_t)RSTRING_LEN(name));
    w_bytes(w, RSTRING_PTR(name), (size_t)RSTRING_LEN(name));
}

/* 'S': member count, then a (member name, value) pair per member, in the order
 * the class declares them -- what the reader replays through klass.new(*values).
 * RGSS's RPG::AudioFile is a Struct, and so are the game's own ones
 * (110_modern algebra's Formatted_Text, Paragrapher). */
static void w_struct(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value members = mrb_funcall(mrb, obj, "members", 0);
    int mark = mrb_gc_arena_save(mrb);
    mrb_int n = RARRAY_LEN(members);
    w_long(w, (int64_t)n);
    for (mrb_int i = 0; i < n; ++i) {
        mrb_value nm = RARRAY_PTR(members)[i];
        mrb_sym sym = mrb_symbol_p(nm) ? mrb_symbol(nm)
                                       : mrb_intern_str(mrb, mrb_obj_as_string(mrb, nm));
        w_symbol(w, mrb_symbol_value(sym));
        w_object(w, mrb_funcall(mrb, obj, "[]", 1, mrb_int_value(mrb, i)));
        mrb_gc_arena_restore(mrb, mark);
    }
}

/* '[', count, then the elements. Children are reachable through the link table,
 * so the arena can be rewound after each one -- the same trick the reader uses
 * to stay flat on a big save. */
static void w_array(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_int n = RARRAY_LEN(obj);
    int mark = mrb_gc_arena_save(mrb);
    w_byte(w, '[');
    w_long(w, (int64_t)n);
    for (mrb_int i = 0; i < n; ++i) {
        w_object(w, RARRAY_PTR(obj)[i]);
        mrb_gc_arena_restore(mrb, mark);
    }
}

/* '{' + count + pairs, or '}' + count + pairs + the default value when the hash
 * has one (the reader calls default= for '}' exactly like this) */
static void w_hash(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value keys = mrb_funcall(mrb, obj, "keys", 0);
    mrb_value dflt = mrb_funcall(mrb, obj, "default", 0);
    int mark = mrb_gc_arena_save(mrb);
    mrb_int n = RARRAY_LEN(keys);
    w_byte(w, mrb_nil_p(dflt) ? '{' : '}');
    w_long(w, (int64_t)n);
    for (mrb_int i = 0; i < n; ++i) {
        mrb_value k = RARRAY_PTR(keys)[i];
        w_object(w, k);
        w_object(w, mrb_hash_get(mrb, obj, k));
        mrb_gc_arena_restore(mrb, mark);
    }
    if (!mrb_nil_p(dflt)) w_object(w, dflt);
}

/* 'o' carries the instance variables, in the order mruby reports them; the
 * reader sets them back in that order */
static void w_ivars(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;
    mrb_value names = mrb_funcall(mrb, obj, "instance_variables", 0);
    int mark = mrb_gc_arena_save(mrb);
    mrb_int n = RARRAY_LEN(names);
    w_long(w, (int64_t)n);
    for (mrb_int i = 0; i < n; ++i) {
        mrb_value nm = RARRAY_PTR(names)[i];
        mrb_sym sym = mrb_symbol_p(nm) ? mrb_symbol(nm)
                                       : mrb_intern_str(mrb, mrb_obj_as_string(mrb, nm));
        w_symbol(w, mrb_symbol_value(sym));
        w_object(w, mrb_iv_get(mrb, obj, sym));
        mrb_gc_arena_restore(mrb, mark);
    }
}

static void w_object(writer* w, mrb_value obj)
{
    mrb_state* mrb = w->mrb;

    /* user marshal first, exactly where Ruby checks it: an object whose class
     * defines _dump writes itself (RGSS's Table, Color and Tone do) */
    if (!mrb_immediate_p(obj) && mrb_respond_to(mrb, obj, mrb_intern_lit(mrb, "_dump"))) {
        mrb_value data;
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_byte(w, 'u');
        w_class_path(w, obj);
        data = mrb_funcall(mrb, obj, "_dump", 1, mrb_int_value(mrb, 0));
        if (!mrb_string_p(data))
            mrb_raise(mrb, E_TYPE_ERROR, "marshal: _dump did not return a String");
        w_long(w, (int64_t)RSTRING_LEN(data));
        w_bytes(w, RSTRING_PTR(data), (size_t)RSTRING_LEN(data));
        return;
    }

    switch (mrb_type(obj)) {
    case MRB_TT_FALSE:
        w_byte(w, mrb_nil_p(obj) ? '0' : 'F');
        return;
    case MRB_TT_TRUE:
        w_byte(w, 'T');
        return;
    case MRB_TT_INTEGER: {
        /* Ruby's own line, verified against it: an Integer outside
         * [-2**30, 2**30-1] is written as a Bignum, so the bytes do not depend
         * on how wide this engine's mrb_int is either */
        int64_t x = (int64_t)mrb_integer(obj);
        if (x >= -((int64_t)1 << 30) && x < ((int64_t)1 << 30)) {
            w_byte(w, 'i');
            w_long(w, x);
        } else {
            /* Ruby links Bignums (they are objects) but not Fixnums -- the link
             * table has to hold them in the same order on both sides, or an
             * '@' index would point at the wrong entry */
            if (w_linked(w, obj)) return;
            w_register(w, obj);
            w_bignum(w, obj);
        }
        return;
    }
    case MRB_TT_FLOAT:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_float(w, obj);
        return;
    case MRB_TT_SYMBOL:
        w_symbol(w, obj);
        return;
    case MRB_TT_STRING:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_byte(w, '"');
        w_long(w, (int64_t)RSTRING_LEN(obj));
        w_bytes(w, RSTRING_PTR(obj), (size_t)RSTRING_LEN(obj));
        return;
    case MRB_TT_ARRAY:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_array(w, obj);
        return;
    case MRB_TT_HASH:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_hash(w, obj);
        return;
    case MRB_TT_STRUCT:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_byte(w, 'S');
        w_class_path(w, obj);
        w_struct(w, obj);
        return;
    case MRB_TT_BIGINT:
        /* mruby-bigint: an Integer mrb_int cannot hold. On the 3DS's 32-bit
         * mrb_int that starts at 2**31, so this is a Bignum there, and Ruby
         * links Bignums -- the same bytes and the same link entries as the
         * INTEGER case above produces on the 64-bit PC build. */
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_bignum(w, obj);
        return;
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_class_bytes(w, obj);
        return;
    case MRB_TT_OBJECT:
        if (w_linked(w, obj)) return;
        w_register(w, obj);
        w_byte(w, 'o');
        w_class_path(w, obj);
        w_ivars(w, obj);
        return;
    default:
        mrb_raisef(mrb, E_TYPE_ERROR, "marshal: can't dump %v (an instance of %v)", obj,
                   mrb_obj_value(mrb_obj_class(mrb, obj)));
    }
}

static mrb_value marshal_load(mrb_state* mrb, mrb_value self)
{
    mrb_value arg;
    mrb_get_args(mrb, "o", &arg);
    mrb_value src = arg;
    mrb_value io = mrb_nil_value();
    mrb_int start = 0;
    if (!mrb_string_p(src)) {
        /* RGSS loads a save with the file object: File.open(...) then a dozen
         * Marshal.load(file) calls in a row, each holding one Marshal stream.
         * Read what the file has left, parse one object, then leave the file
         * just after it -- the position our File.open (a MemIO) keeps. */
        mrb_value data;
        if (!mrb_respond_to(mrb, src, mrb_intern_lit(mrb, "read")))
            mrb_raise(mrb, E_TYPE_ERROR, "Marshal.load: expected a String or a file object");
        if (mrb_respond_to(mrb, src, mrb_intern_lit(mrb, "pos")))
            start = mrb_integer(mrb_funcall(mrb, src, "pos", 0));
        data = mrb_funcall(mrb, src, "read", 0);
        if (!mrb_string_p(data))
            mrb_raise(mrb, E_TYPE_ERROR, "Marshal.load: file read did not return a String");
        src = data;
        io = arg;
    }
    reader r;
    r.mrb = mrb;
    r.p = (const uint8_t*)RSTRING_PTR(src);
    r.end = r.p + RSTRING_LEN(src);
    const uint8_t* origin = r.p;
    if (RSTRING_LEN(src) < 2 || r.p[0] != 4 || r.p[1] > 8)
        mrb_raise(mrb, E_TYPE_ERROR, "incompatible marshal file format (can't be read)");
    r.p += 2;
    r.symbols = mrb_ary_new(mrb);
    r.objects = mrb_ary_new(mrb);
    mrb_gc_protect(mrb, src);
    r.arena = mrb_gc_arena_save(mrb);
    mrb_value v = r_object(&r);
    if (!mrb_nil_p(io)) {
        mrb_int consumed = (mrb_int)(r.p - origin);
        if (!mrb_respond_to(mrb, io, mrb_intern_lit(mrb, "pos=")))
            mrb_raise(mrb, E_TYPE_ERROR, "Marshal.load: the file object must support pos=");
        mrb_funcall(mrb, io, "pos=", 1, mrb_int_value(mrb, start + consumed));
    }
    return v;
}

static mrb_value marshal_dump(mrb_state* mrb, mrb_value self)
{
    mrb_value obj, io = mrb_nil_value();
    writer w;
    mrb_int argc = mrb_get_args(mrb, "o|o", &obj, &io);
    if (argc == 2 && !mrb_respond_to(mrb, io, mrb_intern_lit(mrb, "write"))) {
        /* Marshal.dump(obj, limit): RGSS never passes a depth limit, it passes
         * the file, and writing the whole object is more than a limit asks for. */
        printf("marshal: depth limit ignored, dumping the whole object\n");
        io = mrb_nil_value();
    }
    w.mrb = mrb;
    w.out = mrb_str_new(mrb, NULL, 0);
    w.syms = mrb_ary_new(mrb);
    w.objs = mrb_ary_new(mrb);
    w.ids = mrb_hash_new(mrb);
    mrb_gc_protect(mrb, w.out);
    mrb_gc_protect(mrb, w.syms);
    mrb_gc_protect(mrb, w.objs);
    mrb_gc_protect(mrb, w.ids);
    w_byte(&w, 4);
    w_byte(&w, 8);
    w_object(&w, obj);
    if (!mrb_nil_p(io)) {
        mrb_funcall(mrb, io, "write", 1, w.out);
        return io;
    }
    return w.out;
}

void ttm_init_marshal(mrb_state* mrb)
{
    struct RClass* m = mrb_define_module(mrb, "Marshal");
    mrb_define_module_function(mrb, m, "load", marshal_load, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, m, "dump", marshal_dump, MRB_ARGS_ARG(1, 1));
    mrb_define_const(mrb, m, "MAJOR_VERSION", mrb_fixnum_value(4));
    mrb_define_const(mrb, m, "MINOR_VERSION", mrb_fixnum_value(8));
}
