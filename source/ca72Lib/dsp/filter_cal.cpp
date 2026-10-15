#include "filter_cal.h"

#include "keyboard.h"
#include "tuning.h"
#include "voice_types.h"

namespace ca72::filter_cal
{
using ca72::expo::Input;
using ca72::vcf::Vcf;

ca72::vcf::FilterExpo FilterTrims::expo() const
{
    auto e = ca72::vcf::FilterExpo::default_();
    e.r39 = r39;
    e.r49 = r49;
    return e;
}

ca72::vcf::VcfCircuit FilterTrims::circuit() const
{
    auto c = ca72::vcf::VcfCircuit::default_();
    c.r73_pos = r73_pos;
    return c;
}

std::array<Input, 6> inputs (double cutoff, bool kb1, bool kb2, double v_kbd)
{
    double pos = ca72::clamp (cutoff, 0.0, 1.0);
    Input open { .r = std::numeric_limits<double>::infinity(), .v = 0.0 };
    return {
        Input { .r = 200e3 + 5e3 * pos * (1.0 - pos), .v = -10.0 + 20.0 * pos },
        Input { .r = 47e3, .v = 0.0 },
        kb1 ? Input { .r = 300e3, .v = v_kbd } : open,
        kb2 ? Input { .r = 150e3, .v = v_kbd } : open,
        Input { .r = 33e3, .v = 0.0 },
        Input { .r = 100e3, .v = 0.0 },
    };
}

static std::vector<double> run (double rate, const FilterTrims& trims, double r14, double i0, double seconds)
{
    Vcf f = Vcf::new_ (rate, 4);
    f.circuit = trims.circuit();
    f.circuit.r14 = r14;
    size_t n = (size_t) ((0.1 + seconds) * rate);
    size_t skip = (size_t) (0.1 * rate);
    std::vector<double> out;
    out.reserve (n - skip);
    for (size_t k = 0; k < n; ++k)
    {
        double kick = k < 8 ? 20e-6 : 0.0;
        double y = f.tick (kick, G_BUS_OFF, i0);
        if (k >= skip)
            out.push_back (y);
    }
    return out;
}

static double frequency (std::span<const double> x, double rate)
{
    double sum = 0.0;
    for (double v : x)
        sum += v;
    double m = sum / (double) x.size();
    std::vector<double> ups;
    for (size_t i = 0; i + 1 < x.size(); ++i)
    {
        double w0 = x[i];
        double w1 = x[i + 1];
        if (w0 < m && w1 >= m)
            ups.push_back ((double) i + (m - w0) / (w1 - w0));
    }
    if (ups.size() < 3)
        return 0.0;
    return rate * (double) (ups.size() - 1) / (ups[ups.size() - 1] - ups[0]);
}

double oscillation (double rate, const FilterTrims& trims, std::span<const Input> ins)
{
    double i0 = trims.expo().current (ins, 25.0);
    auto x = run (rate, trims, 0.0, i0, 0.1);
    return frequency (std::span<const double> (x).subspan ((size_t) (0.05 * rate)), rate);
}

bool regenerates (double rate, const FilterTrims& trims, std::span<const Input> ins, double emphasis)
{
    double i0 = trims.expo().current (ins, 25.0);
    auto x = run (rate, trims, ca72::voice::emphasis_r14_drawn (emphasis), i0, 0.4);
    auto peak = [&] (double a, double b)
    {
        double m = 0.0;
        for (size_t i = (size_t) (a * rate); i < (size_t) (b * rate); ++i)
            m = std::fmax (m, std::fabs (x[i]));
        return m;
    };
    double early = peak (0.05, 0.15);
    double late = peak (0.3, 0.4);
    return late > early || late > 0.05;
}

template <typename F>
static std::optional<double> bisect (double lo, double hi, size_t iters, F f, double target)
{
    double f_lo = f (lo);
    double f_hi = f (hi);
    if ((f_lo - target) * (f_hi - target) > 0.0)
        return std::nullopt;
    bool rising = f_hi > f_lo;
    double a = lo;
    double b = hi;
    for (size_t k = 0; k < iters; ++k)
    {
        double m = 0.5 * (a + b);
        if ((f (m) < target) == rising)
            a = m;
        else
            b = m;
    }
    return 0.5 * (a + b);
}

static double key_volts (const ca72::keyboard::Keyboard& kb, size_t k)
{
    auto r = kb.static_out (k);
    return r.is_ok() ? r.value() : (double) k * ca72::tuning::KEY_STEP;
}

FilterTrims folkman (double rate)
{
    FilterTrims t { .r39 = 0.5, .r49 = 250.0, .r73_pos = 0.5 };
    const auto ins = inputs (0.4, false, false, 0.0);
    if (auto p = bisect (
            0.0,
            1.0,
            20,
            [&] (double pos)
            {
                FilterTrims tt = t;
                tt.r73_pos = pos;
                return regenerates (rate, tt, ins, 0.75) ? 1.0 : 0.0;
            },
            0.5))
        t.r73_pos = *p;
    if (auto r = bisect (
            0.0,
            1.0,
            30,
            [&] (double r39)
            {
                FilterTrims tt = t;
                tt.r39 = r39;
                return oscillation (rate, tt, ins);
            },
            440.0))
        t.r39 = *r;
    auto kc = ca72::keyboard::KeyboardCircuit::default_();
    kc.r_floor = 0.0;
    const auto kb = ca72::keyboard::Keyboard::new_ (kc, rate);
    const double low = key_volts (kb, 4);
    const double third = key_volts (kb, 28);
    auto tuned = [&] (const FilterTrims& tr)
    {
        return bisect (
            0.05,
            0.85,
            26,
            [&] (double c) { return oscillation (rate, tr, inputs (c, true, true, low)); },
            440.0);
    };
    auto third_at = [&] (double r49)
    {
        FilterTrims tt = t;
        tt.r49 = r49;
        auto c = tuned (tt);
        return c.has_value() ? oscillation (rate, tt, inputs (*c, true, true, third)) : 0.0;
    };
    if (auto r = bisect (0.0, 500.0, 20, third_at, 1760.0))
        t.r49 = *r;
    return t;
}
} // namespace ca72::filter_cal
