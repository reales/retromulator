#pragma once

#include "common.h"

namespace ca72::linear
{
struct Discrete;

struct SmallSignal
{
    size_t n = 0;
    std::vector<double> g;
    std::vector<double> c;
    std::vector<double> g_in;

    std::pair<double, double> response (size_t out, double f) const;
    Discrete discrete (double h) const;
};

struct Discrete
{
    size_t n = 0;
    std::vector<double> a;
    std::vector<double> b;
    std::vector<double> x_;
    std::vector<double> next;
    double u_prev = 0.0;
    double h = 0.0;
    std::vector<double> work;

    void rebuild (const SmallSignal& model, double h);
    void take_state (const Discrete& from);
    void carry (const Discrete& from, std::span<const std::pair<size_t, size_t>> map, bool back);
    void step (double u);

    double x (size_t i) const { return x_[i]; }

    std::pair<double, double> response (size_t out, double f) const;
};

void solve_complex (size_t n, std::span<std::pair<double, double>> m, std::span<std::pair<double, double>> rhs);
void invert_into (size_t n, std::span<double> a, std::span<double> inv);
} // namespace ca72::linear
