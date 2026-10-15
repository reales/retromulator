#include "modulation.h"
#include "contour.h"
#include "ulp.h"
#include "vca.h"

namespace ca72::modulation
{
using mna::Circuit;
using mna::GND;
using mna::NoConvergence;
using mna::Node;
using mna::Part;

double pitch_wheel_volts (double wheel)
{
    return 10.0 * 15.3 / 25.0 + 2.0 * ca72::clamp (wheel, -1.0, 1.0);
}

double mod_wheel_r_drawn (double wheel)
{
    return MOD_WHEEL_FULL_DRAWN * wheel_shape (wheel);
}

double wheel_shape (double wheel)
{
    auto taper = [] (double p) { return (ulp::pow (81.0, p) - 1.0) / 80.0; };
    return taper (ca72::clamp (wheel, 0.0, 1.0) / 3.0) / taper (1.0 / 3.0);
}

double through (std::span<const std::pair<double, double>> points, double x)
{
    x = ca72::clamp (x, 0.0, 1.0);
    const auto [x0, r0] = points[0];
    if (x <= x0)
        return r0 * wheel_shape (x) / wheel_shape (x0);
    for (size_t k = 0; k + 1 < points.size(); ++k)
    {
        const auto [xa, ra] = points[k];
        const auto [xb, rb] = points[k + 1];
        if (x <= xb)
            return ra * ulp::pow (rb / ra, (x - xa) / (xb - xa));
    }
    return points[points.size() - 1].second;
}

double position (std::span<const std::pair<double, double>> points, double r)
{
    const auto [x0, r0] = points[0];
    if (r <= r0)
    {
        const double shape = (std::fmax (r, 0.0) / r0) * wheel_shape (x0);
        const double x = 3.0 * ulp::log (1.0 + shape * (ulp::pow (81.0, 1.0 / 3.0) - 1.0)) / ulp::log (81.0);
        return ca72::clamp (x, 0.0, x0);
    }
    for (size_t k = 0; k + 1 < points.size(); ++k)
    {
        const auto [xa, ra] = points[k];
        const auto [xb, rb] = points[k + 1];
        if (r <= rb)
            return xa + (xb - xa) * ulp::log (r / ra) / ulp::log (rb / ra);
    }
    return 1.0;
}

double mod_wheel_r (double wheel)
{
    return through (MOD_WHEEL_LAW, wheel);
}

double midi_wheel (double cc)
{
    cc = ca72::clamp (cc, 0.0, 1.0);
    if (cc == 1.0)
        return 1.0;
    return position (MOD_WHEEL_LAW, through (MIDI_WHEEL_R, cc));
}

Circuit circuit (double mix)
{
    const double m = ca72::clamp (mix, 0.0, 1.0);
    Circuit c = Circuit::new_ (NODES, HELD);
    c.set (P10, 10.0);
    c.set (N10, -10.0);
    const std::pair<Node, double> guesses[] = { { SUM, 0.0 }, { NE, -0.6 }, { N17, 9.3 }, { N38, 9.3 }, { AMP, 0.0 } };
    for (auto [n, v] : guesses)
        c.set (n, v);
    auto r = [] (Circuit& c, Node a, Node b, double r) { c.add (Part::Resistor (a, b, r)); };
    r (c, NOISE, A, 24e3);
    r (c, OSC3, B, 24e3);
    r (c, A, GND, std::fmax (25e3 * m, 1e-3));
    r (c, B, GND, std::fmax (25e3 * (1.0 - m), 1e-3));
    r (c, A, SUM, 43e3);
    r (c, B, SUM, 43e3);
    r (c, AMP, SUM, 91e3);
    c.add_bjt (P10, SUM, NE, contour::Q2N3392, 25.0, false);
    c.add_bjt (N17, GND, NE, contour::Q2N3392, 25.0, false);
    r (c, NE, N10, 43e3);
    r (c, P10, N17, 6.2e3);
    c.add_bjt (AMP, N17, P10, vca::Q2N4058, 25.0, true);
    r (c, AMP, N10, 10e3);
    r (c, N17, N38, 390.0);
    c.add (Part::Capacitor (N38, GND, 0.022e-6));
    return c;
}

ca72::Result<MixTransfer, NoConvergence> transfer (double mix)
{
    using Ret = ca72::Result<MixTransfer, NoConvergence>;
    Circuit c = circuit (mix);
    auto at = [&c] (double noise, double osc3) -> ca72::Result<double, NoConvergence> {
        c.set (NOISE, noise);
        c.set (OSC3, osc3);
        auto r = c.dc();
        if (r.is_err())
            return ca72::Result<double, NoConvergence>::Err (r.error());
        return ca72::Result<double, NoConvergence>::Ok (c.v (AMP));
    };
    auto r0 = at (0.0, 0.0);
    if (r0.is_err())
        return Ret::Err (r0.error());
    const double offset = r0.value();
    auto rn = at (1.0, 0.0);
    if (rn.is_err())
        return Ret::Err (rn.error());
    const double n = rn.value() - offset;
    auto ro = at (0.0, 1.0);
    if (ro.is_err())
        return Ret::Err (ro.error());
    const double o = ro.value() - offset;
    const double drive = 2.0 / (std::fabs (o) > std::fabs (n) ? o : n);
    const auto [dn, doo] = std::fabs (o) > std::fabs (n) ? std::pair<double, double> { 0.0, drive }
                                                         : std::pair<double, double> { drive, 0.0 };
    auto rv0 = at (dn, doo);
    if (rv0.is_err())
        return Ret::Err (rv0.error());
    const double v0 = rv0.value();
    Circuit loaded = c;
    loaded.add (Part::Resistor (AMP, GND, 2.2e3));
    auto rl = loaded.dc();
    if (rl.is_err())
        return Ret::Err (rl.error());
    const double v1 = loaded.v (AMP);
    const double r_out = (v0 - v1) / (v1 / 2.2e3);
    return Ret::Ok (MixTransfer { offset, n, o, r_out });
}

ca72::Result<Modulation, NoConvergence> Modulation::new_()
{
    using Ret = ca72::Result<Modulation, NoConvergence>;
    std::vector<MixTransfer> table;
    table.reserve (MIX_POINTS);
    for (size_t k = 0; k < MIX_POINTS; ++k)
    {
        auto t = transfer ((double) k / (double) (MIX_POINTS - 1));
        if (t.is_err())
            return Ret::Err (t.error());
        table.push_back (t.value());
    }
    return Ret::Ok (Modulation { std::move (table) });
}

MixTransfer Modulation::mix (double mix) const
{
    const double x = ca72::clamp (mix, 0.0, 1.0) * (double) (MIX_POINTS - 1);
    const size_t i = std::min (x >= 0.0 ? (size_t) std::floor (x) : (size_t) 0, MIX_POINTS - 2);
    const double f = x - (double) i;
    const MixTransfer a = table[i], b = table[i + 1];
    auto l = [f] (double p, double q) { return p + (q - p) * f; };
    return MixTransfer {
        l (a.offset, b.offset),
        l (a.gain_noise, b.gain_noise),
        l (a.gain_osc3, b.gain_osc3),
        l (a.r_out, b.r_out),
    };
}

std::pair<double, double> Modulation::source_loads (double mix)
{
    const double m = ca72::clamp (mix, 0.0, 1.0);
    auto par = [] (double a, double b) { return a <= 0.0 ? 0.0 : a * b / (a + b); };
    return { 24e3 + par (25e3 * m, 43e3), 24e3 + par (25e3 * (1.0 - m), 43e3) };
}

double Modulation::line (double amp, double r_out, double wheel_r, const LineLoads& loads)
{
    double g_l = 1.0 / std::fmax (wheel_r, 0.1);
    double i_l = 0.0;
    if (loads.osc_mod)
    {
        g_l += 3.0 / 51.1e3 + 1.0 / 33e3;
        i_l += -5.0 * 3.0 / 51.1e3 + 10.0 / 33e3;
    }
    if (loads.filter_mod)
    {
        g_l += 1.0 / 33e3;
        i_l += loads.filter_node / 33e3;
    }
    const double r_src = 1e3 + r_out;
    const double line = (amp / r_src + i_l) / (1.0 / r_src + g_l);
    const double q7 = (amp + 10.0) / 10e3 + (amp - line) / r_src;
    if (q7 >= 0.0)
        return line;
    const double g_57 = 1.0 / 1e3;
    const double g_30 = 1.0 / 10e3, i_30 = -10.0 / 10e3;
    const double det = (g_30 + g_57) * (g_57 + g_l) - g_57 * g_57;
    return (g_57 * i_30 + (g_30 + g_57) * i_l) / det;
}
} // namespace ca72::modulation
