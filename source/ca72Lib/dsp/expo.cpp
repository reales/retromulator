#include "expo.h"

#include "fast.h"
#include "ulp.h"
#include "unconverged.h"

namespace ca72::expo
{
ExpoCircuit ExpoCircuit::default_()
{
    return ExpoCircuit {
        1000.0,
        3.5e-3,
        500.0,
        11.8e3,
        160e3,
        33.0,
        100.0,
        0.5,
        1000.0,
        100.0,
        39e3,
        1000.0,
        M5_LINE,
        M4_LINE,
        -10.0,
        1000.0,
        0.0,
        0.0,
        true,
        SUMMER_OFFSET,
        15.16e-6 / 2.0 / 93.75,
        ca72::devices::CA3046,
    };
}

bool bjt_eq (const ca72::devices::Bjt& a, const ca72::devices::Bjt& b)
{
    return a.is == b.is && a.bf == b.bf && a.ise == b.ise && a.ne == b.ne && a.vaf == b.vaf
           && a.ikf == b.ikf && a.br == b.br && a.rb == b.rb && a.re == b.re && a.rc == b.rc
           && a.cje == b.cje && a.vje == b.vje && a.mje == b.mje && a.cjc == b.cjc
           && a.vjc == b.vjc && a.mjc == b.mjc && a.tf == b.tf && a.xti == b.xti
           && a.xtb == b.xtb && a.eg == b.eg && a.tnom == b.tnom;
}

bool ExpoCircuit::operator== (const ExpoCircuit& o) const
{
    return r20 == o.r20 && tc20 == o.tc20 && r11 == o.r11 && r37 == o.r37 && r42 == o.r42
           && r9 == o.r9 && r8 == o.r8 && a8 == o.a8 && r19 == o.r19 && r7 == o.r7
           && r50 == o.r50 && r79 == o.r79 && m5 == o.m5 && m4 == o.m4 && vn == o.vn
           && r10 == o.r10 && pos_v == o.pos_v && pos_g == o.pos_g && trim_on == o.trim_on
           && vos == o.vos && bias == o.bias && bjt_eq (bjt, o.bjt);
}

double ExpoCircuit::v_sum() const
{
    double g10 = 1.0 / r10;
    return (m5 * g10 + pos_v * pos_g - bias) / (g10 + pos_g) + vos;
}

double ExpoCircuit::drive_u (double i_in, double celsius) const
{
    double r20_ = r20 * (1.0 + tc20 * (celsius - 25.0));
    double v_sum_ = v_sum();
    double trim = trim_on ? (vn - v_sum_) / (r11 + r37) : 0.0;
    double fixed = i_in + trim - bias;
    double a = r79 / r42;
    return v_sum_ - r20_ * fixed - (r20_ / r42) * ((vn + a * v_sum_) / (1.0 + a) - v_sum_);
}

double ExpoCircuit::input_current (std::span<const Input> inputs) const
{
    double v_sum_ = v_sum();
    double s = 0.0;
    for (const auto& i : inputs)
        s += (i.v - v_sum_) / i.r;
    return s;
}

double ExpoCircuit::collector_current (std::span<const Input> inputs, double v_ramp, double celsius) const
{
    return solve (inputs, v_ramp, celsius).ic_exp;
}

ExpoState ExpoCircuit::solve (std::span<const Input> inputs, double v_ramp, double celsius) const
{
    return solve_current (input_current (inputs), v_ramp, celsius, std::nullopt);
}

ExpoState ExpoCircuit::solve_current (double i_in, double v_ramp, double celsius, std::optional<double> guess) const
{
    const auto& b = bjt;
    auto bt = b.at (celsius);
    double vt = bt.vt;
    double r20_ = r20 * (1.0 + tc20 * (celsius - 25.0));
    double v_sum_ = v_sum();
    double ic_ref = -m4 / r50 - bias;
    double r_top = r9 + r8 * (1.0 - a8);
    double r_bot = r8 * a8 + r19;
    double k = r_bot / (r_top + r_bot);
    double r_th = r_top * r_bot / (r_top + r_bot);
    double trim = trim_on ? (vn - v_sum_) / (r11 + r37) : 0.0;
    double fixed = i_in + trim - bias;
    ExpoState st = ExpoState::default_();
    st.ic_exp = guess.value_or (ic_ref);
    st.ic_ref = ic_ref;
    st.v_sum = v_sum_;
    st.vb_ref = m5;
    st.vb_exp = m5;
    bool converged = false;
    for (int it = 0; it < 16; ++it)
    {
        double x = st.ic_exp;
        double ib_ref = bt.base_current (ic_ref, st.vb_ref - m4, b.vaf);
        double ib_exp = bt.base_current (x, st.vb_exp - v_ramp, b.vaf);
        double ie_ref = ic_ref + ib_ref;
        double ie_exp = x + ib_exp;
        double ic_tail = ie_exp + ie_ref;
        double ie_tail = ic_tail + bt.base_current (ic_tail, -9.1 + 5.7, b.vaf);
        double a = r79 / r42;
        double v_sub = (vn + r79 * ie_tail + a * v_sum_) / (1.0 + a);
        double sum = fixed + (v_sub - v_sum_) / r42;
        double v_ic1 = v_sum_ - r20_ * sum;
        double vb_ref = m5 + (v_ic1 - m5) * k - r_th * ib_ref;
        double vb_exp = m5 - r7 * ib_exp;
        double dvbe = (vb_exp - b.rb * ib_exp - b.re * ie_exp) - (vb_ref - b.rb * ib_ref - b.re * ie_ref);
        double vbc_exp = vb_exp - b.rb * ib_exp - v_ramp;
        double vbc_ref = vb_ref - b.rb * ib_ref - m4;
        double next = ic_ref * ca72::ulp::exp (dvbe / vt) * (1.0 - vbc_exp / b.vaf) / (1.0 - vbc_ref / b.vaf);
        bool done = std::fabs (next / x - 1.0) < 1e-11;
        st = ExpoState { next, ib_exp, ic_ref, ib_ref, v_sum_, v_ic1, vb_ref, vb_exp, v_sub };
        if (done)
        {
            converged = true;
            break;
        }
    }
    if (! converged)
        ca72::unconverged::note (ca72::unconverged::Solver::Converter);
    return st;
}

ConverterTable ConverterTable::new_ (const ExpoCircuit& expo, double celsius)
{
    double r20 = expo.r20 * (1.0 + expo.tc20 * (celsius - 25.0));
    double u_at_zero = expo.drive_u (0.0, celsius);
    auto i_for = [&] (double u) { return (u_at_zero - u) / r20; };
    auto ln_ic = [&] (double u, double v_ramp) {
        return std::log (expo.solve_current (i_for (u), v_ramp, celsius, std::nullopt).ic_exp);
    };
    double ua = u_at_zero;
    double ub = expo.drive_u (1e-5, celsius);
    double slope = (ln_ic (ub, 0.0) - ln_ic (ua, 0.0)) / (ub - ua);
    auto u_for = [&] (double ic) {
        double target = std::log (ic);
        double guess = ua + (target - ln_ic (ua, 0.0)) / slope;
        double span = std::fabs (2.0 / slope);
        double lo = guess - span;
        double hi = guess + span;
        bool rising = slope > 0.0;
        for (int it = 0; it < 100; ++it)
        {
            double mid = 0.5 * (lo + hi);
            double y = ln_ic (mid, 0.0);
            if ((y < target) == rising)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5 * (lo + hi);
    };
    double a = u_for (1e-11);
    double b = u_for (2e-3);
    double u_lo = ca72::fmin (a, b);
    double u_hi = ca72::fmax (a, b);
    double du = (u_hi - u_lo) / (double) (N - 1);
    auto at = [&] (size_t k) { return u_lo + du * (double) k; };
    ConverterTable t;
    t.u0 = u_lo;
    t.du = du;
    t.top.reserve (N);
    for (size_t k = 0; k < N; ++k)
        t.top.push_back (ln_ic (at (k), 0.0));
    t.bottom.reserve (N);
    for (size_t k = 0; k < N; ++k)
        t.bottom.push_back (ln_ic (at (k), -4.0));
    t.key = { std::bit_cast<uint64_t> (expo.r11), std::bit_cast<uint64_t> (expo.a8), std::bit_cast<uint64_t> (celsius) };
    return t;
}

std::optional<std::pair<double, double>> ConverterTable::lookup (const ExpoCircuit& expo, double celsius, double u) const
{
    std::array<uint64_t, 3> k { std::bit_cast<uint64_t> (expo.r11), std::bit_cast<uint64_t> (expo.a8), std::bit_cast<uint64_t> (celsius) };
    if (key != k)
        return std::nullopt;
    double f = (u - u0) / du;
    if (! (f >= 1.0 && f < (double) (N - 3)))
        return std::nullopt;
    size_t i = (size_t) f;
    double t = f - (double) i;
    auto cubic = [&] (const std::vector<double>& y) {
        double p0 = y[i - 1], p1 = y[i], p2 = y[i + 1], p3 = y[i + 2];
        return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
    };
    return std::pair<double, double> { ca72::fast::exp (cubic (top)), ca72::fast::exp (cubic (bottom)) };
}
} // namespace ca72::expo
