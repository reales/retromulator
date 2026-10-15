#include "tuning.h"

#include "ulp.h"

namespace ca72::tuning
{
using ca72::expo::Input;
using ca72::expo::OSC1_INPUT_R;
using ca72::vco::Vco;

std::array<Input, 6> osc1_inputs (const Tuning& t, double key, Range range)
{
    return osc1_inputs_at (t, key * KEY_STEP, range);
}

std::array<Input, 6> osc1_inputs_at (const Tuning& t, double v_kbd, Range range)
{
    std::array<double, 6> v {
        BEND_DETENT,
        t.tune,
        v_kbd,
        0.0,
        0.0,
        -5.0 - t.octave_step * range.octaves_below_2(),
    };
    std::array<Input, 6> out;
    out.fill (Input { 1.0, 0.0 });
    for (size_t i = 0; i < 6; ++i)
        out[i] = Input { OSC1_INPUT_R[i], v[i] };
    return out;
}

std::pair<double, double> freq_pot (double knob)
{
    double k = ca72::clamp (knob, 0.0, 1.0);
    double top = 5e3 * k;
    double bottom = 5e3 * (1.0 - k) + 5.1e3;
    return { 10.0 * bottom / (top + bottom), top * bottom / (top + bottom) };
}

double osc3_control (double knob, bool control)
{
    double r181 = 51e3, r180 = 15e3, r162 = R162, r170 = 15e3, r150 = 1e3, r155 = 3.01e3;
    double vp = (10.0 / r150 - BIAS) / (1.0 / r150 + 1.0 / r155);
    double vi = vp - IC8_OFFSET;
    auto [vw, rw] = freq_pot (knob);
    double r_in = control ? r181 : r181 * r180 / (r181 + r180);
    double i_in = (vw - vi) / (rw + r_in) + (10.0 - vi) / r162 - BIAS;
    return vi - r170 * i_in;
}

double Drive::apply (ca72::expo::ExpoCircuit& expo) const
{
    expo.pos_v = pos_v;
    expo.pos_g = pos_g;
    expo.trim_on = trim_on;
    return expo.input_current (inputs);
}

Drive osc_drive (Osc osc, const Tuning& t, double v_kbd, Range range)
{
    return osc_drive_with (osc, t, v_kbd, Buses::RESTING, range);
}

Drive osc_drive_with (Osc osc, const Tuning& t, double v_kbd, const Buses& buses, Range range)
{
    Input open { std::numeric_limits<double>::infinity(), 0.0 };
    std::array<Input, 8> inputs;
    inputs.fill (open);
    double tap = -5.0 - t.octave_step * range.octaves_below_2();
    std::array<double, 5> summed { buses.bend, t.tune, v_kbd, buses.modulation, buses.ext };
    Drive d { inputs, 0.0, 0.0, true };
    bool joined = ! (osc.tag == Osc::Tag::Three && ! osc.three_control);
    if (joined)
    {
        for (size_t i = 0; i < summed.size(); ++i)
            inputs[i] = Input { OSC1_INPUT_R[i], summed[i] };
    }
    inputs[5] = Input { OSC1_INPUT_R[5], tap };
    switch (osc.tag)
    {
        case Osc::Tag::One:
            break;
        case Osc::Tag::Two:
        {
            auto [vw, rw] = freq_pot (osc.two_freq);
            double g1 = 1.0 / (rw + 220e3);
            double g2 = 1.0 / 91e3;
            d.pos_g = g1 + g2;
            d.pos_v = (vw * g1 - 10.0 * g2) / d.pos_g;
            break;
        }
        case Osc::Tag::Three:
            inputs[6] = Input { 51.1e3, osc3_control (osc.three_freq, osc.three_control) };
            d.trim_on = osc.three_control;
            break;
    }
    d.inputs = inputs;
    return d;
}

double measure_hz (Vco& vco, double sample_rate, double i_in, size_t cycles)
{
    vco.reset();
    std::vector<double> times;
    times.reserve (cycles + 3);
    uint64_t seen = 0;
    double lim = sample_rate * 40.0;
    size_t limit = (std::isnan (lim) || lim <= 0.0) ? 0 : (size_t) lim;
    size_t n = 0;
    while (times.size() < cycles + 3 && n < limit)
    {
        vco.tick (i_in, 0.0);
        auto [t, count] = vco.last_reset();
        if (count != seen)
        {
            seen = count;
            times.push_back (t);
        }
        n += 1;
    }
    if (times.size() < 4)
        return 0.0;
    std::span<const double> used = std::span<const double> (times).subspan (2);
    double period = (used[used.size() - 1] - used[0]) / (double) (used.size() - 1);
    return sample_rate / period;
}

double Tuner::hz (Vco& vco, const Tuning& t, uint32_t key, Range range)
{
    plays += 1;
    vco.expo.r11 = t.r11;
    vco.expo.a8 = t.a8;
    double i_in = osc_drive_with (osc, t, volts (key), buses, range).apply (vco.expo);
    return measure_hz (vco, sample_rate, i_in, 6);
}

void Tuner::range_and_scale (Vco& vco, Tuning& t)
{
    for (int it = 0; it < 10; ++it)
    {
        double lo = hz (vco, t, LOW_A, Range::R2);
        double hi = hz (vco, t, HIGH_A, Range::R2);
        std::array<double, 2> e { ca72::ulp::log2 (lo / 440.0), ca72::ulp::log2 (hi / 3520.0) };
        if (std::fabs (e[0]) < 2e-6 && std::fabs (e[1]) < 2e-6)
            break;
        double d11 = 5.0, d8 = 0.005;
        Tuning t11 = t;
        t11.r11 = t.r11 + d11;
        Tuning t8 = t;
        t8.a8 = t.a8 + d8;
        double j00 = ca72::ulp::log2 (hz (vco, t11, LOW_A, Range::R2) / lo) / d11;
        double j01 = ca72::ulp::log2 (hz (vco, t8, LOW_A, Range::R2) / lo) / d8;
        double j10 = ca72::ulp::log2 (hz (vco, t11, HIGH_A, Range::R2) / hi) / d11;
        double j11 = ca72::ulp::log2 (hz (vco, t8, HIGH_A, Range::R2) / hi) / d8;
        double det = j00 * j11 - j01 * j10;
        if (std::fabs (det) < 1e-18)
            break;
        t.r11 = ca72::clamp (t.r11 + (-e[0] * j11 + e[1] * j01) / det, 0.0, 1000.0);
        t.a8 = ca72::clamp (t.a8 + (-e[1] * j00 + e[0] * j10) / det, 0.0, 1.0);
    }
}

double Tuner::secant (Vco& vco, Tuning base, void (*set) (Tuning&, double), double x0, double dx,
                      uint32_t key, Range range, double target)
{
    Tuning ta = base;
    set (ta, x0);
    double xa = x0;
    double ea = ca72::ulp::log2 (hz (vco, ta, key, range) / target);
    double xb = x0 + dx;
    for (int it = 0; it < 12; ++it)
    {
        Tuning tb = base;
        set (tb, xb);
        double eb = ca72::ulp::log2 (hz (vco, tb, key, range) / target);
        if (std::fabs (eb) < 2e-6 || eb == ea)
            return xb;
        double next = xb - eb * (xb - xa) / (eb - ea);
        xa = xb;
        ea = eb;
        xb = next;
    }
    return xb;
}

std::pair<Tuning, size_t> folkman_1973 (Vco& vco, double sample_rate, Tuning start)
{
    return folkman_1973_keys (vco, sample_rate, start, [] (uint32_t k) { return (double) k * KEY_STEP; });
}

std::pair<Tuning, size_t> folkman_1973_keys (Vco& vco, double sample_rate, Tuning start,
                                             const std::function<double (uint32_t)>& volts)
{
    return folkman_1973_buses (vco, sample_rate, start, volts, Buses::RESTING);
}

std::pair<Tuning, size_t> folkman_1973_buses (Vco& vco, double sample_rate, Tuning start,
                                              const std::function<double (uint32_t)>& volts, const Buses& buses)
{
    Tuner tu { sample_rate, 0, volts, Osc::One(), buses };
    Tuning t = start;
    tu.range_and_scale (vco, t);
    t.tune = tu.secant (vco, t, [] (Tuning& tt, double x) { tt.tune = x; }, t.tune, 0.02, SECOND_A, Range::R2, 880.0);
    t.octave_step = tu.secant (vco, t, [] (Tuning& tt, double x) { tt.octave_step = x; }, t.octave_step, 0.002,
                               SECOND_A, Range::R8, 220.0);
    vco.expo.r11 = t.r11;
    vco.expo.a8 = t.a8;
    return { t, tu.plays };
}

std::pair<Tuning, size_t> folkman_1973_osc (Vco& vco, double sample_rate, Tuning start, Osc osc,
                                            const std::function<double (uint32_t)>& volts)
{
    return folkman_1973_osc_buses (vco, sample_rate, start, osc, volts, Buses::RESTING);
}

std::pair<Tuning, size_t> folkman_1973_osc_buses (Vco& vco, double sample_rate, Tuning start, Osc osc,
                                                  const std::function<double (uint32_t)>& volts, const Buses& buses)
{
    Osc o = Osc::One();
    switch (osc.tag)
    {
        case Osc::Tag::One: o = Osc::One(); break;
        case Osc::Tag::Two: o = Osc::Two (FREQ_CENTRE); break;
        case Osc::Tag::Three: o = Osc::Three (FREQ_CENTRE, true); break;
    }
    Tuner tu { sample_rate, 0, volts, o, buses };
    Tuning t = start;
    tu.range_and_scale (vco, t);
    vco.expo.r11 = t.r11;
    vco.expo.a8 = t.a8;
    return { t, tu.plays };
}
} // namespace ca72::tuning
