#pragma once

#include "common.h"
#include "ulp.h"
#include "unconverged.h"

namespace ca72::devices
{
inline constexpr double K_OVER_Q = 8.617333262e-5;

inline double vt (double celsius) { return K_OVER_Q * (celsius + 273.15); }

struct BjtAt;

struct Bjt
{
    double is = 0.0;
    double bf = 0.0;
    double ise = 0.0;
    double ne = 0.0;
    double vaf = 0.0;
    double ikf = 0.0;
    double br = 0.0;
    double rb = 0.0;
    double re = 0.0;
    double rc = 0.0;
    double cje = 0.0;
    double vje = 0.0;
    double mje = 0.0;
    double cjc = 0.0;
    double vjc = 0.0;
    double mjc = 0.0;
    double tf = 0.0;
    double xti = 0.0;
    double xtb = 0.0;
    double eg = 0.0;
    double tnom = 0.0;

    bool operator== (const Bjt&) const = default;

    BjtAt at (double celsius) const;
};

inline constexpr Bjt CA3046 = {
    .is = 9.3e-16,
    .bf = 110.0,
    .ise = 9.1e-13,
    .ne = 2.0,
    .vaf = 64.0,
    .ikf = std::numeric_limits<double>::infinity(),
    .br = 1.0,
    .rb = 100.0,
    .re = 2.0,
    .rc = 20.0,
    .cje = 1.0e-12,
    .vje = 0.75,
    .mje = 0.33,
    .cjc = 1.0e-12,
    .vjc = 0.75,
    .mjc = 0.33,
    .tf = 0.28e-9,
    .xti = 3.0,
    .xtb = 1.5,
    .eg = 1.11,
    .tnom = 25.0,
};

std::pair<double, double> degenerated (double a, double b, double two_vt);
std::pair<double, double> degenerated_from (double a, double b, double two_vt, double& z);

struct PairWarm
{
    double z = 0.0;
    double at = 0.0;
    double t = 0.0;

    static const PairWarm COLD;

    bool operator== (const PairWarm&) const = default;

    double tanh (double u)
    {
        const double d = u - at;
        if (std::fabs (d) < 0.01)
        {
            const double d2 = d * d;
            const double td = d * (1.0 - d2 * (1.0 / 3.0 - d2 * (2.0 / 15.0 - d2 * (17.0 / 315.0))));
            return (t + td) / (1.0 + t * td);
        }
        const double tv = ca72::ulp::tanh_in_tick (u);
        at = u;
        t = tv;
        return tv;
    }
};

inline const PairWarm PairWarm::COLD = {
    std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::quiet_NaN(),
    std::numeric_limits<double>::quiet_NaN(),
};

std::pair<double, double> degenerated_warm (double a, double b, double two_vt, PairWarm& w);
std::pair<double, double> degenerated_warm_to (double a, double b, double two_vt, PairWarm& w, double tol);

struct DiodeAt
{
    double is = 0.0;
    double nvt = 0.0;

    bool operator== (const DiodeAt&) const = default;

    std::pair<double, double> current (double v) const
    {
        const double e = ca72::ulp::exp (std::fmin (v / nvt, 80.0));
        return { is * (e - 1.0), is * e / nvt };
    }
};

struct Diode
{
    double is = 0.0;
    double n = 0.0;
    double rs = 0.0;
    double eg = 0.0;
    double xti = 0.0;

    bool operator== (const Diode&) const = default;

    std::function<std::pair<double, double> (double)> law (double celsius) const
    {
        const DiodeAt d = at (celsius);
        return [d] (double v) { return d.current (v); };
    }

