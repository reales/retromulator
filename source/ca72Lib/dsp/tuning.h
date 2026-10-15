#pragma once

#include "common.h"
#include "expo.h"
#include "vco.h"

namespace ca72::tuning
{
inline constexpr double KEY_STEP = 8.48e-3 * 10.0;
inline constexpr double BEND_DETENT = 10.0 * 15.3 / 25.0;
inline constexpr double TUNE_CENTRE = 10.0 * 7.6 / 10.1;
inline constexpr uint32_t LOW_A = 4;
inline constexpr uint32_t SECOND_A = 16;
inline constexpr uint32_t HIGH_A = 40;

inline constexpr double LO_BELOW_2 = 8.74;

// Fieldless enum with a method: a struct over an unscoped enum, so Range::R8, switch (range)
// and range.octaves_below_2() all work.
struct Range
{
    enum Value
    {
        Lo,
        R32,
        R16,
        R8,
        R4,
        R2
    };

    Value value = Lo;

    constexpr Range() = default;
    constexpr Range (Value v) : value (v) {}
    constexpr explicit Range (int v) : value (static_cast<Value> (v)) {}
    constexpr operator Value() const { return value; }

    constexpr double octaves_below_2() const
    {
        switch (value)
        {
            case R2: return 0.0;
            case R4: return 1.0;
            case R8: return 2.0;
            case R16: return 3.0;
            case R32: return 4.0;
            case Lo: return LO_BELOW_2;
        }
        return LO_BELOW_2;
    }
};

constexpr double octaves_below_2 (Range self) { return self.octaves_below_2(); }

struct Tuning
{
    double r11;
    double a8;
    double octave_step;
    double tune;

    static Tuning default_() { return Tuning { 500.0, 0.5, 15.0 / 51.1, TUNE_CENTRE }; }
    bool operator== (const Tuning&) const = default;
};

std::array<ca72::expo::Input, 6> osc1_inputs (const Tuning& t, double key, Range range);
std::array<ca72::expo::Input, 6> osc1_inputs_at (const Tuning& t, double v_kbd, Range range);

struct Osc
{
    enum class Tag
    {
        One,
        Two,
        Three
    } tag = Tag::One;
    double two_freq = 0.0;
    double three_freq = 0.0;
    bool three_control = false;

    static Osc One() { return Osc { Tag::One, 0.0, 0.0, false }; }
    static Osc Two (double freq) { return Osc { Tag::Two, freq, 0.0, false }; }
    static Osc Three (double freq, bool control) { return Osc { Tag::Three, 0.0, freq, control }; }

    bool operator== (const Osc&) const = default;
};

inline constexpr double FREQ_CENTRE = 0.5;

std::pair<double, double> freq_pot (double knob);

inline constexpr double BIAS = 15.16e-6 / 2.0 / 93.75;
inline constexpr double IC8_OFFSET = 0.1868e-3;
inline constexpr double R162 = 2.96e3;

double osc3_control (double knob, bool control);

struct Drive
{
    std::array<ca72::expo::Input, 8> inputs;
    double pos_v;
    double pos_g;
    bool trim_on;

    bool operator== (const Drive&) const = default;

    double apply (ca72::expo::ExpoCircuit& expo) const;
};

struct Buses
{
    double bend;
    double modulation;
    double ext;

    static const Buses RESTING;
    bool operator== (const Buses&) const = default;
};

inline constexpr Buses Buses::RESTING { BEND_DETENT, 0.0, 0.0 };

Drive osc_drive (Osc osc, const Tuning& t, double v_kbd, Range range);

inline constexpr double OPEN_BUS =
    (10.0 / 33e3 - 3.0 * 5.0 / ca72::expo::R_EXT) / (1.0 / 33e3 + 3.0 / ca72::expo::R_EXT);

Drive osc_drive_with (Osc osc, const Tuning& t, double v_kbd, const Buses& buses, Range range);

double measure_hz (ca72::vco::Vco& vco, double sample_rate, double i_in, size_t cycles);

struct Tuner
{
    double sample_rate;
    size_t plays;
    const std::function<double (uint32_t)>& volts;
    Osc osc;
    Buses buses;

    double hz (ca72::vco::Vco& vco, const Tuning& t, uint32_t key, Range range);
    void range_and_scale (ca72::vco::Vco& vco, Tuning& t);
    double secant (ca72::vco::Vco& vco, Tuning base, void (*set) (Tuning&, double), double x0, double dx,
                   uint32_t key, Range range, double target);
};

std::pair<Tuning, size_t> folkman_1973 (ca72::vco::Vco& vco, double sample_rate, Tuning start);
std::pair<Tuning, size_t> folkman_1973_keys (ca72::vco::Vco& vco, double sample_rate, Tuning start,
                                             const std::function<double (uint32_t)>& volts);
std::pair<Tuning, size_t> folkman_1973_buses (ca72::vco::Vco& vco, double sample_rate, Tuning start,
                                              const std::function<double (uint32_t)>& volts, const Buses& buses);
std::pair<Tuning, size_t> folkman_1973_osc (ca72::vco::Vco& vco, double sample_rate, Tuning start, Osc osc,
                                            const std::function<double (uint32_t)>& volts);
std::pair<Tuning, size_t> folkman_1973_osc_buses (ca72::vco::Vco& vco, double sample_rate, Tuning start, Osc osc,
                                                  const std::function<double (uint32_t)>& volts, const Buses& buses);
} // namespace ca72::tuning
