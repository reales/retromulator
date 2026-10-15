#include "devices.h"

#include <bit>

namespace ca72::devices
{
namespace
{
    int64_t total_key (double x)
    {
        int64_t b = std::bit_cast<int64_t> (x);
        b ^= (int64_t) ((uint64_t) (b >> 63) >> 1);
        return b;
    }
} // namespace

std::pair<double, double> degenerated (double a, double b, double two_vt)
{
    double z = std::numeric_limits<double>::quiet_NaN();
    return degenerated_from (a, b, two_vt, z);
}

std::pair<double, double> degenerated_from (double a, double b, double two_vt, double& z)
{
    double x = std::isfinite (z) ? z : a / (1.0 + b / two_vt);
    double t = 0.0;
    double last = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        t = ca72::ulp::tanh (x / two_vt);
        last = (x + b * t - a) / (1.0 + b * (1.0 - t * t) / two_vt);
        x -= last;
        if (std::fabs (last) <= 1e-15 * (1.0 + std::fabs (a)))
            break;
    }
    if (std::fabs (last) > 1e-15 * (1.0 + std::fabs (a)))
        ca72::unconverged::note (ca72::unconverged::Solver::DegeneratedPair);
    z = x;
    const double tf = std::fabs (last) <= 1e-12 * (1.0 + std::fabs (a))
                          ? t - (1.0 - t * t) * last / two_vt
                          : ca72::ulp::tanh (x / two_vt);
    const double s = (1.0 - tf * tf) / two_vt;
    return { tf, s / (1.0 + b * s) };
}

std::pair<double, double> degenerated_warm (double a, double b, double two_vt, PairWarm& w)
{
    return degenerated_warm_to (a, b, two_vt, w, 1e-15);
}

std::pair<double, double> degenerated_warm_to (double a, double b, double two_vt, PairWarm& w, double tol)
{
    double x = std::isfinite (w.z) ? w.z : a / (1.0 + b / two_vt);
    double t = 0.0;
    double last = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        t = w.tanh (x / two_vt);
        last = (x + b * t - a) / (1.0 + b * (1.0 - t * t) / two_vt);
        x -= last;
        if (std::fabs (last) <= tol * (1.0 + std::fabs (a)))
            break;
    }
    if (std::fabs (last) > tol * (1.0 + std::fabs (a)))
        ca72::unconverged::note (ca72::unconverged::Solver::DegeneratedPair);
    w.z = x;
    const double tf = std::fabs (last) <= 1e-12 * (1.0 + std::fabs (a))
                          ? t - (1.0 - t * t) * last / two_vt
                          : w.tanh (x / two_vt);
    const double s = (1.0 - tf * tf) / two_vt;
    return { tf, s / (1.0 + b * s) };
}

DiodeAt Diode::at (double celsius) const
{
    const double t = celsius + 273.15;
    const double tn = 298.15;
    const double nvt = n * K_OVER_Q * t;
    const double is_t = is
                        * ca72::ulp::pow (t / tn, xti / n)
                        * ca72::ulp::exp ((t / tn - 1.0) * eg / nvt);
    return DiodeAt { is_t, nvt };
}

BjtAt Bjt::at (double celsius) const
{
    const double t = celsius + 273.15;
    const double tn = tnom + 273.15;
    const double vt_ = K_OVER_Q * t;
    const double ratlog = ca72::ulp::log (t / tn);
    const double factlog = (t / tn - 1.0) * eg / vt_ + xti * ratlog;
    const double bfactor = ca72::ulp::exp (ratlog * xtb);
    return BjtAt {
        vt_,
        is * ca72::ulp::exp (factlog),
        bf * bfactor,
        ise * ca72::ulp::exp (factlog / ne) / bfactor,
        ne,
        ikf,
        br * bfactor,
    };
}

std::pair<double, double> BjtAt::transport (double ic, double vbc, double vaf) const
{
    const double q1 = 1.0 / (1.0 - vbc / vaf);
    const double icm = std::fmax (ic, 1e-30);
    const double qb = q1 * (1.0 + q1 * icm / ikf);
    return { icm * qb, qb };
}

double BjtAt::base_current (double ic, double vbc, double vaf) const
{
    const auto [i_f, qb] = transport (ic, vbc, vaf);
    const double vbe = vt * ca72::ulp::log (i_f / is);
    return i_f / bf + ise * ca72::ulp::exp (vbe / (ne * vt));
}

double BjtAt::base_slope (double ic, double vbc, double vaf) const
{
    const auto [i_f, qb] = transport (ic, vbc, vaf);
    const double vbe = vt * ca72::ulp::log (i_f / is);
    const double ib_ise = ise * ca72::ulp::exp (vbe / (ne * vt));
    return (1.0 / bf + ib_ise / (ne * i_f)) * transport_slope (ic, i_f, qb, vbc, vaf);
}

