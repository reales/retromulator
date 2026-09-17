#pragma once

// Hand-written replacement for the autotools config.h so libmp3lame builds as
// plain sources in the Projucer project. Encoder only: mpglib (the decoder) and
// the NASM/i386 assembly are left out, so LAME uses its portable C paths.

#define PACKAGE "lame"
#define VERSION "3.100"

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMCPY 1
#define HAVE_STRCHR 1

#if defined(_WIN32)
 // MSVC has no <strings.h>; LAME only wants it for strcasecmp.
 #define strcasecmp  _stricmp
 #define strncasecmp _strnicmp
#else
 #define HAVE_STRINGS_H 1
 #define HAVE_UNISTD_H 1
 #define HAVE_SYS_TIME_H 1
 #define HAVE_SYS_TYPES_H 1
#endif

#define HAVE_LONG_LONG 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8

#if defined(__LP64__) || defined(_WIN64)
 #define SIZEOF_LONG 8
 #define SIZEOF_UNSIGNED_LONG 8
#else
 #define SIZEOF_LONG 4
 #define SIZEOF_UNSIGNED_LONG 4
#endif

// Float is enough for the encoder and keeps the tables small.
#define TAKEHIRO_IEEE754_HACK 1

// Builds the library, not the CLI frontend.
#define LAME_LIBRARY_BUILD 1

// configure emits these when the platform headers do not already provide them.
#ifndef HAVE_IEEE754_FLOAT32_T
 typedef float ieee754_float32_t;
#endif
#ifndef HAVE_IEEE754_FLOAT64_T
 typedef double ieee754_float64_t;
#endif