    DiodeAt at (double celsius) const;
};

template <typename F>
std::pair<double, double> series_junction_from (double v, double r, const F& law, double guess)
{
    if (r <= 0.0)
        return { v, std::get<0> (law (v)) };
    double lo, hi;
    if (v >= 0.0)
    {
        lo = 0.0;
        hi = v;
    }
    else
    {
        lo = v;
        hi = 0.0;
    }
    auto g = [&] (double vj) -> std::pair<double, double> {
        const auto [i, di] = law (vj);
        return { vj + r * i - v, 1.0 + r * di };
    };
    double vj = ca72::clamp (guess, lo, hi);
    double dx_old = hi - lo;
    for (int iter = 0; iter < 200; ++iter)
    {
        const auto [gv, dg] = g (vj);
        if (gv == 0.0)
            return { vj, std::get<0> (law (vj)) };
        if (gv > 0.0)
            hi = vj;
        else
            lo = vj;
        const double newton = vj - gv / dg;
        double dx;
        if (newton < lo || newton > hi || std::fabs (2.0 * gv) > std::fabs (dx_old * dg))
        {
            const double mid = 0.5 * (lo + hi);
            const double d = vj - mid;
            vj = mid;
            dx = d;
        }
        else
        {
            const double d = vj - newton;
            vj = newton;
            dx = d;
        }
        dx_old = dx;
        if (std::fabs (dx) < 1e-13 || hi - lo < 1e-13)
            return { vj, std::get<0> (law (vj)) };
    }
    ca72::unconverged::note (ca72::unconverged::Solver::SeriesJunction);
    return { vj, std::get<0> (law (vj)) };
}

template <typename F>
std::pair<double, double> series_junction (double v, double r, const F& law)
{
    return series_junction_from (v, r, law, v > 0.0 ? std::fmin (v, 0.7) : v);
}

struct JunctionCapacitance
{
    double cj = 0.0;
    double vj = 0.0;
    double m = 0.0;
    double lin = 0.0;

    static constexpr double FC = 0.5;

    bool operator== (const JunctionCapacitance&) const = default;

    static JunctionCapacitance new_ (double cj, double vj, double m)
    {
        return { cj, vj, m, ca72::ulp::pow (1.0 - FC, -(1.0 + m)) };
    }

    double at (double v) const
    {
        if (v < FC * vj)
            return cj * ca72::ulp::pow (1.0 - v / vj, -m);
        return cj * lin * (1.0 - FC * (1.0 + m) + m * v / vj);
    }
};

inline double junction_capacitance (double cj, double vj, double m, double v)
{
    return JunctionCapacitance::new_ (cj, vj, m).at (v);
}

struct Currents
{
    double ic = 0.0;
    double ib = 0.0;
    std::array<double, 2> dic {};
    std::array<double, 2> dib {};

    bool operator== (const Currents&) const = default;
};

struct BjtAt
{
    double vt = 0.0;
    double is = 0.0;
    double bf = 0.0;
    double ise = 0.0;
    double ne = 0.0;
    double ikf = 0.0;
    double br = 0.0;

    bool operator== (const BjtAt&) const = default;

    std::function<std::pair<double, double> (double)> base_law() const
    {
        return [self = *this] (double v) -> std::pair<double, double> {
            const double e = ca72::ulp::exp (std::fmin (v / self.vt, 80.0));
            const double e2 = ca72::ulp::exp (std::fmin (v / (self.ne * self.vt), 80.0));
            return {
                self.is / self.bf * (e - 1.0) + self.ise * (e2 - 1.0),
                self.is / self.bf * e / self.vt + self.ise * e2 / (self.ne * self.vt),
            };
        };
    }

