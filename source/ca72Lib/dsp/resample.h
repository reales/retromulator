#pragma once

#include "common.h"

#include "../hiir/Downsampler2xF64Fpu.h"
#include "../hiir/Upsampler2xF64Fpu.h"

namespace ca72::resample
{
// 2: the upstream two-point polyBLEP (one sample late). 4: cubic B-spline polyBLEP (two samples late).
inline int blep_points = 2;

struct BlepLine
{
    double held = 0.0;
    double next_correction = 0.0;
    double older = 0.0;
    double next2_correction = 0.0;
    int points = 2;

    static BlepLine default_() { return with_points (blep_points); }
    static BlepLine with_points (int n) { BlepLine b; b.points = n; return b; }

    // Integral of the cubic B-spline from -2 to x, less the unit step.
    static double residual4 (double x)
    {
        auto lower = [] (double u)
        {
            if (u <= -2.0) return 0.0;
            if (u <= -1.0) { const double w = u + 2.0; return w * w * w * w / 24.0; }
            return 1.0 / 24.0 + (4.0 * u - 2.0 * u * u * u - 0.75 * u * u * u * u + 2.75) / 6.0;
        };
        return x < 0.0 ? lower (x) : -lower (-x);
    }

    static double kernel4 (double x)
    {
        const double a = std::fabs (x);
        if (a < 1.0) return (4.0 - 6.0 * a * a + 3.0 * a * a * a) / 6.0;
        if (a < 2.0) { const double w = 2.0 - a; return w * w * w / 6.0; }
        return 0.0;
    }

    void step (double delta, double frac)
    {
        if (points == 4)
        {
            const double f = ca72::clamp (frac, 0.0, 1.0);
            older += delta * residual4 (-1.0 - f);
            held += delta * residual4 (-f);
            next_correction += delta * residual4 (1.0 - f);
            next2_correction += delta * residual4 (2.0 - f);
            return;
        }
        const double d = ca72::clamp (1.0 - frac, 0.0, 1.0);
        held += delta * d * d * 0.5;
        next_correction -= delta * (1.0 - d) * (1.0 - d) * 0.5;
    }

    void impulse (double area, double frac)
    {
        if (points == 4)
        {
            const double f = ca72::clamp (frac, 0.0, 1.0);
            older += area * kernel4 (-1.0 - f);
            held += area * kernel4 (-f);
            next_correction += area * kernel4 (1.0 - f);
            next2_correction += area * kernel4 (2.0 - f);
            return;
        }
        const double d = ca72::clamp (1.0 - frac, 0.0, 1.0);
        held += area * d;
        next_correction += area * (1.0 - d);
    }

    // Integral of the step residual: corrects a slope change of `delta` per sample at `frac`.
    static double ramp2 (double x)
    {
        if (x <= -1.0 || x >= 1.0) return 0.0;
        return x < 0.0 ? (x + 1.0) * (x + 1.0) * (x + 1.0) / 6.0 : (1.0 - x) * (1.0 - x) * (1.0 - x) / 6.0;
    }

    static double ramp4 (double x)
    {
        if (x <= -2.0 || x >= 2.0) return 0.0;
        auto simpson = [] (double a, double b, double eps)
        {
            const double h = (b - a) / 32.0;
            double s = residual4 (a + eps) + residual4 (b - eps);
            for (int i = 1; i < 32; ++i)
                s += (i % 2 ? 4.0 : 2.0) * residual4 (a + h * i);
            return s * h / 3.0;
        };
        if (x <= 0.0)
            return simpson (-2.0, x, 1e-12);
        return simpson (-2.0, 0.0, 1e-12) + simpson (0.0, x, 1e-12);
    }

    void ramp (double delta, double frac)
    {
        const double f = ca72::clamp (frac, 0.0, 1.0);
        if (points == 4)
        {
            older += delta * ramp4 (-1.0 - f);
            held += delta * ramp4 (-f);
            next_correction += delta * ramp4 (1.0 - f);
            next2_correction += delta * ramp4 (2.0 - f);
            return;
        }
        held += delta * ramp2 (-f);
        next_correction += delta * ramp2 (1.0 - f);
    }

    double push (double naive)
    {
        if (points == 4)
        {
            const double out = older;
            older = held;
            held = naive + next_correction;
            next_correction = next2_correction;
            next2_correction = 0.0;
            return out;
        }
        const double out = held;
        held = naive + next_correction;
        next_correction = 0.0;
        return out;
    }
};

std::vector<double> halfband_taps (size_t len, double beta);

struct Halfband
{
    std::vector<double> taps;
    std::vector<double> line;
    size_t pos = 0;
    bool phase = false;
    bool sparse_ = false;

    static Halfband new_ (size_t len, double beta);
    static Halfband sparse (size_t len, double beta);

    double sum (std::span<const double> w) const;

    size_t delay() const { return taps.size() / 2; }

    std::optional<double> push (double x);
    double filter (double x);
    double filter_stuffed_zero();
    void reset();
};

// Not upstream: polyphase IIR halfbands (hiir) instead of the Kaiser FIRs, chosen when a
// resampler is made. The stage next to the base rate is the narrow one (20 kHz passband at 48 kHz).
inline thread_local bool iir_resamplers = true;

struct IirDown
{
    bool narrow = false;
    hiir::Downsampler2xF64Fpu<8> n8;
    hiir::Downsampler2xF64Fpu<4> n4;
    double pending = 0.0;
    bool have = false;

    static IirDown make (bool narrow);
    double delay() const;
    std::optional<double> push (double x)
    {
        if (! have)
        {
            pending = x;
            have = true;
            return std::nullopt;
        }
        have = false;
        const double in[2] { pending, x };
        return narrow ? n8.process_sample (in) : n4.process_sample (in);
    }
    void reset();
};

struct IirUp
{
    bool narrow = false;
    hiir::Upsampler2xF64Fpu<8> n8;
    hiir::Upsampler2xF64Fpu<4> n4;

    static IirUp make (bool narrow);
    double delay() const;
    void push (double x, double& a, double& b)
    {
        if (narrow)
            n8.process_sample (a, b, x);
        else
            n4.process_sample (a, b, x);
    }
    void reset();
};

struct Decimator
{
    std::vector<Halfband> stages;
    std::vector<IirDown> iir;

    static Decimator new_ (size_t factor);
    static Decimator sparse (size_t factor);
    // Always the Kaiser FIRs (the preamp's FEEDBACK delay needs their whole-sample latency).
    static Decimator fir (size_t factor);
    static Decimator fir_sparse (size_t factor);
    static Decimator with (size_t factor, Halfband (*make) (size_t, double));

    std::optional<double> push (double x);
    double delay() const;
    void reset();
};

struct Interpolator
{
    std::vector<Halfband> stages;
    std::vector<IirUp> iir;
    size_t factor_ = 0;

    static Interpolator new_ (size_t factor);
    static Interpolator sparse (size_t factor);
    static Interpolator fir (size_t factor);
    static Interpolator fir_sparse (size_t factor);
    static Interpolator with (size_t factor, Halfband (*make) (size_t, double));

    size_t factor() const { return factor_; }

    void push (double x, std::span<double> out);
    double delay() const;
    void reset();
};

uint32_t stages_len (size_t factor);
} // namespace ca72::resample
