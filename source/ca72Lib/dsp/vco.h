#pragma once

#include "common.h"
#include "expo.h"
#include "resample.h"
#include "tables.h"

namespace ca72::vco
{
struct CoreParams
{
    double c1;
    double c_par;
    double i_leak;
    double v_threshold;
    double v_top0;
    double top_per_slope;
    double t_hold;
    std::array<double, 8> delay_ln_slope;
    std::array<double, 8> delay;

    static CoreParams default_();
    bool operator== (const CoreParams&) const = default;

    double delay_at (double slope) const;
};

struct ShaperParams
{
    double buf_gain;
    double buf_offset;
    double saw_ratio;
    double rect_fall0;
    double rect_rise0;
    double rect_per_width;
    double rect_high;
    double rect_low;
    double rect_rise_delay;
    double rect_fall_delay;
    double rect_fall_overdrive;
    double tri_glitch;
    double buf_lag;

    static ShaperParams default_();
    bool operator== (const ShaperParams&) const = default;
};

double triangle (double buf);

inline constexpr double TIMING_MAX = 5e-3;
inline constexpr double EARLY_AT_MAX = 0.881;

struct VcoOut
{
    double saw = 0.0;
    double tri = 0.0;
    double rect = 0.0;

    static VcoOut default_() { return VcoOut {}; }
    bool operator== (const VcoOut&) const = default;
};

struct Solved
{
    uint64_t i_in;
    std::optional<uint64_t> guess;
    uint64_t celsius;
    ca72::expo::ExpoCircuit expo;
    double top;
    double bottom;
};

struct Vco
{
    ca72::expo::ExpoCircuit expo;
    CoreParams core;
    ShaperParams shaper;
    double celsius;
    size_t oversample;
    double dt;
    size_t other;
    std::array<ca72::resample::Decimator, 3> dec_other;
    std::optional<std::shared_ptr<const ca72::expo::ConverterTable>> table;
    bool table_on;
    double v;
    double hold_left;
    std::optional<double> reset_in;
    std::optional<double> rise_in;
    std::optional<double> fall_in;
    bool rect_is_low;
    std::optional<double> guess;
    ca72::resample::BlepLine saw;
    ca72::resample::BlepLine tri;
    ca72::resample::BlepLine rect;
    std::array<ca72::resample::Decimator, 3> dec;
    uint64_t ticks;
    double last_reset_;
    uint64_t resets;
    size_t sub;
    std::array<std::optional<Solved>, 2> solved;
    int blep = ca72::resample::blep_points;
    bool blamp = false;

    static Vco new_ (double sample_rate, size_t oversample);

    void prepare (size_t oversample);
    void set_oversample (size_t oversample);
    std::pair<double, uint64_t> last_reset() const;
    double latency() const;
    void start_at (double at);
    void reset();
    double ramp() const;
    VcoOut tick (double i_in, double width);
    double timing_current() const;
    VcoOut tick_current (double i0, double b, double width);
    void step (double a, double b, double c, double v_fall, double rise_buf);
    void do_reset (double a, double b, double c, double frac, double rise_buf);
    // Not upstream: BLEP points (2 or 4) and BLAMP on the slope change at the reset.
    void set_quality (int points, bool corners);
    void fresh_bleps();
};

} // namespace ca72::vco