    std::pair<double, double> transport (double ic, double vbc, double vaf) const;
    double base_current (double ic, double vbc, double vaf) const;
    double base_slope (double ic, double vbc, double vaf) const;
    double transport_slope (double ic, double i_f, double qb, double vbc, double vaf) const;
    double junction_voltage (double ic, double vbc, double vaf) const;
    double gm_factor (double ic, double vbc, double vaf) const;
    std::pair<double, double> currents (double vbe, double vbc, double vaf) const;
    Currents currents_d (double vbe, double vbc, double vaf) const;
};

// Rust's Terminals trait: a closure (ic, ib) -> (vb, ve, vc), or a type with `at`.
template <typename T>
std::tuple<double, double, double> terminals_at (const T& terminals, double ic, double ib)
{
    if constexpr (requires { terminals.at (ic, ib); })
        return terminals.at (ic, ib);
    else
        return terminals (ic, ib);
}

template <typename T>
std::tuple<std::array<double, 2>, double, double> solve_junctions_limited (const BjtAt& m,
                                                                           const Bjt& q,
                                                                           bool pnp,
                                                                           std::array<double, 2> x,
                                                                           const T& terminals,
                                                                           size_t iterations)
{
    const double p = pnp ? -1.0 : 1.0;
    auto res = [&] (std::array<double, 2> xx) -> std::tuple<std::array<double, 2>, double, double> {
        const auto [ic, ib] = m.currents (xx[0], xx[1], q.vaf);
        const auto [vb, ve, vc] = terminals_at (terminals, ic, ib);
        const double vbi = vb - p * q.rb * ib;
        const double vei = ve + p * q.re * (ic + ib);
        const double vci = vc - p * q.rc * ic;
        return { std::array<double, 2> { p * (vbi - vei) - xx[0], p * (vbi - vci) - xx[1] }, ic, ib };
    };
    for (size_t it = 0; it < iterations; ++it)
    {
        const auto [r, ic_, ib_] = res (x);
        const double h = 1e-7;
        const auto [r0, ic0, ib0] = res ({ x[0] + h, x[1] });
        const auto [r1, ic1, ib1] = res ({ x[0], x[1] + h });
        const double j00 = (r0[0] - r[0]) / h;
        const double j01 = (r1[0] - r[0]) / h;
        const double j10 = (r0[1] - r[1]) / h;
        const double j11 = (r1[1] - r[1]) / h;
        const double det = j00 * j11 - j01 * j10;
        const std::array<double, 2> dx = {
            (r[0] * j11 - r[1] * j01) / det,
            (j00 * r[1] - j10 * r[0]) / det,
        };
        const std::array<double, 2> want = { -dx[0], -dx[1] };
        double f = 1.0;
        for (const double w : want)
        {
            if (w > 2.0 * m.vt)
                f = std::fmin (f, 2.0 * m.vt / w);
            else if (w < -0.2)
                f = std::fmin (f, -0.2 / w);
        }
        const std::array<double, 2> step = { want[0] * f, want[1] * f };
        x = { x[0] + step[0], x[1] + step[1] };
        if (std::fabs (step[0]) < 1e-12 && std::fabs (step[1]) < 1e-12)
        {
            const auto [rr, ic, ib] = res (x);
            return { x, ic, ib };
        }
    }
    ca72::unconverged::note (ca72::unconverged::Solver::Junctions);
    const auto [rr, ic, ib] = res (x);
    return { x, ic, ib };
}

template <typename T>
std::tuple<std::array<double, 2>, double, double> solve_junctions (const BjtAt& m,
                                                                   const Bjt& q,
                                                                   bool pnp,
                                                                   std::array<double, 2> x,
                                                                   const T& terminals)
{
    return solve_junctions_limited (m, q, pnp, x, terminals, 60);
}

struct TailPair
{
    std::pair<const BjtAt&, const Bjt&> tail;
    std::pair<const BjtAt&, const Bjt&> pair;
    bool pnp;
    double vb0;
    double rb;
    double ve0;
    double re;
    double mid;
    double pair_vbc;
    double r_emitter;
    double r_collector;
};

std::tuple<std::array<double, 3>, double, double, double> solve_tail_pair (const TailPair& tp,
                                                                          std::array<double, 3> x,
                                                                          size_t iterations);

std::optional<std::array<double, 3>> solve3 (std::array<std::array<double, 3>, 3> a, std::array<double, 3> b);
} // namespace ca72::devices
