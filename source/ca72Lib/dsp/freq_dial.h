#pragma once

#include "common.h"

namespace ca72::freq_dial
{
// Fieldless enums with Default or methods: a struct over an unscoped enum (Law::Semitones,
// switch (law), law == Law::Hardware and Law::default_() all work).
struct Law
{
    enum Value
    {
        Semitones,
        Hardware
    };

    Value value = Semitones;

    constexpr Law() = default;
    constexpr Law (Value v) : value (v) {}
    constexpr explicit Law (int v) : value (static_cast<Value> (v)) {}
    constexpr operator Value() const { return value; }

    static constexpr Law default_() { return Law (Semitones); }
};

inline constexpr std::array<double, 5> OSC2_SEMITONES { 16.753480, -0.651459, 0.194997, -0.005174, -0.000512 };
inline constexpr std::array<double, 5> OSC3_SEMITONES { 16.634609, -0.799108, 0.818439, -0.073963, 0.039155 };

inline constexpr std::array<std::pair<double, double>, 5> OSC2_HARDWARE { {
    { 0.0, 0.0334 },
    { 1.0 / 6.0, 0.1195 },
    { 0.5, 0.5 },
    { 5.0 / 6.0, 0.9000 },
    { 1.0, 1.0 },
} };

inline constexpr std::array<std::pair<double, double>, 5> OSC3_HARDWARE { {
    { 0.0, 0.0008 },
    { 1.0 / 6.0, 0.1060 },
    { 0.5, 0.5 },
    { 5.0 / 6.0, 0.9209 },
    { 1.0, 1.0 },
} };

inline constexpr double DIAL_END = 7.5;

struct Pot
{
    enum Value
    {
        Two,
        Three
    };

    Value value = Two;

    constexpr Pot() = default;
    constexpr Pot (Value v) : value (v) {}
    constexpr explicit Pot (int v) : value (static_cast<Value> (v)) {}
    constexpr operator Value() const { return value; }

    const std::array<double, 5>& curve() const;
    const std::array<std::pair<double, double>, 5>& hardware() const;
    double semitones (double wiper) const;
    double wiper (double semitones) const;
    double wiper_at (Law law, double knob) const;
    double knob_at (Law law, double wiper) const;
};

double along (std::span<const std::pair<double, double>> points, double knob);
double back_along (std::span<const std::pair<double, double>> points, double track);
} // namespace ca72::freq_dial
