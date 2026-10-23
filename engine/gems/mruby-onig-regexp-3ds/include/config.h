/*
 * config.h -- Onigmo configuration, in place of ./configure.
 *
 * Serves both builds of the spike: MinGW-w64 gcc (PC, LLP64) and devkitARM
 * (3DS, ILP32 newlib). Type sizes come from gcc's predefined macros, so one
 * file is right for both.
 */
#ifndef TTM_ONIGMO_CONFIG_H
#define TTM_ONIGMO_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_ALLOCA 1          /* gcc's __builtin_alloca on both targets */

#define SIZEOF_SHORT     __SIZEOF_SHORT__
#define SIZEOF_INT       __SIZEOF_INT__
#define SIZEOF_LONG      __SIZEOF_LONG__
#define SIZEOF_LONG_LONG __SIZEOF_LONG_LONG__
#define SIZEOF_VOIDP     __SIZEOF_POINTER__

#define PACKAGE_VERSION "6.2.0"

#endif
