#include "preamp.h"
#include "contour.h"
#include "ulp.h"
#include "vca.h"
#include "vcf.h"
#include "voice_types.h"

namespace ca72::preamp
{
using mna::Circuit;
using mna::GND;
using mna::NoConvergence;
using mna::Node;
using mna::Part;

PreampCircuit PreampCircuit::default_()
{
    return PreampCircuit { 200.0, 0.06, vcf::TIS97, vca::Q2N4058, contour::Q2N3392, vcf::TIS93, vcf::TIS92 };
}

double Plain::high (double x, double& state, double tau, double h)
{
    const double g = h / (2.0 * tau);
    const double v = (x - state) * g / (1.0 + g);
    const double y = v + state;
    state = y + v;
    return x - y;
}

double Plain::clip (double u)
{
    constexpr double LO = -7.3;
    constexpr double HI = 9.9;
    constexpr double K = 0.3;
    if (u > HI + K)
        return HI;
    if (u > HI - K)
    {
        const double d = u - (HI - K);
        return u - d * d / (4.0 * K);
    }
    if (u < LO - K)
        return LO;
    if (u < LO + K)
    {
        const double d = u - (LO + K);
        return u + d * d / (4.0 * K);
    }
    return u;
}

PreampOut Plain::tick (double v_in, double r_src, bool on, double h)
{
    constexpr double R_IN = 96.6e3;
    constexpr double A = 725.6;
    const double rt = r_src + 1e3 + R_IN;
    const double vb = R_IN / rt * high (v_in, lp_in, 0.1e-6 * rt, h);
    constexpr double R62 = 1e3;
    const double k = R62 / (R61 + R62);
    const double amp = clip (A * (vb - v26 * (1.0 - k)) / (1.0 + A * k));
    const double node = (amp * R62 + v26 * R61) / (R61 + R62);
    v26 += h / 220e-6 * (node - v26) / R62;
    const double out = on ? high (amp, lp_20, 1e-6 * 33e3, h) : 0.0;
    if (fall.first != h)
        fall = { h, ulp::exp (-h / 0.122) };
    peak = std::fmax (peak * fall.second, amp);
    const double x = ca72::clamp ((peak - 1.375) / 0.40, 0.0, 1.0);
    return PreampOut { amp, out / 33e3, 1.004 * x * x * (3.0 - 2.0 * x) };
}

Delayed Delayed::new_ (double latency)
{
    auto up = resample::Interpolator::fir_sparse (2);
    auto down = resample::Decimator::fir_sparse (2);
    const double delay = 2.0 * (latency - up.delay() - down.delay());
    assert (delay >= 0.0 && delay - std::trunc (delay) == 0.0 && delay < 32.0);
    std::array<double, 32> line {};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return Delayed {
        std::move (up),
        std::move (down),
        line,
        0,
        (size_t) delay,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        { nan, nan },
        { 0.0, 0.0 },
        0.0,
        0.0,
        0.0,
    };
}

void Delayed::reset()
{
    up.reset();
    down.reset();
    line.fill (0.0);
    pos = 0;
    lp_in = 0.0;
    v26 = 0.0;
    lp_20 = 0.0;
    peak = 0.0;
    amp = 0.0;
}

double Delayed::high (double x, double& state, double k)
{
    const double v = (x - state) * k;
    const double y = v + state;
    state = y + v;
    return x - y;
}

PreampOut Delayed::tick (double v_in, double r_src, bool on, double dt)
{
    constexpr double R_IN = 96.6e3;
    constexpr double A = 725.6;
    constexpr double R62 = 1e3;
    if (key != std::pair<double, double> { dt, r_src })
    {
        key = { dt, r_src };
        const double rt = r_src + 1e3 + R_IN;
        auto pole = [dt] (double tau) {
            const double g = dt / (2.0 * tau);
            return g / (1.0 + g);
        };
        k_in = { R_IN / rt, pole (0.1e-6 * rt) };
        k_20 = pole (1e-6 * 33e3);
        k_26 = 0.5 * dt / 220e-6 / R62;
        fall = ulp::exp (-dt / 0.122);
    }
    const double vb = k_in.first * high (v_in, lp_in, k_in.second);
    std::array<double, 2> fine { 0.0, 0.0 };
    up.push (vb, fine);
    const double k = R62 / (R61 + R62);
    for (double x : fine)
    {
        line[pos] = x;
        const double xd = line[(pos + 32 - delay) % 32];
        pos = (pos + 1) % 32;
        const double a = Plain::clip (A * (xd - v26 * (1.0 - k)) / (1.0 + A * k));
        const double node = (a * R62 + v26 * R61) / (R61 + R62);
        v26 += k_26 * (node - v26);
        if (auto v = down.push (a))
            amp = *v;
    }
    const double a = amp;
    const double out = high (a, lp_20, k_20);
    peak = std::fmax (peak * fall, a);
    const double x = ca72::clamp ((peak - 1.375) / 0.40, 0.0, 1.0);
    return PreampOut { a, on ? out / 33e3 : 0.0, 1.004 * x * x * (3.0 - 2.0 * x) };
}

void Preamp::set_quality (voice::Quality q)
{
    const bool potato = q == voice::Quality::Potato;
    if (potato != plain.has_value())
    {
        plain = potato ? std::optional<Plain> (Plain::default_()) : std::nullopt;
        up.reset();
        down_amp.reset();
        down_out.reset();
    }
    const std::pair<double, double> t = q == voice::Quality::NoCompromises ? std::pair<double, double> { 1e-9, 1e-9 }
                                                                            : std::pair<double, double> { 1e-5, 1e-5 };
    net.tolerance = t;
    lamp.tolerance = t;
}

ca72::Result<Preamp, NoConvergence> Preamp::new_ (PreampCircuit circuit, double rate, double r_src)
{
    using Ret = ca72::Result<Preamp, NoConvergence>;
    const PreampCircuit& k = circuit;
    Circuit c = Circuit::new_ (NODES, HELD);
    c.set (P10, 10.0);
    c.set (N10, -10.0);
    const std::pair<Node, double> guesses[] = { { TAIL, -0.6 }, { C27, 9.3 }, { N64, 9.3 }, { AMP, 0.0 }, { B32, 0.0 } };
    for (auto [n, v] : guesses)
        c.set (n, v);
    auto r = [] (Circuit& c, Node a, Node b, double r) { return c.add (Part::Resistor (a, b, r)); };
    auto cap = [] (Circuit& c, Node a, Node b, double v) { return c.add (Part::Capacitor (a, b, v)); };
    const size_t r_src_part = r (c, SRC, N78, r_src + 1e3);
    cap (c, N78, B27, 0.1e-6);
    r (c, B27, GND, 100e3);
    c.add_bjt (C27, B27, TAIL, k.q27, 25.0, false);
    r (c, P10, C27, 27e3);
    r (c, C27, N64, 100.0);
    cap (c, N64, P10, 100e-12);
    c.add_bjt (P10, B32, TAIL, k.q27, 25.0, false);
    r (c, TAIL, N10, 150e3);
    c.add_bjt (AMP, C27, P10, k.q33, 25.0, true);
    r (c, AMP, N10, 10e3);
    r (c, B32, N61, 1e3);
    r (c, N61, AMP, R61);
    r (c, N61, N62, 1e3);
    cap (c, N62, GND, 220e-6);
    cap (c, B32, AMP, 10e-12);
    cap (c, AMP, OUT, 1e-6);
    const size_t r46 = r (c, OUT, GND, 33e3);
    {
        auto d = c.dc();
        if (d.is_err())
            return Ret::Err (d.error());
    }
    c.set_theta (0.5);
    Circuit l = Circuit::new_ (LNODES, LHELD);
    l.set (L15, 15.0);
    l.set (LN10, -10.0);
    l.set (LAMP_IN, c.v (AMP));
    const std::pair<Node, double> lguesses[] = { { E25, -0.5 }, { M, -1.8 }, { E31, -1.2 }, { LAMP, 15.0 }, { C34, 15.0 } };
    for (auto [n, v] : lguesses)
        l.set (n, v);
    r (l, LAMP_IN, B25, 10e3);
    l.add_bjt (L15, B25, E25, k.q25, 25.0, false);
    l.add (Part::Capacitor (E25, GND, 0.47e-6));
    r (l, E25, M, 100e3);
    r (l, M, LN10, 680e3);
    l.add_bjt (LN10, M, E31, k.q31, 25.0, true);
    r (l, L15, E31, 10e3);
    l.add_bjt (C34, E31, GND, k.q34, 25.0, false);
    r (l, LAMP, C34, 47.0);
    r (l, L15, LAMP, k.r_lamp);
    {
        auto d = l.dc();
        if (d.is_err())
            return Ret::Err (d.error());
    }
    auto up = resample::Interpolator::fir (SUBSTEPS);
    auto down_amp = resample::Decimator::fir (SUBSTEPS);
    const double latency = up.delay() + down_amp.delay();
    return Ret::Ok (Preamp {
        circuit,
        std::move (c),
        std::move (l),
        r_src_part,
        r46,
        1.0 / rate,
        std::move (up),
        std::move (down_amp),
        resample::Decimator::fir (SUBSTEPS),
        { 0.0, 0.0 },
        r_src,
        true,
        std::nullopt,
        Delayed::new_ (latency),
        false,
        0,
    });
}

void Preamp::set_source_resistance (double r)
{
    if (r != r_in)
    {
        r_in = r;
        net.set_resistance (r_src, r + 1e3);
    }
}

void Preamp::set_switch (bool o)
{
    if (o != on)
    {
        on = o;
        net.set_resistance (r46, o ? 33e3 : 1e12);
    }
}

bool Preamp::step (Circuit& net, double h)
{
    if (net.step (h).is_ok())
        return true;
    for (int i = 0; i < 16; ++i)
        if (! net.step (h / 16.0).is_ok())
            return false;
    return true;
}

double Preamp::latency() const
{
    return up.delay() + down_amp.delay();
}

void Preamp::set_delayed (bool o)
{
    if (o != delayed_on)
    {
        delayed_on = o;
        if (o)
            delayed.reset();
        else if (plain.has_value())
            plain = Plain::default_();
    }
}

PreampOut Preamp::tick (double v_in)
{
    if (delayed_on)
    {
        const PreampOut o = delayed.tick (v_in, r_in, on, dt);
        last = { o.amp, o.i_bus * 33e3 };
        return o;
    }
    if (plain.has_value())
    {
        const PreampOut o = plain->tick (v_in, r_in, on, dt);
        last = { o.amp, o.i_bus * 33e3 };
        return o;
    }
    const double h = dt / (double) SUBSTEPS;
    std::array<double, SUBSTEPS> fine {};
    up.push (v_in, fine);
    auto [amp, out] = last;
    for (double x : fine)
    {
        net.set (SRC, x);
        if (! step (net, h))
            failed += 1;
        if (auto a = down_amp.push (net.v (AMP)))
            amp = *a;
        if (auto o = down_out.push (net.v (OUT)))
            out = *o;
    }
    last = { amp, out };
    return outputs();
}

PreampOut Preamp::outputs()
{
    const auto [amp, out] = last;
    lamp.set (LAMP_IN, net.v (AMP));
    if (! step (lamp, dt))
        failed += 1;
    const double i_lamp = (15.0 - lamp.v (LAMP)) / circuit.r_lamp;
    return PreampOut { amp, on ? out / 33e3 : 0.0, std::fmax (i_lamp / circuit.i_lamp, 0.0) };
}
} // namespace ca72::preamp
