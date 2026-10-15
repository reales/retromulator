#include "noise.h"
#include "contour.h"
#include "ulp.h"
#include "voice_types.h"
#include <bit>
#include <numbers>

namespace ca72::noise
{
using mna::Circuit;
using mna::GND;
using mna::NoConvergence;
using mna::Node;
using mna::Part;

ca72::Result<Circuit, NoConvergence> circuit (const NoiseCircuit& k, const NoiseLoads& loads)
{
    return circuit_with (k, loads, contour::Q2N3392);
}

ca72::Result<Circuit, NoConvergence> circuit_with (const NoiseCircuit& k, const NoiseLoads& loads, const devices::Bjt& q)
{
    using Ret = ca72::Result<Circuit, NoConvergence>;
    const double re = contour::Q2N3392.re - q.re;
    Circuit c = Circuit::new_ (NODES, HELD);
    c.set (P10, 10.0);
    c.set (N10, -10.0);
    c.set (SRC, -10.0 + k.vbr);
    const std::pair<Node, double> guesses[] = {
        { NX, 8.3 },
        { E15, -2.5 },
        { B12, -9.3 },
        { C12N, -8.6 },
        { E12, -9.99 },
        { C4, 9.9 },
        { E4, -9.3 },
        { P1, -9.3 },
        { N3, -10.0 },
        { N2, -10.0 },
        { B3, -9.3 },
        { Y3, -8.0 },
        { C3N, -2.0 },
        { E3, -9.9 },
        { R1N, -2.0 },
        { B6, -9.3 },
        { Z6, -8.0 },
        { C6N, -2.0 },
        { E6, -9.9 },
    };
    for (auto [n, v] : guesses)
        c.set (n, v);
    auto r = [] (Circuit& c, Node a, Node b, double r) { c.add (Part::Resistor (a, b, r)); };
    auto cap = [] (Circuit& c, Node a, Node b, double v) { c.add (Part::Capacitor (a, b, v)); };
    r (c, P10, NX, 75e3);
    cap (c, NX, N10, 10e-6);
    r (c, NX, E15, 470e3);
    r (c, SRC, E15, k.rd);
    cap (c, E15, B12, 5.6e-6);
    r (c, B12, C12N, 4.7e6);
    r (c, P10, C12N, 100e3);
    cap (c, C12N, N10, 100e-12);
    c.add_bjt (C12N, B12, E12, q, 25.0, false);
    r (c, E12, N10, std::fmax (k.r26, 1e-3) + re);
    c.add_bjt (C4, C12N, E4, q, 25.0, false);
    r (c, P10, C4, 470.0);
    r (c, E4, N10, 3.3e3 + re);
    cap (c, C12N, E4, 100e-12);
    cap (c, E4, WHITE, 0.15e-6);
    r (c, WHITE, GND, open (loads.white));
    r (c, E4, P1, 10e3);
    cap (c, P1, N3, 0.12e-6);
    r (c, N3, N10, 3.3e3);
    cap (c, P1, N2, 0.033e-6);
    r (c, N2, N10, 220.0);
    cap (c, P1, B3, 2.5e-6);
    r (c, B3, Y3, 560e3);
    r (c, Y3, C3N, 560e3);
    cap (c, Y3, N10, 2.5e-6);
    c.add_bjt (C3N, B3, E3, q, 25.0, false);
    r (c, P10, C3N, 3.3e3);
    r (c, E3, N10, 120.0 + re);
    cap (c, C3N, PINK, 2.5e-6);
    r (c, PINK, GND, open (loads.pink));
    r (c, C3N, R1N, 10e3);
    cap (c, R1N, N10, 0.15e-6);
    cap (c, R1N, B6, 2.5e-6);
    r (c, B6, Z6, 2.2e6);
    r (c, Z6, C6N, 2.2e6);
    cap (c, Z6, N10, 0.1e-6);
    c.add_bjt (C6N, B6, E6, q, 25.0, false);
    r (c, P10, C6N, 10e3);
    r (c, E6, N10, 3.9e3 + re);
    cap (c, C6N, N10, 0.15e-6);
    cap (c, C6N, RED, 10e-6);
    r (c, RED, GND, 100e3);
    r (c, RED, GND, open (loads.red));
    auto dc = c.dc();
    if (dc.is_err())
        return Ret::Err (dc.error());
    return Ret::Ok (std::move (c));
}

ca72::Result<Noise, NoConvergence> Noise::new_ (NoiseCircuit circuit, NoiseLoads loads, double rate, uint64_t seed)
{
    using Ret = ca72::Result<Noise, NoConvergence>;
    auto full = model_with (circuit, loads, contour::Q2N3392);
    if (full.is_err())
        return Ret::Err (full.error());
    auto [model, out, full_nodes] = std::move (full.value());
    auto sys = model.discrete (1.0 / (rate * (double) OVERSAMPLE));
    devices::Bjt plain_q = contour::Q2N3392;
    plain_q.rb = 0.0;
    plain_q.re = 0.0;
    plain_q.rc = 0.0;
    auto pl = model_with (circuit, loads, plain_q);
    if (pl.is_err())
        return Ret::Err (pl.error());
    auto [plain, plain_out, plain_nodes] = std::move (pl.value());
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < std::min (full_nodes.size(), plain_nodes.size()); ++i)
        if (full_nodes[i].has_value() && plain_nodes[i].has_value())
            pairs.emplace_back (*full_nodes[i], *plain_nodes[i]);
    auto plain_sys = plain.discrete (1.0 / rate);
    return Ret::Ok (Noise {
        circuit,
        loads,
        rate,
        std::move (model),
        std::move (sys),
        out,
        { resample::Decimator::new_ (OVERSAMPLE), resample::Decimator::new_ (OVERSAMPLE), resample::Decimator::new_ (OVERSAMPLE) },
        Gauss::new_ (seed),
        { std::move (plain), std::move (plain_sys), plain_out },
        false,
        std::move (pairs),
        0.0,
        0.0,
        std::nullopt,
        LOAD_EVERY,
        OVERSAMPLE,
    });
}

