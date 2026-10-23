/*
 * gem.c -- mruby-ttm-rgss: RGSS1's built-in classes, implemented in C.
 * So far the value classes (values.c); Sprite, Bitmap and friends join in M3.
 */

#include <mruby.h>

void ttm_init_values(mrb_state* mrb);

void mrb_mruby_ttm_rgss_gem_init(mrb_state* mrb)
{
    ttm_init_values(mrb);
}

void mrb_mruby_ttm_rgss_gem_final(mrb_state* mrb)
{
    (void)mrb;
}
