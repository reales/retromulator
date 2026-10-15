#include "keyboard.h"
#include "contour.h"
#include "vca.h"
#include "voice_types.h"

namespace ca72::keyboard
{
using mna::Circuit;
using mna::GND;
using mna::NoConvergence;
using mna::Node;
using mna::Part;

std::pair<double, double> tolerance (voice::Quality q)
{
    switch (q)
    {
        case voice::Quality::NoCompromises: return { 1e-9, 1e-9 };
        case voice::Quality::HighFidelity: return { 1e-5, 1e-6 };
        case voice::Quality::Potato: return { 1e-4, 1e-5 };
    }
    assert (false);
    return { 1e-9, 1e-9 };
}

size_t substeps (voice::Quality q)
{
    return q == voice::Quality::Potato ? 1 : SUBSTEPS;
}

KeyboardCircuit KeyboardCircuit::default_()
{
    return KeyboardCircuit {
        590.0,
        1e3,
        1e3,
        1e3,
        vcf::TIS93,
        vcf::TIS92,
        10.0,
        0.1,
        R_FLOOR,
        0.33e-6,
        4.7e6,
        43e3,
        6.2e3,
        560.0,
        3.9e3,
        1.5e3,
        330.0,
        contour::Q2N3392,
        vca::Q2N4058,
        mna::J2N4303,
        100e3,
        22e6,
        5.1e3,
        0.01e-6,
        100e3,
        1e-6,
        10e3,
        330.0,
        3.9e3,
        10e3,
    };
}

Load Load::of (size_t oscillators, bool kbd_1, bool kbd_2)
{
    const double g_osc = (double) oscillators / 51.1e3;
    const double g_filter = (kbd_1 ? 1.0 / vcf::R53 : 0.0) + (kbd_2 ? 1.0 / vcf::R54 : 0.0);
    const double g = g_osc + g_filter;
    return Load { 1.0 / g, -5.0 * g_osc / g };
}

ca72::Result<double, NoConvergence> KeyboardCircuit::source_current (double r_load) const
{
    using Ret = ca72::Result<double, NoConvergence>;
    Circuit c = Circuit::new_ (6, 2);
    c.set (1, 10.0);
    const Node b11 = 2, b9 = 3, e9 = 4, kcur = 5;
    c.set (b11, 5.0);
    c.set (b9, 4.3);
    c.set (e9, 5.0);
    c.set (kcur, 3.6);
    c.add (Part::Resistor (1, b11, r20));
    c.add (Part::Resistor (b11, GND, r22));
    c.add_bjt (1, b11, b9, q11, 25.0, false);
    c.add (Part::Resistor (b9, GND, r25));
    c.add_bjt (kcur, b9, e9, q9, 25.0, true);
    c.add (Part::Resistor (1, e9, r1));
    c.add (Part::Resistor (kcur, GND, r_load));
    auto d = c.dc();
    if (d.is_err())
        return Ret::Err (d.error());
    return Ret::Ok (c.v (kcur) / r_load);
}

ca72::Result<double, NoConvergence> KeyboardCircuit::string_current() const
{
    return source_current (r_string * (double) (KEYS - 1) + r_floor);
}

ca72::Result<std::pair<double, double>, NoConvergence> KeyboardCircuit::source_thevenin() const
{
    using Ret = ca72::Result<std::pair<double, double>, NoConvergence>;
    const double ra = r_string * (double) (KEYS - 1) + r_floor;
    const double rb = r_string + r_floor;
    auto a = source_current (ra);
    if (a.is_err())
        return Ret::Err (a.error());
    const double ia = a.value();
    auto b = source_current (rb);
    if (b.is_err())
        return Ret::Err (b.error());
    const double ib = b.value();
    const double va = ia * ra, vb = ib * rb;
    const double r_out = (va - vb) / (ib - ia);
    return Ret::Ok ({ ia * r_out + va, r_out });
}

Keyboard Keyboard::new_ (KeyboardCircuit circuit, double rate)
{
    const KeyboardCircuit& k = circuit;
    auto sc = k.string_current();
    const double string_current = sc.is_ok() ? sc.value() : 8.41e-3;
    auto th = k.source_thevenin();
    const auto [v_src, r_src] = th.is_ok() ? th.value() : std::pair<double, double> { 8.41e-3 * 1.8e6, 1.8e6 };
    Circuit c = Circuit::new_ (NODES, HELD);
    c.set (P10, 10.0);
    c.set (N10, -10.0);
    c.set (ISRC, v_src);
    c.set (LOADV, LOAD_DEFAULT.v);
    const std::pair<Node, double> guesses[] = {
        { KCUR, 3.6 },
        { S_HI, 0.0 },
        { S_LO, 0.0 },
        { TRIG, -9.5 },
        { T2, -10.0 },
        { GN, -9.4 },
        { G13, -9.4 },
        { BUS, 9.8 },
        { E23, 9.1 },
        { C23, 9.3 },
        { C14, 10.0 },
        { FB, 0.0 },
        { KO, 9.9 },
        { GIN, 9.9 },
        { C6, 0.0 },
        { G10, 0.0 },
        { D10, 9.2 },
        { OUT, 1.2 },
    };
    for (auto [n, v] : guesses)
        c.set (n, v);
    c.add (Part::Resistor (ISRC, KCUR, r_src));
    const size_t r_top = c.add (Part::Resistor (KCUR, S_HI, k.r_string * (double) (KEYS - 1)));
    const size_t r_mid = c.add (Part::Resistor (S_HI, S_LO, SHORT));
    const size_t r_bot = c.add (Part::Resistor (S_LO, GND, std::fmax (k.r_floor, SHORT)));
    const size_t r_lo = c.add (Part::Resistor (S_LO, BUS, OPEN));
    const size_t r_hi = c.add (Part::Resistor (S_HI, BUS, OPEN));
    const size_t r_trig = c.add (Part::Resistor (P10, TRIG, OPEN));
    c.add (Part::Resistor (TRIG, T2, k.r65));
    c.add (Part::Capacitor (T2, N10, k.c13));
    c.add (Part::Resistor (T2, N10, k.r34));
    const devices::Diode d = contour::D1N4004;
    c.add (Part::Diode (GN, T2, d.is, d.n * devices::vt (25.0), CR5_CAP, CR5_TT));
    c.add (Part::Resistor (GN, GIN, k.r31));
    c.add (Part::Resistor (GN, G13, k.r64));
    c.add_jfet (C6, G13, GIN, k.jfet);
    c.add (Part::Capacitor (BUS, GND, k.c9));
    c.add (Part::Resistor (P10, BUS, k.r53));
    c.add_bjt (C23, BUS, E23, k.q23, 25.0, false);
    c.add_bjt (C14, FB, E23, k.q23, 25.0, false);
    c.add (Part::Resistor (E23, N10, k.r52));
    c.add (Part::Resistor (P10, C23, k.r58));
    c.add (Part::Resistor (P10, C14, k.r51));
    c.add_bjt (KO, C23, P10, k.q24, 25.0, true);
    c.add (Part::Resistor (P10, KO, k.r59));
    c.add (Part::Resistor (KO, N10, k.r54));
    const size_t r_glide = c.add (Part::Resistor (KO, GIN, k.r61));
    c.add (Part::Capacitor (C6, GND, k.c6));
    c.add (Part::Resistor (C6, G10, k.r21));
    c.add_jfet (D10, G10, OUT, k.jfet);
    c.add (Part::Resistor (P10, D10, k.r2));
    c.add (Part::Resistor (OUT, N10, k.r18));
    c.add (Part::Resistor (OUT, FB, k.r30));
    const size_t r_load = c.add (Part::Resistor (OUT, LOADV, LOAD_DEFAULT.r));
    Keyboard kb {
        circuit,
        std::move (c),
        rate,
        string_current,
        r_top,
        r_mid,
        r_bot,
        r_lo,
        r_hi,
        r_trig,
        r_glide,
        r_load,
        std::nullopt,
        false,
        0.0,
        LOAD_DEFAULT,
        0,
        0,
        { 0.0, 0.0, 0.0, 0.0 },
        std::numeric_limits<size_t>::max(),
        false,
        voice::Quality::NoCompromises,
        0,
        0,
    };
    if (kb.net.dc().is_err())
        kb.failed += 1;
    kb.last = { kb.net.v (C6), kb.net.v (OUT), kb.net.v (T2), kb.net.v (G13) };
    return kb;
}

double Keyboard::key_voltage (size_t k) const
{
    return string_current_ * (circuit.r_string * (double) k + circuit.r_floor);
}

void Keyboard::set_quality (voice::Quality q)
{
    if (q != quality)
    {
        quality = q;
        net.tolerance = tolerance (q);
        net.predict_limit = q == voice::Quality::Potato ? std::optional<double> (PREDICT_LIMIT) : std::nullopt;
        net.step_iterations = q == voice::Quality::Potato ? STEP_ITERATIONS : 100;
    }
}

void Keyboard::set_rate (double r)
{
    if (r != rate)
    {
        flush();
        rate = r;
    }
}

double Keyboard::out() const
{
    return net.v (OUT);
}

std::optional<double> Keyboard::probe (std::string_view name) const
{
    Node n;
    if (name == "bus")
        n = BUS;
    else if (name == "c6")
        n = C6;
    else if (name == "ko")
        n = KO;
    else if (name == "t2")
        n = T2;
    else if (name == "g13")
        n = G13;
    else if (name == "out")
        n = OUT;
    else if (name == "trig")
        n = TRIG;
    else
        return std::nullopt;
    return net.v (n);
}

void Keyboard::set_glide (double r)
{
    if (r != glide)
    {
        flush();
        glide = r;
        net.set_resistance (r_glide, circuit.r61 + std::fmax (r, 0.0));
        quiet = 0;
        since_change = 0;
    }
}

void Keyboard::set_load (Load l)
{
    if (l != load)
    {
        flush();
        load = l;
        net.set_resistance (r_load, l.r);
        net.set (LOADV, l.v);
        quiet = 0;
    }
}

void Keyboard::contacts (std::optional<std::pair<size_t, size_t>> k, bool t)
{
    apply (k, t);
}

void Keyboard::apply (std::optional<std::pair<size_t, size_t>> k, bool t)
{
    if (k.has_value())
    {
        const auto [lo, hi] = *k;
        const size_t lo_c = std::min (lo, KEYS - 1);
        k = std::pair<size_t, size_t> { lo_c, std::max (std::min (hi, KEYS - 1), lo_c) };
    }
    if (k != keys)
    {
        flush();
        keys = k;
        const double rs = circuit.r_string;
        auto seg = [rs] (size_t n) { return std::fmax (rs * (double) n, SHORT); };
        if (k.has_value())
        {
            const auto [lo, hi] = *k;
            net.set_resistance (r_top, seg (KEYS - 1 - hi));
            net.set_resistance (r_mid, seg (hi - lo));
            const double floor = circuit.r_floor;
            net.set_resistance (r_bot, std::fmax (rs * (double) lo + floor, SHORT));
            net.set_resistance (r_lo, circuit.contact);
            const double r = hi > lo ? circuit.contact : OPEN;
            net.set_resistance (r_hi, r);
        }
        else
        {
            net.set_resistance (r_lo, OPEN);
            net.set_resistance (r_hi, OPEN);
        }
        quiet = 0;
        since_change = 0;
    }
    if (t != trig)
    {
        flush();
        trig = t;
        const double r = t ? circuit.contact : OPEN;
        net.set_resistance (r_trig, r);
        quiet = 0;
        since_change = 0;
    }
}

void Keyboard::flush()
{
    if (pending > 0)
    {
        const double h = (double) pending / rate;
        pending = 0;
        step (h);
    }
}

void Keyboard::step (double h)
{
    if (net.step (h).is_ok())
        return;
    subdivided += 1;
    if (! halve (h, 0))
        failed += 1;
}

bool Keyboard::halve (double h, size_t depth)
{
    for (int i = 0; i < 2; ++i)
        if (net.step (h / 2.0).is_err() && (depth >= 10 || ! halve (h / 2.0, depth + 1)))
            return false;
    return true;
}

double Keyboard::tick (std::optional<std::pair<size_t, size_t>> k, bool t)
{
    apply (k, t);
    if (quiet >= QUIET_STEPS)
    {
        pending += 1;
        if (pending < BLOCK_MAX)
            return net.v (OUT);
        const double h = (double) pending / rate;
        pending = 0;
        step (h);
    }
    else if (fast || (double) since_change < ACTIVE_S * rate)
    {
        const size_t n = substeps (quality);
        for (size_t i = 0; i < n; ++i)
            step (1.0 / (rate * (double) n));
    }
    else
    {
        step (1.0 / rate);
    }
    if (since_change != std::numeric_limits<size_t>::max())
        since_change += 1;
    const std::array<double, 4> now { net.v (C6), net.v (OUT), net.v (T2), net.v (G13) };
    double moved = 0.0;
    for (size_t i = 0; i < 4; ++i)
        moved = std::fmax (moved, std::fabs (now[i] - last[i]));
    const double out_moved = std::fmax (std::fabs (now[1] - last[1]), std::fabs (now[0] - last[0]));
    fast = out_moved > FAST_V;
    last = now;
    if (moved < QUIET_V && (double) since_change >= ACTIVE_S * rate)
        quiet += 1;
    else
        quiet = 0;
    return now[1];
}

ca72::Result<void, NoConvergence> Keyboard::settle()
{
    using Ret = ca72::Result<void, NoConvergence>;
    flush();
    double h = 1e-6;
    int q = 0;
    for (int i = 0; i < 400; ++i)
    {
        const double b0 = net.v (C6), b1 = net.v (OUT), b2 = net.v (BUS);
        const size_t f = failed;
        step (h);
        if (failed > f)
            return Ret::Err (NoConvergence { 0, std::numeric_limits<double>::infinity() });
        const double moved = std::fmax (std::fmax (std::fabs (net.v (C6) - b0), std::fabs (net.v (OUT) - b1)),
                                        std::fabs (net.v (BUS) - b2));
        q = moved < 1e-12 ? q + 1 : 0;
        if (q >= 3)
            break;
        h = std::fmin (h * 2.0, 1.0);
    }
    auto d = net.dc();
    if (d.is_err())
        return Ret::Err (d.error());
    last = { net.v (C6), net.v (OUT), net.v (T2), net.v (G13) };
    quiet = QUIET_STEPS;
    since_change = std::numeric_limits<size_t>::max();
    return Ret::Ok();
}

ca72::Result<double, NoConvergence> Keyboard::static_out (size_t k) const
{
    using Ret = ca72::Result<double, NoConvergence>;
    Keyboard kb = *this;
    kb.apply (std::pair<size_t, size_t> { k, k }, true);
    auto s = kb.settle();
    if (s.is_err())
        return Ret::Err (s.error());
    return Ret::Ok (kb.net.v (OUT));
}
} // namespace ca72::keyboard