ca72::Result<std::tuple<linear::SmallSignal, std::array<size_t, 3>, std::vector<std::optional<size_t>>>, NoConvergence>
    Noise::model_with (const NoiseCircuit& k, const NoiseLoads& loads, const devices::Bjt& q)
{
    using T = std::tuple<linear::SmallSignal, std::array<size_t, 3>, std::vector<std::optional<size_t>>>;
    using Ret = ca72::Result<T, NoConvergence>;
    auto cr = circuit_with (k, loads, q);
    if (cr.is_err())
        return Ret::Err (cr.error());
    Circuit c = std::move (cr.value());
    auto [g, cm, g_in] = c.small_signal (SRC);
    const size_t n = g_in.size();
    auto idx = [&] (Node node) { return c.solved_index (node).value_or (0); };
    std::vector<std::optional<size_t>> nodes;
    nodes.reserve (NODES);
    for (Node node = 0; node < NODES; ++node)
        nodes.push_back (c.solved_index (node));
    std::array<size_t, 3> outs { idx (WHITE), idx (PINK), idx (RED) };
    return Ret::Ok (T { linear::SmallSignal { n, std::move (g), std::move (cm), std::move (g_in) }, outs, std::move (nodes) });
}

void Noise::set_loads (NoiseLoads l)
{
    pending = (l != loads) ? std::optional<NoiseLoads> (l) : std::nullopt;
}

void Noise::apply_loads (NoiseLoads l)
{
    const size_t n = model.n;
    auto g = [] (double r) { return 1.0 / open (r); };
    const std::array<double, 3> old_ { loads.white, loads.pink, loads.red };
    const std::array<double, 3> new_ { l.white, l.pink, l.red };
    for (size_t k = 0; k < 3; ++k)
    {
        const size_t i = out[k];
        model.g[i * n + i] += g (new_[k]) - g (old_[k]);
        auto& m = std::get<0> (other);
        auto& o = std::get<2> (other);
        const size_t i2 = o[k], n2 = m.n;
        m.g[i2 * n2 + i2] += g (new_[k]) - g (old_[k]);
    }
    const double h = 1.0 / (rate * (double) os);
    sys.rebuild (model, h);
    loads = l;
    since_loads = 0;
}

void Noise::set_density (double v_per_rt_hz)
{
    density_ = v_per_rt_hz;
    sigma = v_per_rt_hz * std::sqrt (rate * (double) os / 2.0);
}

void Noise::set_quality (voice::Quality q)
{
    const bool potato = q == voice::Quality::Potato;
    if (potato != plain)
    {
        plain = potato;
        os = potato ? 1 : OVERSAMPLE;
        auto& m = std::get<0> (other);
        auto& osys = std::get<1> (other);
        auto& oout = std::get<2> (other);
        std::swap (model, m);
        std::swap (out, oout);
        const double h = 1.0 / (rate * (double) os);
        osys.rebuild (model, h);
        osys.carry (sys, pairs, ! potato);
        std::swap (sys, osys);
        set_density (density_);
        for (auto& d : dec)
            d.reset();
    }
}

void Noise::reseed (uint64_t seed)
{
    rng = Gauss::new_ (seed);
}

NoiseOut Noise::tick()
{
    if (since_loads != std::numeric_limits<size_t>::max())
        since_loads += 1;
    const size_t every = os == 1 ? 8 * LOAD_EVERY : LOAD_EVERY;
    if (since_loads >= every && pending.has_value())
    {
        NoiseLoads l = *pending;
        pending.reset();
        apply_loads (l);
    }
    std::array<double, 3> y { 0.0, 0.0, 0.0 };
    if (os == 1)
    {
        const double u = sigma * rng.next();
        sys.step (u);
        for (size_t k = 0; k < 3; ++k)
            y[k] = sys.x (out[k]);
    }
    else
    {
        for (size_t s = 0; s < OVERSAMPLE; ++s)
        {
            const double u = sigma * rng.next();
            sys.step (u);
            for (size_t k = 0; k < 3; ++k)
                if (auto v = dec[k].push (sys.x (out[k])))
                    y[k] = *v;
        }
    }
    return NoiseOut { y[0], y[1], y[2] };
}

double Noise::latency() const
{
    return dec[0].delay();
}

Gauss Gauss::new_ (uint64_t seed)
{
    Gauss g { seed * 6364136223846793005ull + 1442695040888963407ull, std::nullopt };
    g.next_u32();
    return g;
}

uint32_t Gauss::next_u32()
{
    const uint64_t old = state;
    state = old * 6364136223846793005ull + 1442695040888963407ull;
    const uint32_t xorshifted = (uint32_t) (((old >> 18) ^ old) >> 27);
    const uint32_t rot = (uint32_t) (old >> 59);
    return std::rotr (xorshifted, (int) rot);
}

double Gauss::uniform()
{
    return ((double) next_u32() + 1.0) / 4294967296.0;
}

double Gauss::next_uniform()
{
    return (2.0 * uniform() - 1.0) * 1.7320508075688772;
}

double Gauss::next()
{
    if (spare.has_value())
    {
        const double s = *spare;
        spare.reset();
        return s;
    }
    const double u1 = uniform();
    const double u2 = uniform();
    const double r = std::sqrt (-2.0 * ulp::log (u1));
    const double th = 2.0 * std::numbers::pi * u2;
    spare = r * ulp::sin (th);
    return r * ulp::cos (th);
}
} // namespace ca72::noise
