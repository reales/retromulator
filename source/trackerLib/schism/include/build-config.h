/* Hand-written configuration for the Trackermeister playback port (Apple clang). */
#ifndef SCHISM_BUILD_CONFIG_H_
#define SCHISM_BUILD_CONFIG_H_

#define PACKAGE "schismtracker"
#define PACKAGE_NAME "schismtracker"
#define PACKAGE_VERSION "trackermeister"
#define VERSION "trackermeister"

#define OPLSOURCE 3

#ifdef _WIN32
/* untested: MSVC has no VLAs, alloca.h or the BSD string calls */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_STAT 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE__STRICMP 1
#define HAVE__STRNICMP 1
#define HAVE_ARITHMETIC_RSHIFT 1
#else
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_STAT 1
#define HAVE_ALLOCA_H 1
#define HAVE_ALLOCA 1
#define HAVE_ASPRINTF 1
#define HAVE_VASPRINTF 1
#define HAVE_SNPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_STRPTIME 1
#define HAVE_MKSTEMP 1
#ifndef _MSC_VER
#define HAVE_LOCALTIME_R 1
#endif
#define HAVE_TZSET 1
#define HAVE_SETENV 1
#define HAVE_UNSETENV 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_STRCASESTR 1
#define HAVE_C99_VLAS 1
#define HAVE_C99_FAMS 1
#define HAVE_ARITHMETIC_RSHIFT 1
#endif

#endif
