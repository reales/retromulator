#pragma once

#include "common.h"
#include "devices.h"

#include <bit>

namespace ca72::expo
{
inline constexpr double M5_LINE = -4.99998103;
inline constexpr double M4_LINE = -3.86706;
inline constexpr double SUMMER_OFFSET = 18.6e-6;

struct Input
{
    double r;
    double v;

    bool operator== (const Input&) const = default;
};

inline constexpr double R_EXT = 50.5e3;

inline constexpr std::array<double, 6> OSC1_INPUT_R { 150e3, 560e3, 51.1e3, 51.1e3, R_EXT, 15e3 };

struct ExpoState
{
    double ic_exp = 0.0;
    double ib_exp = 0.0;
    double ic_ref = 0.0;
    double ib_ref = 0.0;
    double v_sum = 0.0;
    double v_ic1 = 0.0;
    double vb_ref = 0.0;
    double vb_exp = 0.0;
    double v_sub = 0.0;

    static ExpoState default_() { return ExpoState {}; }
    bool operator== (const ExpoState&) const = default;
};

struct ExpoCircuit
{
    double r20;
    double tc20;
    double r11;
    double r37;
    double r42;
    double r9;
    double r8;
    double a8;
    double r19;
    double r7;
    double r50;
    double r79;
    double m5;
    double m4;
    double vn;
    double r10;
    double pos_v;
    double pos_g;
    bool trim_on;
    double vos;
    double bias;
    ca72::devices::Bjt bjt;

    static ExpoCircuit default_();

    bool operator== (const ExpoCircuit& o) const;

    double v_sum() const;
    double drive_u (double i_in, double celsius) const;
    double input_current (std::span<const Input> inputs) const;
    double collector_current (std::span<const Input> inputs, double v_ramp, double celsius) const;
    ExpoState solve (std::span<const Input> inputs, double v_ramp, double celsius) const;
    ExpoState solve_current (double i_in, double v_ramp, double celsius, std::optional<double> guess) const;
};

// Field-wise equality of two Bjt parameter sets (Rust's derived PartialEq).
bool bjt_eq (const ca72::devices::Bjt& a, const ca72::devices::Bjt& b);

struct ConverterTable
{
    double u0;
    double du;
    std::vector<double> top;
    std::vector<double> bottom;
    std::array<uint64_t, 3> key;

    static constexpr size_t N = 4096;

    static ConverterTable new_ (const ExpoCircuit& expo, double celsius);
    std::optional<std::pair<double, double>> lookup (const ExpoCircuit& expo, double celsius, double u) const;
};
} // namespace ca72::expo
