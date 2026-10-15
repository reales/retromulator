#include "voice_types.h"

#include "ulp.h"
#include "vcf.h"

namespace ca72::voice
{
namespace
{
    constexpr std::array<std::pair<double, double>, 6> VOLUME_TRACK { {
        { 0.0, 0.0 },
        { 0.2, 0.1548 },
        { 0.4, 0.3777 },
        { 0.6, 0.6226 },
        { 0.8, 0.8453 },
        { 1.0, 1.0 },
    } };

    constexpr std::array<std::pair<double, double>, 6> CONTOUR_AMOUNT_TRACK { {
        { 0.0, 0.0 },
        { 0.25, 0.2080 },
        { 0.4, 0.3796 },
        { 0.5, 0.5111 },
        { 0.75, 0.7950 },
        { 1.0, 1.0 },
    } };

    constexpr std::array<std::pair<double, double>, 7> SUSTAIN_TRACK { {
        { 0.0, 0.0 },
        { 0.2, 0.1611 },
        { 0.4, 0.3735 },
        { 0.5, 0.4924 },
        { 0.6, 0.6185 },
        { 0.8, 0.8375 },
        { 1.0, 1.0 },
    } };

    constexpr std::array<std::pair<double, double>, 7> CUTOFF_TRACK { {
        { 0.0, 0.0 },
        { 0.1, 0.0582 },
        { 0.3, 0.2669 },
        { 0.5, 0.5 },
        { 0.7, 0.7304 },
        { 0.9, 0.9346 },
        { 1.0, 1.0 },
    } };

    constexpr std::array<std::pair<double, double>, 10> EMPHASIS_R14 { {
        { 0.0, 50e3 },
        { 0.2, 30.68e3 },
        { 0.25, 18.82e3 },
        { 0.3, 9.47e3 },
        { 0.4, 3.64e3 },
        { 0.5, 2.91e3 },
        { 0.6, 2.30e3 },
        { 0.7, 1.548e3 },
        { 0.75, 1.35e3 },
        { 0.85, 834.0 },
    } };

    constexpr std::array<std::pair<double, double>, 4> GLIDE_LAW { {
        { 0.25, 162e3 },
        { 0.5, 257e3 },
        { 0.75, 2.1e6 },
        { 1.0, 5e6 },
    } };

