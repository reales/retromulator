// Static siddefs.h for reSID — Retromulator integration (replaces autoconf-generated version)
// reSID is GPL v2 by Dag Lem <resid@nimrod.no>

#ifndef RESID_SIDDEFS_H
#define RESID_SIDDEFS_H

#define RESID_INLINING 1
#define RESID_INLINE inline
#define RESID_BRANCH_HINTS 1

#if defined(__GNUC__) || defined(__clang__)
  #define HAVE_BUILTIN_EXPECT 1
#else
  #define HAVE_BUILTIN_EXPECT 0
#endif

#define RESID_FPGA_CODE 0

// On C++20+ we honour reSID's intended `consteval` / non-static `constexpr`
// member usage. On C++17, libc++'s log2/exp aren't constexpr, so the
// ExternalFilterCoefficients ctor cannot be evaluated at compile time —
// fall back to runtime initialization (drop the keywords).
#if __cplusplus >= 202002L
  #define RESID_CONSTEVAL consteval
  #define RESID_CONSTEXPR constexpr
#else
  #define RESID_CONSTEVAL
  #define RESID_CONSTEXPR
#endif
#if __cpp_constinit >= 201907L
  #define RESID_CONSTINIT constinit
#else
  #define RESID_CONSTINIT
#endif

#if RESID_BRANCH_HINTS && HAVE_BUILTIN_EXPECT
  #define likely(x)   __builtin_expect(!!(x), 1)
  #define unlikely(x) __builtin_expect(!!(x), 0)
#else
  #define likely(x)   (x)
  #define unlikely(x) (x)
#endif

#ifndef VERSION
  #define VERSION "1.0-retromulator"
#endif

namespace reSID {

typedef unsigned int reg4;
typedef unsigned int reg8;
typedef unsigned int reg12;
typedef unsigned int reg16;
typedef unsigned int reg24;

typedef int cycle_count;
typedef short short_point[2];
typedef double double_point[2];

enum chip_model { MOS6581, MOS8580 };

enum sampling_method { SAMPLE_FAST, SAMPLE_INTERPOLATE,
                       SAMPLE_RESAMPLE, SAMPLE_RESAMPLE_FASTMEM };

} // namespace reSID

extern "C"
{
#ifndef RESID_VERSION_CC
extern const char* resid_version_string;
#else
const char* resid_version_string = VERSION;
#endif
}

#endif // not RESID_SIDDEFS_H