double BjtAt::transport_slope (double ic, double i_f, double qb, double vbc, double vaf) const
{
    const double q1 = 1.0 / (1.0 - vbc / vaf);
    const double dqb = q1 / (ikf * std::sqrt (1.0 + 4.0 * i_f / ikf));
    return qb / (1.0 - std::fmax (ic, 1e-30) * dqb);
}

double BjtAt::junction_voltage (double ic, double vbc, double vaf) const
{
    const auto [i_f, qb] = transport (ic, vbc, vaf);
    (void) qb;
    return vt * ca72::ulp::log1p (i_f / is);
}

double BjtAt::gm_factor (double ic, double vbc, double vaf) const
{
    const auto [i_f, qb] = transport (ic, vbc, vaf);
    const double q1 = 1.0 / (1.0 - vbc / vaf);
    const double dqb = q1 / (ikf * std::sqrt (1.0 + 4.0 * i_f / ikf));
    return 1.0 - i_f * dqb / qb;
}

std::pair<double, double> BjtAt::currents (double vbe, double vbc, double vaf) const
{
    const double i_f = is * ca72::ulp::expm1 (vbe / vt);
    const double i_r = is * ca72::ulp::expm1 (vbc / vt);
    const double q1 = 1.0 / (1.0 - vbc / vaf);
    const double q2 = i_f / ikf;
    const double qb = 0.5 * q1 * (1.0 + std::sqrt (std::fmax (1.0 + 4.0 * q2, 0.0)));
    const double ib = i_f / bf + ise * ca72::ulp::expm1 (vbe / (ne * vt)) + i_r / br;
    const double ic = (i_f - i_r) / qb - i_r / br;
    return { ic, ib };
}

Currents BjtAt::currents_d (double vbe, double vbc, double vaf) const
{
    auto exp_expm1 = [] (double x) -> std::pair<double, double> {
        const double e = ca72::ulp::exp (x);
        if (std::fabs (x) < 0.5)
            return { e, ca72::ulp::expm1 (x) };
        return { e, e - 1.0 };
    };
    const double xf = std::fmin (vbe / vt, 80.0);
    const double xr = std::fmin (vbc / vt, 80.0);
    const auto [ef, em1f] = exp_expm1 (xf);
    const double i_f = is * em1f;
    const double di_f = is * ef / vt;
    const auto [er, em1r] = exp_expm1 (xr);
    const double i_r = is * em1r;
    const double di_r = is * er / vt;
    const double q1 = 1.0 / (1.0 - vbc / vaf);
    const double dq1 = q1 * q1 / vaf;
    const double sq = std::sqrt (std::fmax (1.0 + 4.0 * i_f / ikf, 0.0));
    const double qb = 0.5 * q1 * (1.0 + sq);
    const double dqb_f = sq > 0.0 ? q1 * di_f / (ikf * sq) : 0.0;
    const double dqb_r = 0.5 * (1.0 + sq) * dq1;
    const double xe = std::fmin (vbe / (ne * vt), 80.0);
    const auto [ee, em1e] = exp_expm1 (xe);
    const double ib = i_f / bf + ise * em1e + i_r / br;
    const std::array<double, 2> dib = {
        di_f / bf + ise * ee / (ne * vt),
        di_r / br,
    };
    const double net = i_f - i_r;
    const double ic = net / qb - i_r / br;
    const std::array<double, 2> dic = {
        di_f / qb - net * dqb_f / (qb * qb),
        -di_r / qb - net * dqb_r / (qb * qb) - di_r / br,
    };
    return Currents { ic, ib, dic, dib };
}

