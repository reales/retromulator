#include "contour.h"
#include "mna.h"
#include "ulp.h"
#include "vcf.h"
#include "voice_types.h"
#include <bit>

namespace ca72::contour
{
using ca72::devices::Bjt;
using ca72::devices::BjtAt;
using ca72::devices::Diode;
using ca72::devices::DiodeAt;
using ca72::devices::series_junction_from;

namespace
{
using NpnSol = std::tuple<std::array<double, 2>, double, double>;
using Pair = std::pair<double, double>;

bool same_bjt (const Bjt& a, const Bjt& b)
{
    return a.is == b.is && a.bf == b.bf && a.ise == b.ise && a.ne == b.ne && a.vaf == b.vaf && a.ikf == b.ikf && a.br == b.br
           && a.rb == b.rb && a.re == b.re && a.rc == b.rc && a.cje == b.cje && a.vje == b.vje && a.mje == b.mje && a.cjc == b.cjc
           && a.vjc == b.vjc && a.mjc == b.mjc && a.tf == b.tf && a.xti == b.xti && a.xtb == b.xtb && a.eg == b.eg && a.tnom == b.tnom;
}

bool same_diode (const Diode& a, const Diode& b)
{
    return a.is == b.is && a.n == b.n && a.rs == b.rs && a.eg == b.eg && a.xti == b.xti;
}
} // namespace

ContourCircuit ContourCircuit::default_()
{
    return ContourCircuit {
        9.307, 10.0, -10.0, 100e3, 0.1e-6, 47e3, 10e3, 10e3, 3.3e3, 10e3, 220.0, 5.1e3, 100e3, 0.01e-6, 1.5e3, true,
        FILTER_SECTION, LOUDNESS_SECTION, Q2N3392, vcf::TIS93, Q12HG, D1N34A, D1N4004, DCR36,
    };
}

std::tuple<double, double, double> SectionState::absorption (const Section& s, double h) const
{
    double i = 0.0, g = 0.0, b = 0.0;
    for (size_t k = 0; k < 3; ++k)
    {
        const double ck = s.absorption[k].first, rk = s.absorption[k].second;
        const double vk = da[k];
        if (ck <= 0.0)
            continue;
        const double a = 0.5 * h / (rk * ck);
        i += (v - vk) / rk;
        g += 1.0 / ((1.0 + a) * rk);
        b += (vk * (1.0 - a) + a * v) / ((1.0 + a) * rk);
    }
    return { i, g, b };
}

void SectionState::absorb (const Section& s, double h, double v_old)
{
    for (size_t k = 0; k < 3; ++k)
    {
        const double ck = s.absorption[k].first, rk = s.absorption[k].second;
        double& vk = da[k];
        if (ck <= 0.0)
        {
            vk = v;
            continue;
        }
        const double a = 0.5 * h / (rk * ck);
        vk = (vk * (1.0 - a) + a * (v_old + v)) / (1.0 + a);
    }
}

bool SectionState::absorbed (const Section& s) const
{
    for (size_t k = 0; k < 3; ++k)
    {
        const double ck = s.absorption[k].first;
        if (! (ck <= 0.0 || std::fabs (v - da[k]) < SETTLED))
            return false;
    }
    return true;
}

NpnSol grounded_npn (const BjtAt& m, const Bjt& q, std::array<double, 2> x, double vb, double r, double vc, double tol)
{
    return grounded_npn_with (m, q, x, { vb, r, vc }, tol, true);
}

NpnSol grounded_npn_with (const BjtAt& m, const Bjt& q, std::array<double, 2> x, std::tuple<double, double, double> vb_r_vc, double tol, bool limit_pnj)
{
    const double vb = std::get<0> (vb_r_vc), r = std::get<1> (vb_r_vc), vc = std::get<2> (vb_r_vc);
    const double crit = limit_pnj ? mna::vcrit (m.vt, m.is) : 0.0;
    const double rb = r + q.rb;
    bool converged = false;
    for (int it = 0; it < 60; ++it)
    {
        const auto d = m.currents_d (x[0], x[1], q.vaf);
        const double vbi = vb - rb * d.ib;
        const std::array<double, 2> res {
            vbi - q.re * (d.ic + d.ib) - x[0],
            vbi - (vc - q.rc * d.ic) - x[1],
        };
        const double j00 = -rb * d.dib[0] - q.re * (d.dic[0] + d.dib[0]) - 1.0;
        const double j01 = -rb * d.dib[1] - q.re * (d.dic[1] + d.dib[1]);
        const double j10 = -rb * d.dib[0] + q.rc * d.dic[0];
        const double j11 = -rb * d.dib[1] + q.rc * d.dic[1] - 1.0;
        const double det = j00 * j11 - j01 * j10;
        const std::array<double, 2> dx {
            (res[0] * j11 - res[1] * j01) / det,
            (j00 * res[1] - j10 * res[0]) / det,
        };
        std::array<double, 2> step;
        if (limit_pnj)
        {
            for (size_t k = 0; k < 2; ++k)
                step[k] = mna::pnjlim (x[k] - dx[k], x[k], m.vt, crit) - x[k];
        }
        else
        {
            for (size_t k = 0; k < 2; ++k)
                step[k] = ca72::clamp (-dx[k], -0.2, 2.0 * m.vt);
        }
        x = { x[0] + step[0], x[1] + step[1] };
        if (std::fabs (step[0]) < tol && std::fabs (step[1]) < tol)
        {
            converged = true;
            break;
        }
    }
    if (! converged)
        unconverged::note (unconverged::Solver::ContourTransistor);
    const auto cur = m.currents (x[0], x[1], q.vaf);
    return { x, cur.first, cur.second };
}

std::pair<std::array<double, 2>, std::array<double, 2>> grounded_npn_slopes (const BjtAt& m, const Bjt& q, std::array<double, 2> x, double r)
{
    const double rb = r + q.rb;
    const auto d = m.currents_d (x[0], x[1], q.vaf);
    const double j00 = -rb * d.dib[0] - q.re * (d.dic[0] + d.dib[0]) - 1.0;
    const double j01 = -rb * d.dib[1] - q.re * (d.dic[1] + d.dib[1]);
    const double j10 = -rb * d.dib[0] + q.rc * d.dic[0];
    const double j11 = -rb * d.dib[1] + q.rc * d.dic[1] - 1.0;
    const double det = j00 * j11 - j01 * j10;
    const std::array<double, 2> x_vb { -(j11 - j01) / det, -(j00 - j10) / det };
    const std::array<double, 2> x_vc { -j01 / det, j00 / det };
    auto along = [] (const std::array<double, 2>& g, const std::array<double, 2>& dx) { return g[0] * dx[0] + g[1] * dx[1]; };
    const std::array<double, 2> dic { d.dic[0], d.dic[1] };
    const std::array<double, 2> dib { d.dib[0], d.dib[1] };
    return {
        { along (dic, x_vb), along (dic, x_vc) },
        { along (dib, x_vb), along (dib, x_vc) },
    };
}

void Contours::set_quality (voice::Quality q)
{
    lean = q != voice::Quality::NoCompromises;
    if (q == voice::Quality::NoCompromises)
    {
        tol = 1e-12;
        tol_decay = 1e-11;
    }
    else
    {
        tol = 1e-9;
        tol_decay = 1e-9;
    }
}

void Contours::set_step (double step)
{
    h = step;
}

Contours Contours::new_ (ContourCircuit circuit, double rate)
{
    SectionState st {};
    st.j7 = 0.7;
    st.j8 = 0.6;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    return Contours {
        circuit,
        25.0,
        Loads::default_(),
        1.0 / rate,
        1e-12,
        1e-11,
        circuit.vn,
        circuit.vn,
        { st, st },
        3.0,
        0.05,
        0.0,
        { 0.0, 0.0 },
        0.0,
        { 0.0, -3.0 },
        { 0.7, 0.6 },
        std::nullopt,
        std::nullopt,
        false,
        { false, false },
        false,
        std::nullopt,
        { nan, nan },
        std::nullopt,
    };
}

std::pair<std::array<BjtAt, 3>, std::array<DiodeAt, 2>> Contours::devices_and_diodes() const
{
    const ContourCircuit& c = circuit;
    const std::array<Bjt, 3> q { c.npn, c.pnp, c.q12 };
    const std::array<Diode, 2> d { c.ge, c.si };
    const uint64_t key = std::bit_cast<uint64_t> (celsius);
    if (devices.has_value())
    {
        const auto& k = std::get<0> (*devices);
        const auto& q0 = std::get<1> (*devices);
        const auto& d0 = std::get<2> (*devices);
        if (k == key && same_bjt (q0[0], q[0]) && same_bjt (q0[1], q[1]) && same_bjt (q0[2], q[2]) && same_diode (d0[0], d[0])
            && same_diode (d0[1], d[1]))
            return { std::get<3> (*devices), std::get<4> (*devices) };
    }
    const std::array<BjtAt, 3> qa { q[0].at (celsius), q[1].at (celsius), q[2].at (celsius) };
    const std::array<DiodeAt, 2> da { d[0].at (celsius), d[1].at (celsius) };
    devices = DevicesAt { key, q, d, qa, da };
    return { qa, da };
}

void Contours::settle (const Panel& panel)
{
    const size_t n = (size_t) (2.0 / h);
    for (size_t i = 0; i < n; ++i)
        tick (false, panel);
}

std::array<SectionSetup, 2> Contours::setup (const Panel& panel)
{
    if (setup_.has_value())
    {
        const auto& [p, l, t, s] = *setup_;
        if (p == panel && l == loads && t == celsius)
            return s;
    }
    const ContourCircuit c = circuit;
    auto mk = [&] (const Section& s, const Controls& ctl) {
        const BjtAt p = c.pnp.at (celsius);
        const double pos = ca72::clamp (ctl.sustain, 0.0, 1.0);
        const double v_w = c.vp * pos;
        const double r_w = 5e3 * pos * (1.0 - pos);
        const double g13 = 1.0 / (s.r_sus + r_w);
        const double g_neg = std::isfinite (s.r_neg) ? 1.0 / s.r_neg : 0.0;
        const double g_sum = g13 + g_neg + 1.0 / s.r_gnd;
        return SectionSetup {
            (v_w * g13 + c.vn * g_neg) / g_sum,
            c.pnp.re + (1.0 / g_sum + c.pnp.rb) / (p.bf + 1.0),
            q6_emitter (s),
            peak (s),
        };
    };
    const std::array<SectionSetup, 2> s { mk (c.filter, panel.filter), mk (c.loudness, panel.loudness) };
    setup_ = std::make_tuple (panel, loads, celsius, s);
    return s;
}

double Contours::peak (const Section& s) const
{
    const ContourCircuit& c = circuit;
    const BjtAt n = c.npn.at (celsius);
    const BjtAt p = c.pnp.at (celsius);
    const double v_sat = 0.05;
    const double vbe1 = n.vt * ulp::log (2e-3 / n.is);
    const double veb5 = p.vt * ulp::log (5e-3 / p.is);
    const double i_c1 = (c.p93 - veb5 - v_sat) / s.r5;
    const double ib1_min = i_c1 / n.bf;
    const double v_c4 = vbe1 + s.r3 * ib1_min;
    const double i_c4 = (c.p93 - v_c4) / s.r4 - (v_c4 - vbe1) / s.r3;
    const double vbe4 = n.vt * ulp::log (i_c4 / n.is);
    const double i_cr3 = n.base_current (i_c4, -1.0, c.npn.vaf) + (vbe4 - v_sat) / s.r14;
    auto law = c.peak_diode.law (celsius);
    const double v_cr3 = root ([&] (double v) { return law (v).first - i_cr3; }, -0.5, 2.0, 0.2, tol) + c.peak_diode.rs * i_cr3;
    const double v_pk = vbe4 + v_cr3;
    return v_pk + s.r_top * ((v_pk - c.vn) / s.r_bot + i_cr3);
}

double Contours::q6_emitter (const Section& s) const
{
    const ContourCircuit& c = circuit;
    const BjtAt n = c.npn.at (celsius);
    const double vb4 = n.vt * ulp::log (0.5e-3 / n.is);
    const double vb7 = 2.0 + n.vt * ulp::log (1.3e-3 * n.bf / n.is);
    double v_e6 = 7.5;
    for (int it = 0; it < 6; ++it)
    {
        const double i_e6 = (v_e6 - c.vn) / s.r8 + std::fmax ((v_e6 - vb7) / s.r9, 0.0);
        const double i_b6 = n.base_current (i_e6, -1.0, c.npn.vaf);
        const double r = s.r6 + s.r5;
        const double f_c1 = (c.p93 - r * i_b6 + r * vb4 / s.r14) / (1.0 + r / s.r14);
        v_e6 = f_c1 - n.vt * ulp::log1p (i_e6 / n.is);
    }
    return v_e6;
}

double Contours::follow (const Section& s, double g, double v, double guess, std::pair<BjtAt, BjtAt> np) const
{
    const ContourCircuit& c = circuit;
    const BjtAt n = np.first, p = np.second;
    auto load = [&] (double out) { return out * g + (out - c.vn) / (s.r_top + s.r_bot); };
    auto f = [&] (double out) -> double {
        switch (s.follower)
        {
            case Follower::Complementary:
            {
                const double ie22 = std::fmax (load (out), 0.0) / (1.0 + p.bf);
                const double ib22 = ie22 / (1.0 + n.bf);
                return v - s.r_in * ib22 - n.vt * ulp::log1p (ie22 / n.is) - out;
            }
            case Follower::Darlington:
            {
                const double ie2 = std::fmax (load (out), 0.0);
                const double ib2 = ie2 / (1.0 + n.bf);
                const double ib3 = ib2 / (1.0 + n.bf);
                return v - s.r_in * ib3 - n.vt * ulp::log1p (ib2 / n.is) - n.vt * ulp::log1p (ie2 / n.is) - out;
            }
        }
        assert (false);
        return 0.0;
    };
    assert (f (c.vn) >= 0.0);
    return root_dir (f, c.vn, v + 1.0, ca72::clamp (guess, c.vn, v + 1.0), false, tol);
}

ContourOut Contours::tick (bool key, const Panel& panel)
{
    const std::array<SectionSetup, 2> setups = setup (panel);
    const ContourCircuit c = circuit;
    const double hh = h;
    const double tl = tol, tl_decay = tol_decay;
    const auto devs = devices_and_diodes();
    const BjtAt npn = devs.first[0], pnp = devs.first[1], q12d = devs.first[2];
    const DiodeAt ge_at = devs.second[0], si_at = devs.second[1];
    auto ge = [ge_at] (double v) { return ge_at.current (v); };
    auto si = [si_at] (double v) { return si_at.current (v); };
    auto base = npn.base_law();
    auto pnp_base = pnp.base_law();

    double t2_inf, tau;
    if (key)
    {
        const double r = c.r65 * c.r34 / (c.r65 + c.r34);
        t2_inf = (c.vp * c.r34 + c.vn * c.r65) / (c.r65 + c.r34);
        tau = r * c.c13;
    }
    else
    {
        t2_inf = c.vn;
        tau = c.r34 * c.c13;
    }
    const double t2_old = t2;
    t2 = t2_inf + (t2 - t2_inf) * ulp::exp (-hh / tau);
    const double kb = key ? c.vp : t2;
    const uint64_t cbits = std::bit_cast<uint64_t> (celsius);
    const std::optional<std::tuple<bool, Panel, Loads, uint64_t>> inputs_now { std::make_tuple (key, panel, loads, cbits) };
    const bool same = inputs_now == inputs;
    const std::optional<std::tuple<bool, bool, uint64_t>> trig_now { std::make_tuple (key, panel.s_trig, cbits) };
    const bool same_trig = lean ? trig_now == trig_inputs : same;
    trig_inputs = trig_now;
    if (! same)
    {
        inputs = inputs_now;
        sec_settled = { false, false };
    }
    if (! same_trig)
        trig_settled = false;

    const double vb4 = npn.vt * ulp::log (0.5e-3 / npn.is);
    const std::array<double, 2> j_feed0 = j_feed;
    auto feed_at = [&] (double rl, double r, double j) { return series_junction_from (rl - vb4, r + c.ge.rs, ge, j); };
    const double j12b0 = j12b;
    auto base12 = q12d.base_law();
    auto q12_base = [&] (double r) { return series_junction_from (r, c.r19 + c.q12.rb, base12, j12b0); };
    const double j10w = j10;
    const bool is_lean = lean;
    std::array<double, 2> x20c = x20;
    auto q20 = [&] (double v7a, double rsta) -> NpnSol {
        if (is_lean)
        {
            NpnSol s = grounded_npn_with (npn, c.npn, x20c, { v7a, c.r55, rsta }, tl, true);
            x20c = std::get<0> (s);
            return s;
        }
        return grounded_npn (npn, c.npn, x20c, v7a, c.r55, rsta, tl);
    };
    auto i60 = [&] (double v7a) { return series_junction_from (kb - v7a, c.r60 + c.ge.rs, ge, j10w).second; };
    const double v0 = v7, rst0 = rst;
    double v7n = v0, rstn = rst0;
    const int passes = trig_settled ? 0 : 2;
    double f_old = 0.0;
    if (passes > 0)
    {
        const double ia = i60 (v0);
        const double ib = std::get<2> (q20 (v0, rst0));
        f_old = (ia - ib) / c.c7;
    }
    auto through = [] (double g, double r) { return g / (1.0 + r * g); };
    [[maybe_unused]] auto solved = [&] (double vb, double r, double vc) {
        return grounded_npn_with (npn, c.npn, { 0.7, 0.6 }, { vb, r, vc }, 1e-12, true);
    };
    for (int pass_t = 0; pass_t < passes; ++pass_t)
    {
        const double r_now = rstn;
        auto g7_by = [&] (double v, const NpnSol& s) -> Pair {
            const std::array<double, 2> x = std::get<0> (s);
            const double ib = std::get<2> (s);
            const auto sj = series_junction_from (kb - v, c.r60 + c.ge.rs, ge, j10w);
            const double vj = sj.first, i = sj.second;
            const double di = -through (ge (vj).second, c.r60 + c.ge.rs);
            const auto slopes = grounded_npn_slopes (npn, c.npn, x, c.r55);
            const double f7 = (i - ib) / c.c7;
            const double df7 = (di - slopes.second[0]) / c.c7;
            return { v - v0 - 0.5 * hh * (f_old + f7), 1.0 - 0.5 * hh * df7 };
        };
        auto g7 = [&] (double v) { return g7_by (v, q20 (v, r_now)); };
        assert (g7_by (v0 - 20.0, solved (v0 - 20.0, c.r55, r_now)).first < 0.0);
        v7n = root_slope (g7, v0 - 20.0, v0 + 20.0, v0 + hh * f_old, true, tl);
        const double v_now = v7n;
        auto f_rst_by = [&] (double r, const NpnSol& s) -> Pair {
            const std::array<double, 2> x = std::get<0> (s);
            const double ic20 = std::get<1> (s);
            const auto slopes20 = grounded_npn_slopes (npn, c.npn, x, c.r55);
            const auto qb = q12_base (r);
            const double j12 = qb.first, ib12 = qb.second;
            const double d12 = through (base12 (j12).second, c.r19 + c.q12.rb);
            const double s_trig = panel.s_trig ? r / c.r49 : 0.0;
            const double d_trig = panel.s_trig ? 1.0 / c.r49 : 0.0;
            double f = (c.p93 - r) / c.r37 - ic20 - ib12 - s_trig;
            double df = -1.0 / c.r37 - slopes20.first[1] - d12 - d_trig;
            const std::array<double, 2> r_resets { c.filter.r_reset, c.loudness.r_reset };
            for (size_t k = 0; k < 2; ++k)
            {
                const auto fa = feed_at (r, r_resets[k], j_feed0[k]);
                f -= fa.second;
                df -= through (ge (fa.first).second, r_resets[k] + c.ge.rs);
            }
            return { f, df };
        };
        auto f_rst = [&] (double r) { return f_rst_by (r, q20 (v_now, r)); };
        assert (f_rst_by (0.0, solved (v_now, c.r55, 0.0)).first >= 0.0);
        rstn = root_slope (f_rst, 0.0, c.p93, rstn, false, tl);
    }
    if (passes > 0)
    {
        trig_settled = same_trig && std::fabs (v7n - v0) < SETTLED && std::fabs (rstn - rst0) < SETTLED && std::fabs (t2 - t2_old) < SETTLED;
        v7 = v7n;
        rst = rstn;
        j10 = series_junction_from (kb - v7n, c.r60 + c.ge.rs, ge, j10w).first;
        j12b = q12_base (rstn).first;
        x20 = std::get<0> (q20 (v7n, rstn));
    }

    std::array<double, 2> x12c = x12;
    auto q12 = [&] (double vt) -> NpnSol {
        if (is_lean)
        {
            NpnSol s = grounded_npn_with (q12d, c.q12, x12c, { rstn, c.r19, vt }, tl, true);
            x12c = std::get<0> (s);
            return s;
        }
        return grounded_npn (q12d, c.q12, x12c, rstn, c.r19, vt, tl);
    };
    const std::array<double, 2> dump_warm = dump;
    auto dump_node = [&] (double vt, std::span<const double> caps, double warm) -> double {
        auto fd = [&] (double d) {
            double sum = -0.0;
            for (double v : caps)
                sum += si (v - d).first;
            return sum - (d - vt) / c.r1401;
        };
        double lo = vt;
        for (double v : caps)
            lo = std::fmin (lo, v);
        lo -= 1.0;
        double hi = vt;
        for (double v : caps)
            hi = std::fmax (hi, v);
        hi += 1.0;
        assert (fd (lo) >= 0.0);
        if (is_lean)
        {
            auto fd_slope = [&] (double d) -> Pair {
                double i = 0.0, g = 0.0;
                for (double v : caps)
                {
                    const auto ig = si (v - d);
                    i = i + ig.first;
                    g = g + ig.second;
                }
                const double f = i - (d - vt) / c.r1401;
                const double slope = std::fabs (f) < 1.0 ? -g - 1.0 / c.r1401 : 0.0;
                return { f, slope };
            };
            const double guess = std::isfinite (warm) ? warm : vt;
            return root_slope (fd_slope, lo, hi, guess, false, tl);
        }
        return root_dir (fd, lo, hi, vt, false, tl);
    };
    auto dump_nodes = [&] (double vt, const std::array<SectionState, 2>& ss) -> std::array<double, 2> {
        if (c.dump_each)
        {
            const double a0 = ss[0].v, a1 = ss[1].v;
            const double d0 = dump_node (vt, std::span<const double> (&a0, 1), dump_warm[0]);
            const double d1 = dump_node (vt, std::span<const double> (&a1, 1), dump_warm[1]);
            return { d0, d1 };
        }
        const std::array<double, 2> both { ss[0].v, ss[1].v };
        const double d = dump_node (vt, both, dump_warm[0]);
        return { d, d };
    };
    const std::array<SectionState, 2> first = sections;
    auto f_vt_by = [&] (double vt, const NpnSol& s12, const std::array<SectionState, 2>& ss) -> Pair {
        const std::array<double, 2> x = std::get<0> (s12);
        const double ic12 = std::get<1> (s12);
        const auto slopes12 = grounded_npn_slopes (q12d, c.q12, x, c.r19);
        double dump_i = 0.0, d_dump = 0.0;
        if (! panel.decay_on)
        {
            const std::array<double, 2> d = dump_nodes (vt, first);
            auto at = [&] (double dn, double g_diodes) -> Pair {
                const double dd = 1.0 / (c.r1401 * (g_diodes + 1.0 / c.r1401));
                return { (dn - vt) / c.r1401, (dd - 1.0) / c.r1401 };
            };
            if (c.dump_each)
            {
                const Pair a0 = at (d[0], si (first[0].v - d[0]).second);
                const Pair a1 = at (d[1], si (first[1].v - d[1]).second);
                dump_i = a0.first + a1.first;
                d_dump = a0.second + a1.second;
            }
            else
            {
                const Pair a = at (d[0], si (first[0].v - d[0]).second + si (first[1].v - d[0]).second);
                dump_i = a.first;
                d_dump = a.second;
            }
        }
        const Pair s0 = si (ss[0].e7 - vt);
        const Pair s1 = si (ss[1].e7 - vt);
        return {
            (c.p93 - vt) / (c.r32 + c.r23) + s0.first + s1.first + dump_i - ic12,
            -1.0 / (c.r32 + c.r23) - s0.second - s1.second + d_dump - slopes12.first[1],
        };
    };
    auto solve_vt = [&] (const std::array<SectionState, 2>& ss, double guess) {
        auto f_vt = [&] (double vt) { return f_vt_by (vt, q12 (vt), ss); };
        return root_slope (f_vt, 0.0, c.p93, guess, false, tl);
    };
    const double vtrig_old = vtrig;
    assert (f_vt_by (0.0, grounded_npn_with (q12d, c.q12, { 0.7, 0.6 }, { rstn, c.r19, 0.0 }, 1e-12, true), sections).first >= 0.0);
    const std::array<SectionState, 2> start_sections = sections;
    const std::array<double, 2> start_j_feed = j_feed;
    const std::array<bool, 2> start_sec_settled = sec_settled;
    double vt_cur = solve_vt (first, vtrig_old);
    bool couple = false;
    if (vtrig_old - vt_cur > COUPLED)
    {
        const std::array<Section, 2> secs { c.filter, c.loudness };
        const std::array<Controls, 2> ctls { panel.filter, panel.loudness };
        for (size_t k = 0; k < 2; ++k)
        {
            const SectionState& st = first[k];
            if (std::fabs (st.v - st.e7) >= COUPLING_CURRENT * (std::fmax (ctls[k].decay, 0.0) + 5.0 + secs[k].esr))
            {
                couple = true;
                break;
            }
        }
    }
    Pair bracket { 0.0, c.p93 };
    std::array<std::optional<Pair>, 2> guess {};
    size_t pass = 0;
    ContourOut out {};
    for (;;)
    {
        sections = start_sections;
        j_feed = start_j_feed;
        sec_settled = start_sec_settled;
        std::array<std::array<double, 2>, 2> sens {};
        std::array<std::optional<Pair>, 2> stepped {};
        vtrig = vt_cur;
        x12 = std::get<0> (q12 (vt_cur));
        const std::array<double, 2> d_nodes = panel.decay_on ? std::array<double, 2> { vt_cur, vt_cur } : dump_nodes (vt_cur, first);
        if (is_lean && ! panel.decay_on)
            dump = d_nodes;
        else
            dump = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
        const bool rising = vtrig_old < 0.5 * c.p93 && vt_cur >= 0.5 * c.p93;
        out = ContourOut {};
        out.vtrig = vt_cur;
        for (size_t k = 0; k < 2; ++k)
        {
            const double d_node = d_nodes[k];
            const Section s = k == 0 ? c.filter : c.loudness;
            const Controls ctl = k == 0 ? panel.filter : panel.loudness;
            const double g = k == 0 ? loads.filter : loads.loudness;
            const SectionSetup su = setups[k];
            SectionState st = sections[k];
            const bool was_set = st.set;
            const auto fa = feed_at (rstn, s.r_reset, j_feed[k]);
            j_feed[k] = fa.first;
            const double i_feed = fa.second;
            if (i_feed > 20e-6)
                st.set = false;
            else if (rising)
                st.set = true;
            if (st.set != was_set || std::fabs (vt_cur - vtrig_old) >= SETTLED)
                sec_settled[k] = false;
            if (sec_settled[k])
            {
                if (k == 0)
                    out.filter = st.out;
                else
                    out.loudness = st.out;
                continue;
            }
            const double b_v = st.v, b_e7 = st.e7, b_out = st.out;
            auto dump_at = [&] (double v) -> Pair { return panel.decay_on ? Pair { 0.0, 0.0 } : si (v - d_node); };
            auto i_dump = [&] (double v) { return dump_at (v).first; };
            const double v_old = st.v;
            double i_da_old, g_da, b_da;
            std::tie (i_da_old, g_da, b_da) = st.absorption (s, hh);
            const double v5 = c.p93 - 0.025;
            const double ga = 1.0 / (s.r7 + std::fmax (ctl.attack, 0.0) + s.esr);
            auto terminal_attacking = [&] (double v) { return v + s.esr * ga * (v5 - v); };
            double terminal;
            if (st.set)
            {
                st.v = (s.c / hh * v_old + 0.5 * (ga * (v5 - v_old) - i_da_old + ga * v5 + b_da)) / (s.c / hh + 0.5 * (ga + g_da));
                st.attacked = true;
                terminal = terminal_attacking (st.v);
            }
            else
            {
                const double r_dec = std::fmax (ctl.decay, 0.0) + 5.0 + s.esr;
                auto emitter = [&] (double x) -> Pair {
                    const Pair id = pnp_base (x);
                    return { id.first * (pnp.bf + 1.0), id.second * (pnp.bf + 1.0) };
                };
                const double j7 = st.j7, j8 = st.j8;
                const double r7 = s.r9 + c.npn.rb;
                const double w_old = st.attacked ? 0.0 : 0.5;
                const double w_new = st.attacked ? 1.0 : 0.5;
                st.attacked = false;
                const double i_old = -(v_old - st.e7) / r_dec - i_dump (v_old);
                using Jac = std::array<std::array<double, 2>, 2>;
                auto res = [&] (double v, double e7) -> std::pair<std::array<double, 2>, Jac> {
                    const auto s7 = series_junction_from (su.v_e6 - e7, r7, base, j7);
                    const double x7 = s7.first, ib7 = s7.second;
                    const double g7 = base (x7).second;
                    double i_b7 = 0.0, d_b7 = 0.0;
                    if (ib7 > 0.0)
                    {
                        i_b7 = ib7;
                        d_b7 = -g7 / (1.0 + r7 * g7);
                    }
                    const auto s8 = series_junction_from (e7 - su.v_th8, su.r_e8, emitter, j8);
                    const double x8 = s8.first, i8 = s8.second;
                    const double g8 = emitter (x8).second;
                    const double d8 = g8 / (1.0 + std::fmax (su.r_e8, 0.0) * g8);
                    const Pair ssi = si (e7 - vt_cur);
                    const double i_si = ssi.first, g_si = ssi.second;
                    const Pair sd = dump_at (v);
                    const double i_d = sd.first, g_d = sd.second;
                    const double i = -(v - e7) / r_dec - i_d;
                    const double i_da = g_da * v - b_da;
                    const std::array<double, 2> r {
                        v - v_old - hh * (w_old * i_old + w_new * i) / s.c + 0.5 * hh * (i_da_old + i_da) / s.c,
                        (c.p93 - e7) / s.r10 + (v - e7) / r_dec + i_b7 - i8 - i_si,
                    };
                    const Jac j { {
                        { 1.0 + (w_new * (1.0 / r_dec + g_d) + 0.5 * g_da) * hh / s.c, -w_new * hh / (r_dec * s.c) },
                        { 1.0 / r_dec, -1.0 / s.r10 - 1.0 / r_dec + d_b7 - d8 - g_si },
                    } };
                    return { r, j };
                };
                double v, e7;
                if (guess[k].has_value())
                {
                    v = guess[k]->first;
                    e7 = guess[k]->second;
                }
                else
                {
                    v = v_old + (2.0 * w_old * i_old - i_da_old) * hh / s.c;
                    e7 = st.e7;
                }
                bool converged = false;
                for (int it = 0; it < 100; ++it)
                {
                    const auto [r0, j] = res (v, e7);
                    const double det = j[0][0] * j[1][1] - j[0][1] * j[1][0];
                    const double sv = (r0[0] * j[1][1] - r0[1] * j[0][1]) / det;
                    double se = (j[0][0] * r0[1] - j[1][0] * r0[0]) / det;
                    se = ca72::clamp (se, -0.2, 0.2);
                    v -= sv;
                    e7 -= se;
                    if (std::fabs (sv) < tl_decay && std::fabs (se) < tl_decay)
                    {
                        converged = true;
                        break;
                    }
                }
                if (! converged)
                    unconverged::note (unconverged::Solver::ContourDecay);
                if (! (std::isfinite (v) && std::isfinite (e7)) || v < c.vn)
                {
                    double v2 = v_old, e72 = st.e7;
                    bool settled = false;
                    for (int it = 0; it < 400; ++it)
                    {
                        const auto [r0, j] = res (v2, e72);
                        const double det = j[0][0] * j[1][1] - j[0][1] * j[1][0];
                        const double sv = ca72::clamp ((r0[0] * j[1][1] - r0[1] * j[0][1]) / det, -0.2, 0.2);
                        const double se = ca72::clamp ((j[0][0] * r0[1] - j[1][0] * r0[0]) / det, -0.2, 0.2);
                        v2 -= sv;
                        e72 -= se;
                        if (std::fabs (sv) < tl_decay && std::fabs (se) < tl_decay)
                        {
                            settled = true;
                            break;
                        }
                    }
                    if (settled && std::isfinite (v2) && std::isfinite (e72) && v2 >= c.vn)
                    {
                        v = v2;
                        e7 = e72;
                    }
                    else
                    {
                        v = v_old;
                        e7 = st.e7;
                    }
                }
                if (couple)
                {
                    const Jac jj = res (v, e7).second;
                    const std::array<double, 2> cv { 0.0, si (e7 - vt_cur).second };
                    const double det = jj[0][0] * jj[1][1] - jj[0][1] * jj[1][0];
                    sens[k] = {
                        -(cv[0] * jj[1][1] - cv[1] * jj[0][1]) / det,
                        -(jj[0][0] * cv[1] - jj[1][0] * cv[0]) / det,
                    };
                    stepped[k] = Pair { v, e7 };
                }
                st.v = v;
                st.e7 = e7;
                st.j7 = series_junction_from (su.v_e6 - e7, s.r9 + c.npn.rb, base, j7).first;
                st.j8 = series_junction_from (e7 - su.v_th8, su.r_e8, emitter, j8).first;
                terminal = v - s.esr * ((v - e7) / r_dec + i_dump (v));
            }
            st.out = follow (s, g, terminal, st.out, { npn, pnp });
            if (st.set && st.out >= su.peak)
            {
                const double v0p = b_v, o0 = b_out;
                bool absorbed = false;
                if (st.out > o0 && o0 < su.peak)
                {
                    const double v_end = st.v;
                    double vv = v0p + (su.peak - o0) * (st.v - v0p) / (st.out - o0);
                    double pk = follow (s, g, terminal_attacking (vv), su.peak, { npn, pnp });
                    if (pk > o0 && std::fabs (pk - su.peak) > 1e-6)
                    {
                        vv = v0p + (su.peak - o0) * (vv - v0p) / (pk - o0);
                        pk = follow (s, g, terminal_attacking (vv), pk, { npn, pnp });
                    }
                    const double theta = ca72::clamp ((vv - v0p) / (v_end - v0p), 0.0, 1.0);
                    st.v = vv;
                    st.absorb (s, theta * hh, v0p);
                    const double hr = (1.0 - theta) * hh;
                    if (hr > 0.0)
                    {
                        const auto ab = st.absorption (s, hr);
                        const double i0 = std::get<0> (ab), g_da2 = std::get<1> (ab), b_da2 = std::get<2> (ab);
                        const double v_at = st.v;
                        st.v = (s.c / hr * v_at - 0.5 * i0 + 0.5 * b_da2) / (s.c / hr + 0.5 * g_da2);
                        st.absorb (s, hr, v_at);
                        pk = follow (s, g, st.v, pk, { npn, pnp });
                    }
                    if (hr <= 0.0)
                        pk = follow (s, g, st.v, pk, { npn, pnp });
                    st.out = pk;
                    absorbed = true;
                }
                st.set = false;
                if (! absorbed)
                    st.absorb (s, hh, v_old);
            }
            else
            {
                st.absorb (s, hh, v_old);
            }
            sec_settled[k] = same && ! st.set && st.absorbed (s) && std::fabs (st.v - b_v) < SETTLED && std::fabs (st.e7 - b_e7) < SETTLED
                             && std::fabs (st.out - b_out) < SETTLED;
            sections[k] = st;
            if (k == 0)
                out.filter = st.out;
            else
                out.loudness = st.out;
        }
        pass += 1;
        if (! couple)
            break;
        const double gv = solve_vt (sections, vt_cur);
        const double f = gv - vt_cur;
        if (std::fabs (f) <= (is_lean ? COUPLED : tl))
            break;
        if (pass >= COUPLING_PASSES)
        {
            unconverged::note (unconverged::Solver::ContourCoupling);
            break;
        }
        if (f > 0.0)
            bracket.first = vt_cur;
        else
            bracket.second = vt_cur;
        const double slope = f_vt_by (gv, q12 (gv), sections).second;
        double moved = -0.0;
        for (size_t k = 0; k < 2; ++k)
            moved += si (sections[k].e7 - gv).second * sens[k][1];
        double next = vt_cur - f / (-moved / slope - 1.0);
        if (! (next > bracket.first && next < bracket.second))
            next = 0.5 * (bracket.first + bracket.second);
        for (size_t k = 0; k < 2; ++k)
        {
            if (stepped[k].has_value())
                guess[k] = Pair { stepped[k]->first + sens[k][0] * (next - vt_cur), stepped[k]->second + sens[k][1] * (next - vt_cur) };
            else
                guess[k] = std::nullopt;
        }
        vt_cur = next;
    }
    return out;
}

std::array<double, 2> Contours::peaks (const Panel& panel)
{
    const auto s = setup (panel);
    return { s[0].peak, s[1].peak };
}
} // namespace ca72::contour
