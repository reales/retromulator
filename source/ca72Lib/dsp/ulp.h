#pragma once

#include "common.h"
#include "fast.h"

#include <string>

namespace ca72::ulp
{
inline void set (int32_t dir) { (void) dir; }

std::vector<std::pair<std::string, uint64_t>> calls();

inline double expm1 (double x) { return std::expm1 (x); }
inline double log (double x) { return std::log (x); }
inline double log1p (double x) { return std::log1p (x); }
inline double log2 (double x) { return std::log2 (x); }
inline double tanh (double x) { return std::tanh (x); }
inline double sin (double x) { return std::sin (x); }
inline double cos (double x) { return std::cos (x); }
inline double tan (double x) { return std::tan (x); }

inline thread_local bool FAST = false;

struct FastScope
{
    bool was;

    static FastScope new_ (bool fast)
    {
        const bool was = FAST;
        FAST = fast;
        return FastScope (was, 0);
    }

    ~FastScope() { FAST = was; }

    FastScope (const FastScope&) = delete;
    FastScope& operator= (const FastScope&) = delete;

private:
    FastScope (bool was_, int) : was (was_) {}
};

inline double exp (double x)
{
    if (FAST)
        return ca72::fast::exp (x);
    return std::exp (x);
}

inline double tanh_in_tick (double x)
{
    if (FAST)
        return ca72::fast::tanh (x);
    return std::tanh (x);
}

inline double pow (double x, double y) { return std::pow (x, y); }
} // namespace ca72::ulp
