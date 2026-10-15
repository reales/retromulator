#pragma once

#include "common.h"

// a440_table.rs: generated tables, values copied exactly.
namespace ca72::a440_table
{
inline constexpr double A68 = 0.000000000e0;
inline constexpr double HZ = 4.306787278e2;
inline constexpr double DC = 7.453326216e0;
extern const std::array<double, 24> AMP;
extern const std::array<double, 24> PHASE;
inline constexpr double START_STEP = 1e-3;
extern const std::array<double, 300> START_DC;
extern const std::array<double, 300> START_AMP;
} // namespace ca72::a440_table
