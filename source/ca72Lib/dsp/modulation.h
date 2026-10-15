#pragma once
#include "common.h"
#include "mna.h"

namespace ca72::modulation
{
inline constexpr size_t MIX_POINTS = 65;

double pitch_wheel_volts (double wheel);

inline constexpr double PITCH_WHEEL_SEMITONES = 8.07;

double mod_wheel_r_drawn (double wheel);

inline constexpr double MOD_WHEEL_FULL_DRAWN = 1.2e3;
inline constexpr double MOD_WHEEL_FULL = 685.0;

double wheel_shape (double wheel);

inline constexpr std::array<std::pair<double, double>, 4> MOD_WHEEL_LAW {
    std::pair<double, double> { 0.25, 29.5 },
    std::pair<double, double> { 0.5, 95.1 },
    std::pair<double, double> { 0.75, 349.0 },
    std::pair<double, double> { 1.0, MOD_WHEEL_FULL },
};

double through (std::span<const std::pair<double, double>> points, double x);
double position (std::span<const std::pair<double, double>> points, double r);

double mod_wheel_r (double wheel);

inline constexpr std::array<std::pair<double, double>, 4> MIDI_WHEEL_R {
    std::pair<double, double> { 32.0 / 127.0, 46.3 },
    std::pair<double, double> { 64.0 / 127.0, 97.8 },
    std::pair<double, double> { 96.0 / 127.0, 261.0 },
    std::pair<double, double> { 1.0, MOD_WHEEL_FULL },
};

double midi_wheel (double cc);

struct MixTransfer
{
    double offset = 0.0;
    double gain_noise = 0.0;
    double gain_osc3 = 0.0;
    double r_out = 0.0;

    static MixTransfer default_() { return {}; }
    bool operator== (const MixTransfer&) const = default;
};

inline constexpr mna::Node P10 = 1;
inline constexpr mna::Node N10 = 2;
inline constexpr mna::Node NOISE = 3;
inline constexpr mna::Node OSC3 = 4;
inline constexpr size_t HELD = 5;
inline constexpr mna::Node A = 5;
inline constexpr mna::Node B = 6;
inline constexpr mna::Node SUM = 7;
inline constexpr mna::Node NE = 8;
inline constexpr mna::Node N17 = 9;
inline constexpr mna::Node N38 = 10;
inline constexpr mna::Node AMP = 11;
inline constexpr size_t NODES = 12;

mna::Circuit circuit (double mix);

ca72::Result<MixTransfer, mna::NoConvergence> transfer (double mix);

struct LineLoads
{
    bool osc_mod = false;
    bool filter_mod = false;
    double filter_node = 0.0;

    bool operator== (const LineLoads&) const = default;
};

struct Modulation
{
    std::vector<MixTransfer> table;

    static ca72::Result<Modulation, mna::NoConvergence> new_();
    MixTransfer mix (double mix) const;
    static std::pair<double, double> source_loads (double mix);
    static double line (double amp, double r_out, double wheel_r, const LineLoads& loads);
};
} // namespace ca72::modulation
