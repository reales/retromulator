#include "linear.h"

#include <bit>

namespace ca72::linear
{
namespace
{
    using C = std::pair<double, double>;

    int64_t total_key (double x)
    {
        int64_t b = std::bit_cast<int64_t> (x);
        b ^= (int64_t) ((uint64_t) (b >> 63) >> 1);
        return b;
    }

    C cmul (C a, C b) { return { a.first * b.first - a.second * b.second, a.first * b.second + a.second * b.first }; }

    C cdiv (C a, C b)
    {
        const double d = b.first * b.first + b.second * b.second;
        return { (a.first * b.first + a.second * b.second) / d, (a.second * b.first - a.first * b.second) / d };
    }

    double cabs (C a) { return std::hypot (a.first, a.second); }
} // namespace

void solve_complex (size_t n, std::span<C> m, std::span<C> rhs)
{
    for (size_t col = 0; col < n; ++col)
    {
        size_t piv = col;
        for (size_t i = col + 1; i < n; ++i)
            if (total_key (cabs (m[i * n + col])) >= total_key (cabs (m[piv * n + col])))
                piv = i;
        if (piv != col)
        {
            for (size_t c = 0; c < n; ++c)
                std::swap (m[col * n + c], m[piv * n + c]);
            std::swap (rhs[col], rhs[piv]);
        }
        const C d = m[col * n + col];
        for (size_t r = col + 1; r < n; ++r)
        {
            const C f = cdiv (m[r * n + col], d);
            if (! (f.first == 0.0 && f.second == 0.0))
            {
                for (size_t c = col; c < n; ++c)
                {
                    const C t = cmul (f, m[col * n + c]);
                    m[r * n + c] = { m[r * n + c].first - t.first, m[r * n + c].second - t.second };
                }
                const C t = cmul (f, rhs[col]);
                rhs[r] = { rhs[r].first - t.first, rhs[r].second - t.second };
            }
        }
    }
    for (size_t col = n; col-- > 0;)
    {
        C s = rhs[col];
        for (size_t c = col + 1; c < n; ++c)
        {
            const C t = cmul (m[col * n + c], rhs[c]);
            s = { s.first - t.first, s.second - t.second };
        }
        rhs[col] = cdiv (s, m[col * n + col]);
    }
}

void invert_into (size_t n, std::span<double> a, std::span<double> inv)
{
    std::fill (inv.begin(), inv.end(), 0.0);
    for (size_t i = 0; i < n; ++i)
        inv[i * n + i] = 1.0;
    for (size_t col = 0; col < n; ++col)
    {
        size_t piv = col;
        for (size_t i = col + 1; i < n; ++i)
            if (total_key (std::fabs (a[i * n + col])) >= total_key (std::fabs (a[piv * n + col])))
                piv = i;
        if (piv != col)
        {
            for (size_t c = 0; c < n; ++c)
            {
                std::swap (a[col * n + c], a[piv * n + c]);
                std::swap (inv[col * n + c], inv[piv * n + c]);
            }
        }
        const double d = a[col * n + col];
        for (size_t c = 0; c < n; ++c)
        {
            a[col * n + c] /= d;
            inv[col * n + c] /= d;
        }
        for (size_t r = 0; r < n; ++r)
        {
            if (r != col)
            {
                const double f = a[r * n + col];
                if (f != 0.0)
                {
                    for (size_t c = 0; c < n; ++c)
                    {
                        a[r * n + c] -= f * a[col * n + c];
                        inv[r * n + c] -= f * inv[col * n + c];
                    }
                }
            }
        }
    }
}

std::pair<double, double> SmallSignal::response (size_t out, double f) const
{
    constexpr double PI = 3.14159265358979323846;
    const double w = 2.0 * PI * f;
    std::vector<C> m (n * n);
    for (size_t k = 0; k < n * n; ++k)
        m[k] = { g[k], w * c[k] };
    std::vector<C> rhs (g_in.size());
    for (size_t k = 0; k < g_in.size(); ++k)
        rhs[k] = { -g_in[k], 0.0 };
    solve_complex (n, m, rhs);
    return rhs[out];
}

Discrete SmallSignal::discrete (double h) const
{
    Discrete d;
    d.n = n;
    d.a.assign (n * n, 0.0);
    d.b.assign (n, 0.0);
    d.x_.assign (n, 0.0);
    d.next.assign (n, 0.0);
    d.u_prev = 0.0;
    d.h = h;
    d.work.assign (3 * n * n, 0.0);
    d.rebuild (*this, h);
    return d;
}

void Discrete::rebuild (const SmallSignal& model, double h_)
{
    assert (model.n == n);
    std::span<double> all (work);
    std::span<double> lhs = all.subspan (0, n * n);
    std::span<double> li = all.subspan (n * n, n * n);
    std::span<double> rhs = all.subspan (2 * n * n);
    for (size_t k = 0; k < n * n; ++k)
    {
        lhs[k] = model.g[k] / 2.0 + model.c[k] / h_;
        rhs[k] = model.c[k] / h_ - model.g[k] / 2.0;
    }
    invert_into (n, lhs, li);
    std::fill (a.begin(), a.end(), 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        for (size_t k = 0; k < n; ++k)
        {
            const double l = li[i * n + k];
            if (l != 0.0)
                for (size_t j = 0; j < n; ++j)
                    a[i * n + j] += l * rhs[k * n + j];
        }
    }
    for (size_t i = 0; i < n; ++i)
    {
        double s = -0.0;
        for (size_t k = 0; k < n; ++k)
            s += li[i * n + k] * model.g_in[k];
        b[i] = -s / 2.0;
    }
    h = h_;
}

void Discrete::take_state (const Discrete& from)
{
    if (from.n == n)
    {
        std::copy (from.x_.begin(), from.x_.end(), x_.begin());
        u_prev = from.u_prev;
    }
}

void Discrete::carry (const Discrete& from, std::span<const std::pair<size_t, size_t>> map, bool back)
{
    std::fill (x_.begin(), x_.end(), 0.0);
    for (const auto& [a0, b0] : map)
    {
        const size_t ia = back ? b0 : a0;
        const size_t ib = back ? a0 : b0;
        x_[ib] = from.x_[ia];
    }
    u_prev = from.u_prev;
}

void Discrete::step (double u)
{
    const double s = u + u_prev;
    size_t i = 0;
    while (i + 4 <= n)
    {
        const double* r0 = a.data() + i * n;
        const double* r1 = r0 + n;
        const double* r2 = r1 + n;
        const double* r3 = r2 + n;
        double acc0 = b[i] * s;
        double acc1 = b[i + 1] * s;
        double acc2 = b[i + 2] * s;
        double acc3 = b[i + 3] * s;
        for (size_t k = 0; k < n; ++k)
        {
            const double xv = x_[k];
            acc0 += r0[k] * xv;
            acc1 += r1[k] * xv;
            acc2 += r2[k] * xv;
            acc3 += r3[k] * xv;
        }
        next[i] = acc0;
        next[i + 1] = acc1;
        next[i + 2] = acc2;
        next[i + 3] = acc3;
        i += 4;
    }
    for (; i < n; ++i)
    {
        const double* row = a.data() + i * n;
        double acc = b[i] * s;
        for (size_t k = 0; k < n; ++k)
            acc += row[k] * x_[k];
        next[i] = acc;
    }
    std::swap (x_, next);
    u_prev = u;
}

std::pair<double, double> Discrete::response (size_t out, double f) const
{
    constexpr double PI = 3.14159265358979323846;
    const double th = 2.0 * PI * f * h;
    const C z = { std::cos (th), std::sin (th) };
    std::vector<C> m (n * n);
    for (size_t k = 0; k < n * n; ++k)
    {
        const C diag = k / n == k % n ? z : C { 0.0, 0.0 };
        m[k] = { diag.first - a[k], diag.second };
    }
    const C zp1 = { z.first + 1.0, z.second };
    std::vector<C> rhs (b.size());
    for (size_t k = 0; k < b.size(); ++k)
        rhs[k] = { b[k] * zp1.first, b[k] * zp1.second };
    solve_complex (n, m, rhs);
    return rhs[out];
}
} // namespace ca72::linear
