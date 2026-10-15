#pragma once

#include "common.h"

// tables.rs: generated tables, values copied exactly.
namespace ca72::tables
{
inline constexpr double TRI_MIN = -4.6;
inline constexpr double TRI_STEP = 0.01;
extern const std::array<double, 891> TRI;
inline constexpr double OUT_DI_MIN = -1.0;
inline constexpr double OUT_DI_STEP = 0.002;
extern const std::array<double, 1001> OUT_DI;
inline constexpr double OUT_DIB_MIN = -1.0;
inline constexpr double OUT_DIB_STEP = 0.002;
extern const std::array<double, 1001> OUT_DIB;
} // namespace ca72::tables