std::tuple<std::array<double, 3>, double, double, double> solve_tail_pair (const TailPair& tp,
                                                                          std::array<double, 3> x,
                                                                          size_t iterations)
{
    const BjtAt& mt = tp.tail.first;
    const Bjt& qt = tp.tail.second;
    const BjtAt& mp = tp.pair.first;
    const Bjt& qp = tp.pair.second;
    const double p = tp.pnp ? -1.0 : 1.0;
    constexpr double RS = 1e3;
    const double rb = tp.rb + qt.rb;
    const double re = tp.re + qt.re;
    const double rc = tp.r_collector + qt.rc;
    const double rep = qp.re + tp.r_emitter;

    using Vec3 = std::array<double, 3>;
    using Mat3 = std::array<Vec3, 3>;
    using Eval = std::tuple<Vec3, Mat3, double, double, double>;

    auto eval = [&] (Vec3 xx) -> Eval {
        const Currents t = mt.currents_d (xx[0], xx[1], qt.vaf);
        const Currents q = mp.currents_d (xx[2], tp.pair_vbc, qp.vaf);
        const double ieh = q.ic + q.ib;
        const double dieh = q.dic[0] + q.dib[0];
        const double e = tp.mid - p * (xx[2] + qp.rb * q.ib + rep * ieh);
        const Vec3 r = {
            p * (tp.vb0 - tp.ve0) - rb * t.ib - re * (t.ic + t.ib) - xx[0],
            p * (tp.vb0 - e) - rb * t.ib + rc * t.ic - xx[1],
            (2.0 * ieh - t.ic) * RS,
        };
        auto col = [&] (size_t k) -> Vec3 {
            return {
                -rb * t.dib[k] - re * (t.dic[k] + t.dib[k]),
                -rb * t.dib[k] + rc * t.dic[k],
                -t.dic[k] * RS,
            };
        };
        const Vec3 c0 = col (0);
        const Vec3 c1 = col (1);
        const Mat3 j = { {
            { c0[0] - 1.0, c1[0], 0.0 },
            { c0[1], c1[1] - 1.0, 1.0 + qp.rb * q.dib[0] + rep * dieh },
            { c0[2], c1[2], 2.0 * dieh * RS },
        } };
        return { r, j, t.ic, t.ib, e };
    };
    auto norm = [] (const Vec3& r) { return std::fmax (std::fmax (std::fabs (r[0]), std::fabs (r[1])), std::fabs (r[2])); };
    const Vec3 vts = { mt.vt, mt.vt, mp.vt };
    Eval cur = eval (x);
    for (size_t it = 0; it < iterations; ++it)
    {
        const auto dxo = solve3 (std::get<1> (cur), std::get<0> (cur));
        if (! dxo.has_value())
            break;
        const Vec3& dx = *dxo;
        const Vec3 want = { -dx[0], -dx[1], -dx[2] };
        double f = 1.0;
        for (size_t i = 0; i < 3; ++i)
        {
            const double w = want[i];
            const double vt_ = vts[i];
            if (w > 2.0 * vt_)
                f = std::fmin (f, 2.0 * vt_ / w);
            else if (w < -0.2)
                f = std::fmin (f, -0.2 / w);
        }
        const double n0 = norm (std::get<0> (cur));
        Vec3 step = { want[0] * f, want[1] * f, want[2] * f };
        Eval next = eval ({ x[0] + step[0], x[1] + step[1], x[2] + step[2] });
        for (int h = 0; h < 8; ++h)
        {
            if (norm (std::get<0> (next)) <= n0 || n0 < 1e-9)
                break;
            step = { 0.5 * step[0], 0.5 * step[1], 0.5 * step[2] };
            next = eval ({ x[0] + step[0], x[1] + step[1], x[2] + step[2] });
        }
        x = { x[0] + step[0], x[1] + step[1], x[2] + step[2] };
        cur = next;
        if (std::fabs (step[0]) < 1e-12 && std::fabs (step[1]) < 1e-12 && std::fabs (step[2]) < 1e-12)
            return { x, std::get<2> (cur), std::get<3> (cur), std::get<4> (cur) };
    }
    ca72::unconverged::note (ca72::unconverged::Solver::Junctions);
    return { x, std::get<2> (cur), std::get<3> (cur), std::get<4> (cur) };
}

std::optional<std::array<double, 3>> solve3 (std::array<std::array<double, 3>, 3> a, std::array<double, 3> b)
{
    for (size_t col = 0; col < 3; ++col)
    {
        size_t piv = col;
        for (size_t i = col + 1; i < 3; ++i)
            if (total_key (std::fabs (a[i][col])) >= total_key (std::fabs (a[piv][col])))
                piv = i;
        if (a[piv][col] == 0.0 || ! std::isfinite (a[piv][col]))
            return std::nullopt;
        std::swap (a[col], a[piv]);
        std::swap (b[col], b[piv]);
        const std::array<double, 3> pivot = a[col];
        for (size_t r = col + 1; r < 3; ++r)
        {
            const double f = a[r][col] / pivot[col];
            for (size_t c = col; c < 3; ++c)
                a[r][c] -= f * pivot[c];
            b[r] -= f * b[col];
        }
    }
    std::array<double, 3> x {};
    for (size_t col = 3; col-- > 0;)
    {
        double sum = -0.0;
        for (size_t c = col + 1; c < 3; ++c)
            sum += a[col][c] * x[c];
        const double s = b[col] - sum;
        x[col] = s / a[col][col];
    }
    for (const double v : x)
        if (! std::isfinite (v))
            return std::nullopt;
    return x;
}
} // namespace ca72::devices