    constexpr std::array<std::pair<double, double>, 6> EXT_TAPER { {
        { 0.2, 0.011'81 },
        { 0.4, 0.028'45 },
        { 0.5, 0.037'10 },
        { 0.6, 0.046'45 },
        { 0.8, 0.509'38 },
        { 1.0, 1.0 },
    } };

    double track (std::span<const std::pair<double, double>> points, double knob)
    {
        const double p = ca72::clamp (knob, 0.0, 1.0);
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto [pa, ta] = points[i];
            const auto [pb, tb] = points[i + 1];
            if (p == pb)
                return tb;
            if (p <= pb)
                return ta + (tb - ta) * (p - pa) / (pb - pa);
        }
        return 1.0;
    }
} // namespace

Panel Panel::default_()
{
    auto osc = [] (bool on)
    {
        OscPanel o;
        o.range = ca72::tuning::Range::R8;
        o.waveform = Waveform::Sawtooth;
        o.on = on;
        o.volume = 0.8;
        o.freq = ca72::tuning::FREQ_CENTRE;
        return o;
    };
    Panel p;
    p.osc = { osc (true), osc (false), osc (false) };
    p.osc3_control = true;
    p.cutoff = 0.5;
    p.emphasis = 0.0;
    p.contour_amount = 0.5;
    p.keyboard_control_1 = true;
    p.keyboard_control_2 = false;
    p.filter_contour = ContourKnobs { 0.0, 0.4, 0.5 };
    p.loudness_contour = ContourKnobs { 0.0, 0.4, 0.8 };
    p.glide = 0.0;
    p.glide_on = false;
    p.decay = true;
    p.noise_on = false;
    p.noise_volume = 0.8;
    p.noise_pink = false;
    p.mod_mix = 0.0;
    p.osc_mod = false;
    p.filter_mod = false;
    p.filter_hi = false;
    p.pitch_wheel = 0.0;
    p.mod_wheel = 0.0;
    p.ext_volume = 0.5;
    p.ext_on = false;
    p.a440 = false;
    p.tune = 0.0;
    p.quality = Quality::NoCompromises;
    p.priority = Priority::Low;
    p.retrigger = false;
    return p;
}

double volume_track (double volume) { return track (VOLUME_TRACK, volume); }

double contour_amount_track (double amount) { return track (CONTOUR_AMOUNT_TRACK, amount); }

double sustain_track (double sustain) { return track (SUSTAIN_TRACK, sustain); }

ca72::expo::Input contour_input (double amount, double env_f)
{
    const double t = contour_amount_track (amount);
    return ca72::expo::Input { ca72::vcf::R74 + 5e3 * t * (1.0 - t), env_f * t };
}

double cutoff_track (double cutoff) { return track (CUTOFF_TRACK, cutoff); }

double audio_taper (double p)
{
    return (ca72::ulp::pow (81.0, ca72::clamp (p, 0.0, 1.0)) - 1.0) / 80.0;
}

double time_pot_drawn (double p) { return 1e6 * audio_taper (p); }

double time_pot (double p_, std::span<const std::pair<double, double>> law)
{
    const double p = ca72::clamp (p_, 0.0, 1.0);
    const auto [p0, r0] = law[0];
    if (p <= p0)
        return r0 * time_pot_drawn (p) / time_pot_drawn (p0);
    auto a = law[0];
    for (size_t i = 1; i < law.size(); ++i)
    {
        const auto b = law[i];
        if (p == b.first)
            return b.second;
        if (p < b.first)
            return a.second * ca72::ulp::pow (b.second / a.second, (p - a.first) / (b.first - a.first));
        a = b;
    }
    return a.second;
}

double emphasis_r14_drawn (double p) { return 50e3 * audio_taper (1.0 - p); }

double emphasis_r14 (double p_)
{
    const double p = ca72::clamp (p_, 0.0, 1.0);
    const auto [pn, rn] = EMPHASIS_R14[EMPHASIS_R14.size() - 1];
    if (p >= pn)
        return rn * audio_taper (1.0 - p) / audio_taper (1.0 - pn);
    for (size_t i = 0; i + 1 < EMPHASIS_R14.size(); ++i)
    {
        const auto [pa, ra] = EMPHASIS_R14[i];
        const auto [pb, rb] = EMPHASIS_R14[i + 1];
        if (p <= pb)
            return ra * ca72::ulp::pow (rb / ra, (p - pa) / (pb - pa));
    }
    return rn;
}

double glide_r (double p, bool on) { return on ? glide_pot (p) : 0.0; }

double glide_pot_drawn (double p) { return 5e6 * audio_taper (p); }

double glide_pot (double p_)
{
    const double p = ca72::clamp (p_, 0.0, 1.0);
    const auto [p0, r0] = GLIDE_LAW[0];
    if (p <= p0)
        return r0 * audio_taper (p) / audio_taper (p0);
    for (size_t i = 0; i + 1 < GLIDE_LAW.size(); ++i)
    {
        const auto [pa, ra] = GLIDE_LAW[i];
        const auto [pb, rb] = GLIDE_LAW[i + 1];
        if (p <= pb)
            return ra * ca72::ulp::pow (rb / ra, (p - pa) / (pb - pa));
    }
    return GLIDE_LAW[GLIDE_LAW.size() - 1].second;
}

double feedback_law (double knob)
{
    if (knob > 0.0)
        return ca72::ulp::pow (std::fmin (knob, 1.0), FEEDBACK_TAPER);
    return 0.0;
}

double ext_taper (double p_)
{
    const double p = ca72::clamp (p_, 0.0, 1.0);
    const auto [p0, t0] = EXT_TAPER[0];
    if (p <= p0)
        return t0 * audio_taper (p) / audio_taper (p0);
    for (size_t i = 0; i + 1 < EXT_TAPER.size(); ++i)
    {
        const auto [pa, ta] = EXT_TAPER[i];
        const auto [pb, tb] = EXT_TAPER[i + 1];
        if (p <= pb)
        {
            const double u = (p - pa) / (pb - pa);
            return ta * ca72::ulp::pow (tb / ta, u);
        }
    }
    return 1.0;
}
} // namespace ca72::voice
