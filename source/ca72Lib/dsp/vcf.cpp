#include "vcf.h"

#include <bit>
#include <numbers>

#include "fast.h"
#include "shared.h"
#include "tables.h"
#include "ulp.h"
#include "unconverged.h"
#include "voice_types.h"

namespace ca72::vcf
{
using ca72::devices::PairWarm;

namespace
{
std::array<PairWarm, 5> cold_warm()
{
    std::array<PairWarm, 5> zs;
    zs.fill (PairWarm::COLD);
    return zs;
}
} // namespace

template <typename F>
static double root (F f, double lo, double hi, double tol)
{
    double flo = f (lo);
    double fhi = f (hi);
    double x = 0.5 * (lo + hi);
    for (int k = 0; k < 100; ++k)
    {
        double secant = lo - flo * (hi - lo) / (fhi - flo);
        x = (k % 3 == 2 || ! (secant > lo && secant < hi)) ? 0.5 * (lo + hi) : secant;
        double fx = f (x);
        if (fx < 0.0)
        {
            lo = x;
            flo = fx;
        }
        else
        {
            hi = x;
            fhi = fx;
        }
        if (hi - lo < tol || fx == 0.0)
            return x;
    }
    ca72::unconverged::note (ca72::unconverged::Solver::FilterNode);
    return x;
}

double FilterExpo::current (std::span<const ca72::expo::Input> inputs, double celsius) const
{
    return solve (inputs, celsius).i0;
}

double FilterExpo::tail_node (double i0, double celsius) const
{
    constexpr double VBC = -0.9;
    auto m = ladder.at (celsius);
    const auto& q = ladder;
    double half = 0.5 * std::fmax (i0, 1e-15);
    double ib = m.base_current (half, VBC, q.vaf);
    double total = 0.0;
    for (double r : chain)
        total += r;
    total += r67;
    double up = 0.0;
    double drawn = 0.0;
    for (double r : chain)
    {
        up += r;
        drawn += up * 2.0 * ib;
    }
    double v_b5 = (vp - drawn) * r67 / total;
    double vbe = m.junction_voltage (half, VBC, q.vaf) + q.rb * ib + q.re * (half + ib);
    return v_b5 - r54 * ib - vbe;
}

std::tuple<double, double, double> FilterExpo::q28_ (double v_b, double celsius) const
{
    auto m = q28.at (celsius);
    const auto& q = q28;
    auto res = [&] (std::array<double, 2> x)
    {
        auto [ic, ib] = m.currents (x[0], x[1], q.vaf);
        double v_e = q.re * (ic + ib);
        double v_bi = v_e + x[0];
        double v_c = v_bi - x[1] + q.rc * ic;
        double v_tail = tail_node (ic, celsius) - r60 * ic;
        return std::pair { std::array<double, 2> { v_bi + q.rb * ib - v_b, v_c - v_tail },
                           std::tuple<double, double, double> { ic, ib, v_tail } };
    };
    std::array<double, 2> x { std::fmin (v_b - 0.02, 0.75), -0.5 };
    for (int it = 0; it < 100; ++it)
    {
        auto r = res (x).first;
        double h = 1e-7;
        auto r0 = res ({ x[0] + h, x[1] }).first;
        auto r1 = res ({ x[0], x[1] + h }).first;
        double j[2][2] = {
            { (r0[0] - r[0]) / h, (r1[0] - r[0]) / h },
            { (r0[1] - r[1]) / h, (r1[1] - r[1]) / h },
        };
        double det = j[0][0] * j[1][1] - j[0][1] * j[1][0];
        double dx[2] = {
            (r[0] * j[1][1] - r[1] * j[0][1]) / det,
            (j[0][0] * r[1] - j[1][0] * r[0]) / det,
        };
        auto lim = [&] (double d) { return ca72::clamp (d, -0.2, 2.0 * m.vt); };
        double step[2] = { lim (-dx[0]), lim (-dx[1]) };
        x = { x[0] + step[0], x[1] + step[1] };
        if (std::fabs (step[0]) < 1e-13 && std::fabs (step[1]) < 1e-13)
            return res (x).second;
    }
    ca72::unconverged::note (ca72::unconverged::Solver::FilterNode);
    return res (x).second;
}

std::pair<ExpoPoint, double> FilterExpo::at_node (double v_ctl, double celsius) const
{
    auto n = q26.at (celsius);
    const auto& q = q26;
    double v_open = vp * (1.0 - r39);
    double r_th = 10e3 * r39 * (1.0 - r39) + r46;
    auto side = [&] (double v)
    {
        auto [i0, ib28, v_tail] = q28_ (v, celsius);
        double ie26 = std::fmax ((v_open - v) / r_th - ib28, 1e-15);
        double vcb = vn + r45 * ie26 - v_ctl;
        double veb = root (
            [&] (double x)
            {
                auto [c, b] = n.currents (x, vcb, q.vaf);
                return c + b - ie26;
            },
            -0.5,
            1.2,
            1e-12);
        double ib26 = n.currents (veb, vcb, q.vaf).second;
        double v_eb = veb + q.rb * ib26 + q.re * ie26;
        return std::tuple<double, ExpoPoint, double> {
            v - v_eb - v_ctl,
            ExpoPoint { .i0 = i0, .v_ctl = v_ctl, .v_e26 = v, .v_tail = v_tail },
            ib26,
        };
    };
    double v = root ([&] (double v) { return std::get<0> (side (v)); }, -1.5, 1.5, 1e-12);
    auto [_, point, ib26] = side (v);
    return { point, ib26 };
}

ExpoPoint FilterExpo::solve (std::span<const ca72::expo::Input> inputs, double celsius) const
{
    const auto ng = node (inputs);
    const double i_in = ng.first;
    const double g_node = ng.second;
    double v = root ([&] (double v) { return v * g_node - i_in - at_node (v, celsius).second; }, -1.5, 1.5, 1e-12);
    return at_node (v, celsius).first;
}

std::pair<double, double> FilterExpo::node (std::span<const ca72::expo::Input> inputs) const
{
    double g_in = 0.0;
    for (const auto& i : inputs)
        g_in += 1.0 / i.r;
    double i_in = 0.0;
    for (const auto& i : inputs)
        i_in += i.v / i.r;
    return { i_in, g_in + 1.0 / (r49 + r70) };
}

std::shared_ptr<const ExpoTable> ExpoTable::shared (FilterExpo expo, double celsius)
{
    static ca72::shared::Shared<std::pair<FilterExpo, uint64_t>, ExpoTable> EXPO_TABLES =
        ca72::shared::Shared<std::pair<FilterExpo, uint64_t>, ExpoTable>::new_();
    return EXPO_TABLES.get (std::pair<FilterExpo, uint64_t> { expo, std::bit_cast<uint64_t> (celsius) },
                            [&] { return ExpoTable::new_ (expo, celsius); });
}

ExpoTable ExpoTable::new_ (FilterExpo expo, double celsius)
{
    std::vector<double> ln_i0;
    std::vector<double> ib26;
    ln_i0.reserve (EXPO_POINTS);
    ib26.reserve (EXPO_POINTS);
    for (size_t k = 0; k < EXPO_POINTS; ++k)
    {
        auto [p, ib] = expo.at_node (EXPO_V_MIN + EXPO_V_STEP * (double) k, celsius);
        ln_i0.push_back (ca72::ulp::log (p.i0));
        ib26.push_back (ib);
    }
    return ExpoTable { .expo = expo, .celsius = celsius, .ln_i0 = std::move (ln_i0), .ib26 = std::move (ib26) };
}

double ExpoTable::current (double i_in, double g_node) const
{
    return current_and_node (i_in, g_node).first;
}

std::pair<double, double> ExpoTable::current_and_node (double i_in, double g_node) const
{
    double v = i_in / g_node;
    for (int k = 0; k < 2; ++k)
        v = (i_in + table_hermite (ib26, EXPO_V_MIN, EXPO_V_STEP, v).first) / g_node;
    return { ca72::ulp::exp (table_hermite (ln_i0, EXPO_V_MIN, EXPO_V_STEP, v).first), v };
}

LadderBias LadderBias::lerp (const LadderBias& o, double t) const
{
    auto l = [t] (double a, double b) { return a + (b - a) * t; };
    auto l4 = [&] (const std::array<double, 4>& a, const std::array<double, 4>& b)
    {
        std::array<double, 4> r;
        for (size_t k = 0; k < 4; ++k)
            r[k] = l (a[k], b[k]);
        return r;
    };
    return LadderBias {
        .e = l4 (e, o.e),
        .b_in = l (b_in, o.b_in),
        .v_in = l (v_in, o.v_in),
        .v_stage = l4 (v_stage, o.v_stage),
        .vt_in = l (vt_in, o.vt_in),
        .vt_stage = l4 (vt_stage, o.vt_stage),
        .c_stage = l4 (c_stage, o.c_stage),
    };
}

static constexpr size_t BIAS_POINTS = 1024;
static constexpr std::pair<double, double> BIAS_LN { -20.723'265'836'946'41, -4.605'170'185'988'091 };

LadderBias VcfCircuit::bias (double i0, double celsius) const
{
    auto m = ladder.at (celsius);
    double vbc = ladder_vbc;
    double vaf = ladder.vaf;
    std::array<double, 4> e {};
    std::array<double, 4> v_stage {};
    double ie = std::fmax (i0, 0.0);
    for (auto& stage : e)
    {
        double half = 0.5 * ie;
        double ic = half * m.bf / (m.bf + 1.0);
        for (int k = 0; k < 3; ++k)
            ic = half - m.base_current (ic, vbc, vaf);
        ie = 2.0 * std::fmax (ic, 0.0);
        stage = ie;
    }
    std::array<double, 4> vt_stage {};
    std::array<double, 4> c_stage {};
    std::array<double, 4> slopes {};
    const auto& q = ladder;
    double c_mu = ca72::devices::junction_capacitance (q.cjc, q.vjc, q.mjc, vbc);
    auto c_je = ca72::devices::JunctionCapacitance::new_ (q.cje, q.vje, q.mje);
    for (size_t k = 0; k < 4; ++k)
    {
        double ic = 0.5 * e[k];
        double slope = m.base_slope (ic, vbc, vaf);
        slopes[k] = slope;
        v_stage[k] = (q.re + q.rb * slope / (1.0 + slope)) * e[k];
        double factor = m.gm_factor (ic, vbc, vaf);
        vt_stage[k] = m.vt / factor;
        double vbe = m.vt * ca72::ulp::log (std::fmax (ic, 1e-30) / m.is);
        double c_pi = c_je.at (vbe) + q.tf * ic * factor / m.vt;
        c_stage[k] = c + 0.5 * (c_pi + c_mu);
    }
    double b_in = slopes[0] * 0.5 * e[0];
    return LadderBias {
        .e = e,
        .b_in = b_in,
        .v_in = 2.0 * ladder.rb * b_in + ladder.re * (e[0] + 2.0 * b_in),
        .v_stage = v_stage,
        .vt_in = vt_stage[0],
        .vt_stage = vt_stage,
        .c_stage = c_stage,
    };
}

std::pair<std::array<double, STATES>, double> VcfCircuit::eval (const std::array<double, STATES>& y, const Drive& d, Jacobian* jac) const
{
    auto zs = cold_warm();
    return eval_from (y, d, jac, zs);
}

std::pair<std::array<double, STATES>, double> VcfCircuit::eval_from (const std::array<double, STATES>& y,
                                                                     const Drive& d,
                                                                     Jacobian* jac,
                                                                     std::array<PairWarm, 5>& zs) const
{
    std::optional<JacobianOut> j;
    if (jac != nullptr)
        j = JacobianOut { jac, nullptr };
    return eval_full (y, d, j, zs);
}

std::pair<std::array<double, STATES>, double> VcfCircuit::eval_full (const std::array<double, STATES>& y,
                                                                     const Drive& d,
                                                                     std::optional<JacobianOut> jac,
                                                                     std::array<PairWarm, 5>& zs) const
{
    return eval_sparse (y, d, jac, zs, true);
}

std::pair<double, double> plain_pair (double x, double b, double two_vt);

std::pair<std::array<double, STATES>, double> VcfCircuit::eval_sparse (const std::array<double, STATES>& y,
                                                                       const Drive& d,
                                                                       std::optional<JacobianOut> jac,
                                                                       std::array<PairWarm, 5>& zs,
                                                                       bool zero) const
{
    double r73b = r73 * (1.0 - r73_pos);
    double r_g = r73 * r73_pos + r76;
    double r_tot = r7 + r14 + r73b + r_g;
    double r_b5 = r67 * r_chain / (r67 + r_chain);
    double k_in = 1.0 / (1.0 + d.g_n * r54);
    double r54_seen = r54 * d.gain;
    const LadderBias& bias = d.bias;
    const auto e = bias.e;
    auto [di, ddi] = table_hermite (ca72::tables::OUT_DI, ca72::tables::OUT_DI_MIN, ca72::tables::OUT_DI_STEP, y[W]);
    auto [dib, ddib] = table_hermite (ca72::tables::OUT_DIB, ca72::tables::OUT_DIB_MIN, ca72::tables::OUT_DIB_STEP, y[W]);
    double i_in0 = (d.i_n - d.g_n * (y[Q27] + y[VB])) * k_in;
    double i_f0 = (y[NT] - r7 * di - y[Q10] - y[VB]) / r_tot;
    double a = r54_seen * i_in0 - r_g * i_f0;
    double b = bias.b_in * (r54_seen * k_in + r_g * (1.0 - r_g / r_tot)) + bias.v_in;
    auto pair = [&] (double x, double b, double two_vt, PairWarm& z)
    {
        if (d.plain)
            return plain_pair (x, b, two_vt);
        return ca72::devices::degenerated_warm (x, b, two_vt, z);
    };
    auto [ud, dud] = pair (a, b, 2.0 * bias.vt_in, zs[0]);
    double c_i = d.g_n * r54 * bias.b_in * k_in;
    double c_f = -r_g * bias.b_in / r_tot;
    double i_in = i_in0 + c_i * ud;
    double i_f = i_f0 + c_f * ud;
    std::array<double, 4> u {};
    std::array<double, 4> du {};
    for (size_t s = 0; s < 4; ++s)
        std::tie (u[s], du[s]) = pair (y[s], bias.v_stage[s], 2.0 * bias.vt_stage[s], zs[s + 1]);
    std::array<double, 4> k {
        d.p / (2.0 * bias.c_stage[0]),
        d.p / (2.0 * bias.c_stage[1]),
        d.p / (2.0 * bias.c_stage[2]),
        d.p / (2.0 * bias.c_stage[3]),
    };
    double load = y[W] / r31 + dib;
    double f3 = k[3] * (e[3] * (u[2] - u[3]) - load);
    std::array<double, STATES> f {
        -k[0] * e[0] * (ud + u[0]),
        k[1] * e[1] * (u[0] - u[1]),
        k[2] * e[2] * (u[1] - u[2]),
        f3,
        f3 - load / c5,
        i_f / c10,
        (-y[NT] / r6 - di - i_f) / c9,
        (i_in + i_f - y[VB] / r_b5) / c24,
        i_in / c27,
    };
    double out = y[NT] - r7 * (di + i_f) - r3 * di;
    if (jac.has_value())
    {
        Jacobian& j = *jac->first;
        std::array<double, STATES>* gout = jac->second;
        if (zero)
            j = Jacobian {};
        constexpr std::array<size_t, 5> COLS { W, Q10, NT, VB, Q27 };
        double gi = -d.g_n * k_in;
        std::array<double, STATES> di_in {};
        di_in[Q27] = gi;
        di_in[VB] = gi;
        std::array<double, STATES> di_f {};
        di_f[NT] = 1.0 / r_tot;
        di_f[W] = -r7 * ddi / r_tot;
        di_f[Q10] = -1.0 / r_tot;
        di_f[VB] = -1.0 / r_tot;
        std::array<double, STATES> d_ud {};
        for (size_t n : COLS)
            d_ud[n] = dud * (r54_seen * di_in[n] - r_g * di_f[n]);
        for (size_t n : COLS)
        {
            di_in[n] += c_i * d_ud[n];
            di_f[n] += c_f * d_ud[n];
            j[0][n] = -k[0] * e[0] * d_ud[n];
        }
        j[0][0] = -k[0] * e[0] * du[0];
        j[1][0] = k[1] * e[1] * du[0];
        j[1][1] = -k[1] * e[1] * du[1];
        j[2][1] = k[2] * e[2] * du[1];
        j[2][2] = -k[2] * e[2] * du[2];
        j[3][2] = k[3] * e[3] * du[2];
        j[3][3] = -k[3] * e[3] * du[3];
        double load_w = 1.0 / r31 + ddib;
        j[3][W] = -k[3] * load_w;
        j[W][2] = j[3][2];
        j[W][3] = j[3][3];
        j[W][W] = j[3][W] - load_w / c5;
        for (size_t n : COLS)
        {
            j[Q10][n] = di_f[n] / c10;
            double d_di = n == W ? ddi : 0.0;
            double d_nt = n == NT ? 1.0 / r6 : 0.0;
            j[NT][n] = (-d_nt - d_di - di_f[n]) / c9;
            double d_vb = n == VB ? 1.0 / r_b5 : 0.0;
            j[VB][n] = (di_in[n] + di_f[n] - d_vb) / c24;
            j[Q27][n] = di_in[n] / c27;
        }
        if (gout != nullptr)
        {
            auto& g = *gout;
            if (zero)
                g = std::array<double, STATES> {};
            for (size_t n : COLS)
                g[n] = -r7 * di_f[n];
            g[NT] += 1.0;
            g[W] -= (r7 + r3) * ddi;
        }
    }
    return { f, out };
}

std::pair<double, double> plain_pair (double x, double b, double two_vt)
{
    double g = 1.0 / (two_vt + b);
    double u = ca72::fast::tanh (x * g);
    return { u, (1.0 - u * u) * g };
}

template <size_t N, bool RECIP>
static std::array<double, N> solve (std::array<std::array<double, N>, N> m, std::array<double, N> r)
{
    std::array<double, N> inv {};
    for (size_t col = 0; col < N; ++col)
    {
        size_t p = col;
        for (size_t i = col + 1; i < N; ++i)
            if (std::fabs (m[i][col]) > std::fabs (m[p][col]))
                p = i;
        std::swap (m[col], m[p]);
        std::swap (r[col], r[p]);
        double d = m[col][col];
        if (std::fabs (d) < 1e-300)
            continue;
        inv[col] = 1.0 / d;
        auto pivot = m[col];
        for (size_t i = col + 1; i < N; ++i)
        {
            double f = RECIP ? m[i][col] * inv[col] : m[i][col] / d;
            if (f != 0.0)
            {
                for (size_t c = col; c < N; ++c)
                    m[i][c] -= f * pivot[c];
                r[i] -= f * r[col];
            }
        }
    }
    std::array<double, N> x {};
    for (size_t i = N; i-- > 0;)
    {
        double s = r[i];
        for (size_t k = i + 1; k < N; ++k)
            s -= m[i][k] * x[k];
        if (std::fabs (m[i][i]) < 1e-300)
            x[i] = 0.0;
        else if (RECIP)
            x[i] = s * inv[i];
        else
            x[i] = s / m[i][i];
    }
    return x;
}

template <bool RECIP>
static std::array<double, STATES> solve_loop (const Jacobian& j, double hh, const std::array<double, STATES>& r)
{
    auto a = [&] (size_t i, size_t k) { return (i == k ? 1.0 : 0.0) - hh * j[i][k]; };
    double a11 = a (1, 1), a22 = a (2, 2), a33 = a (3, 3);
    double p1, q1, p2, q2, p3, q3, s3;
    double a21 = a (2, 1);
    double a32 = a (3, 2);
    if constexpr (RECIP)
    {
        double i11 = 1.0 / a11, i22 = 1.0 / a22, i33 = 1.0 / a33;
        p1 = r[1] * i11;
        q1 = -a (1, 0) * i11;
        p2 = (r[2] - a21 * p1) * i22;
        q2 = -a21 * q1 * i22;
        p3 = (r[3] - a32 * p2) * i33;
        q3 = -a32 * q2 * i33;
        s3 = -a (3, W) * i33;
    }
    else
    {
        p1 = r[1] / a11;
        q1 = -a (1, 0) / a11;
        p2 = (r[2] - a21 * p1) / a22;
        q2 = -a21 * q1 / a22;
        p3 = (r[3] - a32 * p2) / a33;
        q3 = -a32 * q2 / a33;
        s3 = -a (3, W) / a33;
    }
    double aw2 = a (W, 2), aw3 = a (W, 3);
    double dw = a (W, W) + aw3 * s3;
    double pw, qw;
    if constexpr (RECIP)
    {
        double iw = 1.0 / dw;
        pw = (r[W] - aw2 * p2 - aw3 * p3) * iw;
        qw = -(aw2 * q2 + aw3 * q3) * iw;
    }
    else
    {
        pw = (r[W] - aw2 * p2 - aw3 * p3) / dw;
        qw = -(aw2 * q2 + aw3 * q3) / dw;
    }
    constexpr std::array<size_t, 4> IN { Q10, NT, VB, Q27 };
    std::array<std::array<double, 5>, 5> m {};
    std::array<double, 5> b {};
    double a0w = a (0, W);
    m[0][0] = a (0, 0) + a0w * qw;
    b[0] = r[0] - a0w * pw;
    for (size_t c = 0; c < IN.size(); ++c)
        m[0][c + 1] = a (0, IN[c]);
    for (size_t row = 0; row < IN.size(); ++row)
    {
        size_t kk = IN[row];
        double akw = a (kk, W);
        m[row + 1][0] = akw * qw;
        b[row + 1] = r[kk] - akw * pw;
        for (size_t c = 0; c < IN.size(); ++c)
            m[row + 1][c + 1] = a (kk, IN[c]);
    }
    auto u = solve<5, RECIP> (m, b);
    double x0 = u[0];
    double xw = pw + qw * x0;
    std::array<double, STATES> x {};
    x[0] = x0;
    x[1] = p1 + q1 * x0;
    x[2] = p2 + q2 * x0;
    x[3] = p3 + q3 * x0 + s3 * xw;
    x[W] = xw;
    for (size_t c = 0; c < IN.size(); ++c)
        x[IN[c]] = u[c + 1];
    return x;
}

static constexpr std::array<size_t, 6> JAC_ROW0 { 0, W, Q10, NT, VB, Q27 };
static constexpr std::array<size_t, 2> JAC_ROW1 { 0, 1 };
static constexpr std::array<size_t, 2> JAC_ROW2 { 1, 2 };
static constexpr std::array<size_t, 3> JAC_ROW3 { 2, 3, W };
static constexpr std::array<size_t, 5> JAC_ROW5 { W, Q10, NT, VB, Q27 };
static constexpr std::array<std::span<const size_t>, STATES> JAC_ROWS {
    std::span<const size_t> (JAC_ROW0),
    std::span<const size_t> (JAC_ROW1),
    std::span<const size_t> (JAC_ROW2),
    std::span<const size_t> (JAC_ROW3),
    std::span<const size_t> (JAC_ROW3),
    std::span<const size_t> (JAC_ROW5),
    std::span<const size_t> (JAC_ROW5),
    std::span<const size_t> (JAC_ROW5),
    std::span<const size_t> (JAC_ROW5),
};

static constexpr size_t G_HOLD = 80;

Vcf Vcf::new_ (double sample_rate, size_t oversample)
{
    auto up_n = ca72::resample::Interpolator::new_ (oversample);
    size_t os = up_n.factor();
    Vcf v;
    v.circuit = VcfCircuit::default_();
    v.expo = FilterExpo::default_();
    v.celsius = 25.0;
    v.prewarp_hz = PREWARP_HZ;
    v.oversample = os;
    v.h = 1.0 / (sample_rate * (double) os);
    v.y = {};
    v.f_prev = std::nullopt;
    v.bias = { std::numeric_limits<double>::quiet_NaN(),
               std::numeric_limits<double>::quiet_NaN(),
               VcfCircuit::default_().bias (0.0, 25.0) };
    v.up_n = std::move (up_n);
    v.up_g = ca72::resample::Interpolator::new_ (os);
    v.down = ca72::resample::Decimator::new_ (os);
    v.other = os;
    v.other_rs = { ca72::resample::Interpolator::new_ (os),
                   ca72::resample::Interpolator::new_ (os),
                   ca72::resample::Decimator::new_ (os) };
    v.bias_table = std::make_shared<const std::vector<LadderBias>>();
    v.bias_key = { 0, 0 };
    v.potato = false;
    v.recip = false;
    v.tol = 1e-10;
    v.last_iterations = 0;
    v.zs = cold_warm();
    v.gain = 1.0;
    v.warp = std::nullopt;
    v.jac = {};
    v.gout = {};
    v.g_held = { std::numeric_limits<double>::quiet_NaN(), 0, std::array<double, 16> {} };
    v.high_pass = false;
    v.mode = { 0.0, 0.0 };
    return v;
}

void Vcf::prepare (size_t oversample)
{
    auto up = ca72::resample::Interpolator::sparse (oversample);
    other = up.factor();
    other_rs = { std::move (up),
                 ca72::resample::Interpolator::sparse (oversample),
                 ca72::resample::Decimator::sparse (oversample) };
    static ca72::shared::Shared<std::pair<VcfCircuit, uint64_t>, std::vector<LadderBias>> BIAS_TABLES =
        ca72::shared::Shared<std::pair<VcfCircuit, uint64_t>, std::vector<LadderBias>>::new_();
    const double a = BIAS_LN.first;
    const double b = BIAS_LN.second;
    const VcfCircuit circuit = this->circuit;
    const double celsius = this->celsius;
    VcfCircuit key = circuit;
    key.r14 = 0.0;
    bias_table = BIAS_TABLES.get (std::pair<VcfCircuit, uint64_t> { key, std::bit_cast<uint64_t> (celsius) },
                                  [&]
                                  {
                                      std::vector<LadderBias> t;
                                      t.reserve (BIAS_POINTS);
                                      for (size_t k = 0; k < BIAS_POINTS; ++k)
                                      {
                                          double ln = a + (b - a) * (double) k / (double) (BIAS_POINTS - 1);
                                          t.push_back (circuit.bias (std::exp (ln), celsius));
                                      }
                                      return t;
                                  });
    bias_key = { std::bit_cast<uint64_t> (this->celsius), std::bit_cast<uint64_t> (this->circuit.c) };
}

void Vcf::set_drive (double gain)
{
    this->gain = gain;
}

void Vcf::set_quality (ca72::voice::Quality q)
{
    recip = q != ca72::voice::Quality::NoCompromises;
    bool potato = q == ca72::voice::Quality::Potato;
    if (potato != this->potato)
    {
        this->potato = potato;
        tol = potato ? 1e-4 : 1e-10;
        std::get<0> (bias) = std::numeric_limits<double>::quiet_NaN();
    }
}

std::optional<LadderBias> Vcf::table_bias (double i0) const
{
    if (bias_table->empty()
        || bias_key != std::array<uint64_t, 2> { std::bit_cast<uint64_t> (celsius), std::bit_cast<uint64_t> (circuit.c) }
        || i0 <= 0.0)
        return std::nullopt;
    auto [a, b] = BIAS_LN;
    double f = (std::log (i0) - a) / (b - a) * (double) (BIAS_POINTS - 1);
    if (! (f >= 0.0 && f < (double) (BIAS_POINTS - 1)))
        return std::nullopt;
    size_t i = (size_t) f;
    return (*bias_table)[i].lerp ((*bias_table)[i + 1], f - (double) i);
}

void Vcf::set_oversample (size_t oversample)
{
    if (oversample == this->oversample || oversample != other)
        return;
    double rate_h = h * (double) this->oversample;
    std::swap (up_n, std::get<0> (other_rs));
    std::swap (up_g, std::get<1> (other_rs));
    std::swap (down, std::get<2> (other_rs));
    other = this->oversample;
    this->oversample = oversample;
    h = rate_h / (double) oversample;
    up_n.reset();
    up_g.reset();
    down.reset();
    f_prev = std::nullopt;
    warp = std::nullopt;
    g_held = { std::numeric_limits<double>::quiet_NaN(), 0, std::array<double, 16> {} };
}

void Vcf::reset()
{
    zs = cold_warm();
    y = {};
    f_prev = std::nullopt;
    up_n.reset();
    up_g.reset();
    down.reset();
    g_held = { std::numeric_limits<double>::quiet_NaN(), 0, std::array<double, 16> {} };
    mode = { 0.0, 0.0 };
}

double Vcf::latency() const
{
    return up_n.delay() + down.delay();
}

double Vcf::tick (double i_bus, double g_bus, double i0)
{
    std::array<double, 16> ns {};
    std::array<double, 16> gs {};
    up_n.push (i_bus, ns);
    auto [g, n, held] = g_held;
    if (std::bit_cast<uint64_t> (g_bus) == std::bit_cast<uint64_t> (g) && n >= G_HOLD)
    {
        gs = held;
    }
    else
    {
        up_g.push (g_bus, gs);
        size_t n2 = std::bit_cast<uint64_t> (g_bus) == std::bit_cast<uint64_t> (g) ? n + 1 : 0;
        g_held = { g_bus, n2, gs };
    }
    double out = 0.0;
    size_t iters = 0;
    for (size_t k = 0; k < oversample; ++k)
    {
        double v = step (ns[k], std::fmax (gs[k], 0.0), i0, iters);
        v = filter_mode (ns[k], v);
        if (auto o = down.push (v))
            out = *o;
    }
    last_iterations = iters;
    return out;
}

double Vcf::filter_mode (double i_n, double v)
{
    double x = MODE_RT * i_n;
    double a = std::numbers::pi * MODE_HZ * h;
    auto [q, x0] = mode;
    q = (q * (1.0 - a) + a * (x + x0)) / (1.0 + a);
    mode = { q, x };
    return high_pass ? x - q - v : v;
}

Drive Vcf::drive (double i_n, double g_n, double i0)
{
    if (std::get<0> (bias) != i0 || std::get<1> (bias) != celsius)
    {
        std::optional<LadderBias> from_table = potato ? table_bias (i0) : std::nullopt;
        LadderBias b = from_table.has_value() ? *from_table : circuit.bias (i0, celsius);
        bias = { i0, celsius, b };
        warp = std::nullopt;
    }
    LadderBias b = std::get<2> (bias);
    if (warp.has_value() && warp->first == prewarp_hz)
        return Drive { .i_n = i_n, .g_n = g_n, .bias = b, .p = warp->second, .gain = gain, .plain = potato };
    double p;
    if (prewarp_hz > 0.0)
    {
        double corner = b.e[0] / (4.0 * b.c_stage[0] * b.vt_stage[0]);
        double theta = ca72::clamp (std::fmin (corner, 2.0 * std::numbers::pi * prewarp_hz) * h / 2.0, 1e-9, 1.4);
        p = ca72::ulp::tan (theta) / theta;
    }
    else
    {
        p = 1.0;
    }
    warp = std::pair<double, double> { prewarp_hz, p };
    return Drive { .i_n = i_n, .g_n = g_n, .bias = b, .p = p, .gain = gain, .plain = potato };
}

double Vcf::step (double i_n, double g_n, double i0, size_t& iters)
{
    Drive d = drive (i_n, g_n, i0);
    const VcfCircuit& c = circuit;
    const double h = this->h;
    const std::array<double, STATES> y0 = y;
    std::array<double, STATES> f_old = f_prev.has_value() ? *f_prev : c.eval_from (y0, d, nullptr, zs).first;
    std::array<double, STATES> yy = y0;
    for (size_t i = 0; i < STATES; ++i)
        yy[i] = y0[i] + h * f_old[i];
    std::optional<std::tuple<std::array<double, STATES>, double, std::array<double, STATES>>> done;
    size_t iterations = potato ? 2 : 20;
    for (size_t it = 0; it < iterations; ++it)
    {
        iters += 1;
        auto [fy, out] = c.eval_sparse (yy, d, JacobianOut { &jac, &gout }, zs, false);
        std::array<double, STATES> r {};
        for (size_t i = 0; i < STATES; ++i)
            r[i] = yy[i] - y0[i] - 0.5 * h * (f_old[i] + fy[i]);
        std::array<double, STATES> dy = recip ? solve_loop<true> (jac, 0.5 * h, r) : solve_loop<false> (jac, 0.5 * h, r);
        double big = 0.0;
        for (size_t i = 0; i < STATES; ++i)
        {
            yy[i] -= dy[i];
            big = std::fmax (big, std::fabs (dy[i]));
        }
        if (big < tol || (potato && it + 1 == iterations))
        {
            done = std::tuple { fy, out, dy };
            break;
        }
    }
    if (! done.has_value() && ! potato)
        ca72::unconverged::note (ca72::unconverged::Solver::FilterLoop);
    y = yy;
    std::array<double, STATES> f_new;
    double out;
    if (done.has_value())
    {
        auto& [fy, o0, dy] = *done;
        std::array<double, STATES> f = fy;
        for (size_t i = 0; i < STATES; ++i)
            for (size_t kk : JAC_ROWS[i])
                f[i] -= jac[i][kk] * dy[kk];
        double o = o0;
        for (size_t i = W; i < STATES; ++i)
            o -= gout[i] * dy[i];
        f_new = f;
        out = o;
    }
    else
    {
        std::tie (f_new, out) = c.eval_from (yy, d, nullptr, zs);
    }
    f_prev = f_new;
    return out;
}

std::pair<double, double> table_hermite (std::span<const double> t, double min, double step, double x)
{
    size_t n = t.size();
    double u = (x - min) / step;
    if (u <= 0.0)
    {
        double s = (t[1] - t[0]) / step;
        return { t[0] + s * (x - min), s };
    }
    if (u >= (double) (n - 1))
    {
        double s = (t[n - 1] - t[n - 2]) / step;
        return { t[n - 1] + s * (x - min - step * (double) (n - 1)), s };
    }
    // Rust's `as usize` saturates (NaN and negatives to 0); a plain C++ cast would be undefined.
    size_t i = std::min (u >= 0.0 ? (size_t) std::floor (u) : (size_t) 0, n - 2);
    double f = u - (double) i;
    auto m = [&] (size_t k)
    {
        double a = k == 0 ? t[0] : t[k - 1];
        double b = k + 1 >= n ? t[n - 1] : t[k + 1];
        double span = (k == 0 || k + 1 >= n) ? 1.0 : 2.0;
        return (b - a) / span;
    };
    double p0 = t[i], p1 = t[i + 1], m0 = m (i), m1 = m (i + 1);
    double f2 = f * f, f3 = f * f * f;
    double v = (2.0 * f3 - 3.0 * f2 + 1.0) * p0
               + (f3 - 2.0 * f2 + f) * m0
               + (-2.0 * f3 + 3.0 * f2) * p1
               + (f3 - f2) * m1;
    double d = (6.0 * f2 - 6.0 * f) * p0
               + (3.0 * f2 - 4.0 * f + 1.0) * m0
               + (-6.0 * f2 + 6.0 * f) * p1
               + (3.0 * f2 - 2.0 * f) * m1;
    return { v, d / step };
}
} // namespace ca72::vcf
