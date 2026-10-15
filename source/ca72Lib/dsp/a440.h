#pragma once

#include "common.h"
#include "a440_table.h"

namespace ca72::a440
{
inline constexpr double HZ = 440.0;
inline constexpr size_t N = 2048;

struct A440
{
    std::vector<double> wave;
    double phase;
    double step;
    double since_on;
    double dt;
    bool on;

    static A440 new_ (double rate);

    bool is_on() const { return on; }
    double tick (bool on_);
};
} // namespace ca72::a440
