/*
 * gem.c -- mruby-ttm-gfx: RGSS1's graphics classes in C (see gfx.h).
 */

#include <mruby.h>
#include <mruby/class.h>

#include "gfx.h"

void mrb_mruby_ttm_gfx_gem_init(mrb_state* mrb)
{
    if (!mrb_class_defined(mrb, "RGSSError"))
        mrb_define_class(mrb, "RGSSError", mrb->eStandardError_class);
    /* raised by Graphics.update when the app is closing; not a StandardError,
     * so the game's own `rescue => e` does not swallow it */
    if (!mrb_class_defined(mrb, "TTMQuit"))
        mrb_define_class(mrb, "TTMQuit", mrb->eException_class);
    ttm_font_init(mrb);
    ttm_init_bitmap(mrb);
    ttm_init_viewport(mrb);
    ttm_init_sprite(mrb);
    ttm_init_window(mrb);
    ttm_init_tilemap(mrb);
    ttm_init_plane(mrb);
    ttm_init_graphics(mrb);
}

void mrb_mruby_ttm_gfx_gem_final(mrb_state* mrb)
{
    (void)mrb;
}
