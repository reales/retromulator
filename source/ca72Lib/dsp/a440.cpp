#include "a440.h"

#include "ulp.h"

#include <numbers>

namespace ca72::a440
{
namespace t = ca72::a440_table;

A440 A440::new_ (double rate)
{
    std::vector<double> wave;
    wave.reserve (N + 1);
    for (size_t i = 0; i <= N; ++i)
    {
        double x = (double) i / (double) N;
        double s = 0.0;
        for (size_t k = 0; k < t::AMP.size() && k < t::PHASE.size(); ++k)
            s += t::AMP[k] * ca72::ulp::cos (2.0 * std::numbers::pi * (double) (k + 1) * x + t::PHASE[k]);
        wave.push_back (s);
    }
    return A440 { std::move (wave), 0.0, HZ / rate, 0.0, 1.0 / rate, false };
}

double A440::tick (bool on_)
{
    if (! on_)
    {
        on = false;
        return 0.0;
    }
    if (! on)
    {
        on = true;
        since_on = 0.0;
        phase = 0.0;
    }
    double pos = since_on / t::START_STEP;
    size_t last = t::START_DC.size() - 1;
    double dc, amp;
    if (pos < (double) last)
    {
        size_t i = (size_t) pos;
        double f = pos - (double) i;
        dc = t::START_DC[i] + (t::START_DC[i + 1] - t::START_DC[i]) * f;
        amp = t::START_AMP[i] + (t::START_AMP[i + 1] - t::START_AMP[i]) * f;
    }
    else
    {
        dc = t::DC;
        amp = 1.0;
    }
    double x = phase * (double) N;
    size_t i = std::min ((size_t) x, N - 1);
    double f = x - (double) i;
    double w = wave[i] + (wave[i + 1] - wave[i]) * f;
    phase += step;
    if (phase >= 1.0)
        phase -= 1.0;
    since_on += dt;
    return dc + amp * w;
}
} // namespace ca72::a440
