#pragma once

#include "common.h"

namespace ca72::unconverged
{
enum class Solver
{
    ContourRoot,
    ContourTransistor,
    ContourDecay,
    ContourCoupling,
    SeriesJunction,
    DegeneratedPair,
    Junctions,
    VcaBias,
    Converter,
    FilterNode,
    FilterLoop,
};

inline constexpr size_t N = 11;

inline constexpr std::array<const char*, N> NAMES = {
    "contour root",
    "contour transistor",
    "contour decay",
    "contour coupling",
    "series junction",
    "degenerated pair",
    "junctions",
    "VCA bias",
    "converter",
    "filter node",
    "filter loop",
};

inline thread_local std::array<uint64_t, N> COUNTS {};

inline void note (Solver s)
{
    COUNTS[(size_t) s] += 1;
}

std::vector<std::pair<const char*, uint64_t>> take();
} // namespace ca72::unconverged
