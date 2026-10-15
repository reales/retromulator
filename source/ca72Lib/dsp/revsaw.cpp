#include "revsaw.h"

#include "expo.h"

namespace ca72::revsaw
{
using ca72::mna::Circuit;
using ca72::mna::Depletion;
using ca72::mna::DepletionConsts;
using ca72::mna::GND;
using ca72::mna::NoConvergence;
using ca72::mna::Part;

RevSawCircuit RevSawCircuit::default_()
{
    return RevSawCircuit {
        330.0,
        2.2e-6,
        2.2e3,
        68e3,
        33e3,
        390e3,
        7.5e3,
        18e3,
        ca72::contour::Q2N3392,
        4300.0 * 4700.0 / 9000.0,
    };
}

bool RevSawCircuit::operator== (const RevSawCircuit& o) const
{
    return r164 == o.r164 && c10 == o.c10 && r165 == o.r165 && r175 == o.r175 && r171 == o.r171
           && r182 == o.r182 && r172 == o.r172 && r176 == o.r176 && ca72::expo::bjt_eq (q37, o.q37)
           && r_saw == o.r_saw;
}

ca72::Result<RevSaw, NoConvergence> RevSaw::new_ (RevSawCircuit circuit, double rate)
{
    using Ret = ca72::Result<RevSaw, NoConvergence>;
    const RevSawCircuit& k = circuit;
    Circuit c = Circuit::new_ (NODES, HELD);
    c.set (P10, 10.0);
    c.set (N10, -10.0);
    const std::array<std::pair<ca72::mna::Node, double>, 5> init { {
        { SAW, 0.0 }, { NA, 9.5 }, { B37, 0.65 }, { C37, 4.2 }, { OUT, 0.0 } } };
    for (auto [n, v] : init)
        c.set (n, v);
    c.add (Part::Resistor (SRC, SAW, k.r_saw));
    c.add (Part::Resistor (P10, NA, k.r164));
    c.add (Part::Capacitor (NA, GND, k.c10));
    c.add (Part::Resistor (NA, C37, k.r165));
    c.add (Part::Resistor (C37, B37, k.r175));
    c.add (Part::Resistor (SAW, B37, k.r171));
    c.add (Part::Resistor (B37, N10, k.r182));
    c.add_bjt (C37, B37, GND, k.q37, 25.0, false);
    c.add (Part::Resistor (C37, OUT, k.r172));
    c.add (Part::Resistor (OUT, N10, k.r176));
    {
        auto r = c.dc();
        if (r.is_err())
            return Ret::Err (r.error());
    }
    double v0 = c.v (OUT);
    Circuit probe = c;
    probe.add (Part::Resistor (OUT, N10, 10e3));
    {
        auto r = probe.dc();
        if (r.is_err())
            return Ret::Err (r.error());
    }
    double v1 = probe.v (OUT);
    double r_out = (v0 - v1) / ((v1 + 10.0) / 10e3);
    auto dep = [] (double cj0, double vj, double m) {
        Depletion d { cj0, vj, m, 0.5 };
        return std::pair<Depletion, DepletionConsts> { d, d.consts() };
    };
    const auto& q = k.q37;
    auto q37_at = q.at (25.0);
    std::array<std::pair<Depletion, DepletionConsts>, 2> deps { dep (q.cje, q.vje, q.mje), dep (q.cjc, q.vjc, q.mjc) };
    return Ret::Ok (RevSaw {
        circuit,
        std::move (c),
        1.0 / rate,
        r_out,
        q37_at,
        deps,
        std::nullopt,
        0,
    });
}

void RevSaw::set_quality (ca72::voice::Quality q)
{
    bool potato = q == ca72::voice::Quality::Potato;
    if (potato != plain.has_value())
    {
        if (potato)
        {
            double b = net.v (B37);
            double c = net.v (C37);
            std::array<double, 2> x { b, b - c };
            double nan = std::numeric_limits<double>::quiet_NaN();
            std::array<std::array<double, 2>, 2> near { { { nan, nan }, { nan, nan } } };
            std::array<double, 2> qs;
            for (size_t j = 0; j < 2; ++j)
                qs[j] = charge (j, x[j], near[j]).first;
            plain = Plain { net.v (NA), x, qs, near };
        }
        else
        {
            plain = std::nullopt;
        }
    }
}

RevSawOut RevSaw::tick (double saw_open)
{
    if (plain)
        return tick_plain (*plain, saw_open);
    net.set (SRC, saw_open);
    if (net.step (dt).is_err())
    {
        bool ok = true;
        for (int i = 0; i < 16; ++i)
        {
            if (net.step (dt / 16.0).is_err())
            {
                ok = false;
                break;
            }
        }
        if (! ok)
            failed += 1;
    }
    return RevSawOut { net.v (SAW), net.v (OUT), r_out_ };
}

std::pair<double, double> RevSaw::charge (size_t j, double v, std::array<double, 2>& near) const
{
    const auto& [d, k] = dep[j];
    double q, c;
    if (d.cj0 > 0.0)
    {
        auto qc = d.charge_near (v, near, k);
        q = qc.first;
        c = qc.second;
    }
    else
    {
        q = 0.0;
        c = 0.0;
    }
    double tf = circuit.q37.tf;
    if (j == 0 && tf > 0.0)
    {
        auto [qd, cd] = ca72::mna::diffusion_charge (v, q37.is, q37.vt, tf);
        return { q + qd, c + cd };
    }
    return { q, c };
}

RevSawOut RevSaw::tick_plain (Plain p, double saw_open)
{
    const RevSawCircuit& k = circuit;
    const auto& q = k.q37;
    const auto& m = q37;
    std::array<double, 2> q_old = p.q;
    double h = dt;
    double g_c = k.c10 / dt;
    double g_na = g_c + 1.0 / k.r164;
    double v_th = (g_c * p.v_na + 10.0 / k.r164) / g_na;
    double r_th = k.r165 + 1.0 / g_na;
    double r_s = k.r_saw + k.r171;
    double r_o = k.r172 + k.r176;
    double f0b = -1.0 / r_s - 1.0 / k.r175 - 1.0 / k.r182;
    double f0c = 1.0 / k.r175;
    double f1b = 1.0 / k.r175;
    double f1c = -1.0 / r_th - 1.0 / k.r175 - 1.0 / r_o;
    double crit = ca72::mna::vcrit (m.vt, m.is);
    auto nodes = [&] (std::array<double, 2> x, std::array<std::array<double, 2>, 2>& nr) {
        auto d = m.currents_d (x[0], x[1], q.vaf);
        auto [qe, ce] = charge (0, x[0], nr[0]);
        auto [qc, cc] = charge (1, x[1], nr[1]);
        double ie_q = (qe - q_old[0]) / h;
        double ic_q = (qc - q_old[1]) / h;
        d.ib += ie_q + ic_q;
        d.ic -= ic_q;
        d.dib[0] += ce / h;
        d.dib[1] += cc / h;
        d.dic[1] -= cc / h;
        double ie = d.ic + d.ib;
        double vb = x[0] + q.re * ie + q.rb * d.ib;
        double vc = x[0] + q.re * ie - x[1] + q.rc * d.ic;
        return std::tuple<ca72::devices::Currents, double, double, std::array<double, 2>> { d, vb, vc, { qe, qc } };
    };
    std::array<std::array<double, 2>, 2> near = p.near;
    std::array<double, 2> x = p.x;
    bool converged = false;
    for (int it = 0; it < 60; ++it)
    {
        auto [d, vb, vc, unused] = nodes (x, near);
        (void) unused;
        std::array<double, 2> f {
            (saw_open - vb) / r_s + (vc - vb) / k.r175 + (-10.0 - vb) / k.r182 - d.ib,
            (v_th - vc) / r_th - (vc - vb) / k.r175 - (vc + 10.0) / r_o - d.ic,
        };
        std::array<double, 2> die { d.dic[0] + d.dib[0], d.dic[1] + d.dib[1] };
        std::array<double, 2> dvb {
            1.0 + q.re * die[0] + q.rb * d.dib[0],
            q.re * die[1] + q.rb * d.dib[1],
        };
        std::array<double, 2> dvc {
            1.0 + q.re * die[0] + q.rc * d.dic[0],
            -1.0 + q.re * die[1] + q.rc * d.dic[1],
        };
        double j00 = f0b * dvb[0] + f0c * dvc[0] - d.dib[0];
        double j01 = f0b * dvb[1] + f0c * dvc[1] - d.dib[1];
        double j10 = f1b * dvb[0] + f1c * dvc[0] - d.dic[0];
        double j11 = f1b * dvb[1] + f1c * dvc[1] - d.dic[1];
        double det = j00 * j11 - j01 * j10;
        std::array<double, 2> dx {
            (f[0] * j11 - f[1] * j01) / det,
            (j00 * f[1] - j10 * f[0]) / det,
        };
        std::array<double, 2> next {
            ca72::mna::pnjlim (x[0] - dx[0], x[0], m.vt, crit),
            ca72::mna::pnjlim (x[1] - dx[1], x[1], m.vt, crit),
        };
        double moved = ca72::fmax (std::fabs (next[0] - x[0]), std::fabs (next[1] - x[1]));
        x = next;
        if (moved < PLAIN_TOL)
        {
            converged = true;
            break;
        }
    }
    bool fail = ! converged || ! std::isfinite (x[0]) || ! std::isfinite (x[1]);
    if (fail)
        x = p.x;
    auto [d_, vb, vc, charges] = nodes (x, near);
    (void) d_;
    failed += fail ? 1 : 0;
    p.x = x;
    p.q = charges;
    p.near = near;
    p.v_na = (g_c * p.v_na + 10.0 / k.r164 + vc / k.r165) / (g_na + 1.0 / k.r165);
    plain = p;
    return RevSawOut {
        (saw_open / k.r_saw + vb / k.r171) / (1.0 / k.r_saw + 1.0 / k.r171),
        -10.0 + (vc + 10.0) * k.r176 / r_o,
        r_out_,
    };
}
} // namespace ca72::revsaw
