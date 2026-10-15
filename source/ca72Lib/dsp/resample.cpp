#include "resample.h"
#include "ulp.h"

namespace ca72::resample
{
static double bessel_i0 (double x)
{
    double sum = 1.0;
    double term = 1.0;
    const double y = x * x / 4.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= y / ((double) k * (double) k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

std::vector<double> halfband_taps (size_t len, double beta)
{
    constexpr double PI = 3.14159265358979323846;
    const size_t n = len | 1;
    const double m = (double) (n / 2);
    const double i0b = bessel_i0 (beta);
    std::vector<double> taps;
    taps.reserve (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double k = (double) i - m;
        const double sinc = k == 0.0 ? 0.5 : ca72::ulp::sin (PI * k / 2.0) / (PI * k);
        const double r = k / m;
        taps.push_back (sinc * bessel_i0 (beta * std::sqrt (std::fmax (1.0 - r * r, 0.0))) / i0b);
    }
    return taps;
}

Halfband Halfband::new_ (size_t len, double beta)
{
    Halfband h;
    h.taps = halfband_taps (len, beta);
    const size_t n = h.taps.size();
    h.line.assign (2 * n, 0.0);
    h.pos = 0;
    h.phase = false;
    h.sparse_ = false;
    return h;
}

Halfband Halfband::sparse (size_t len, double beta)
{
    Halfband h = Halfband::new_ (len, beta);
    const size_t m = h.taps.size() / 2;
    for (size_t i = 0; i < h.taps.size(); ++i)
        if (i != m && ((ptrdiff_t) i - (ptrdiff_t) m) % 2 == 0)
            h.taps[i] = 0.0;
    h.sparse_ = true;
    return h;
}

double Halfband::sum (std::span<const double> w) const
{
    if (sparse_)
    {
        const size_t m = taps.size() / 2;
        double acc = 0.0;
        for (size_t i = 1 - m % 2; i < taps.size(); i += 2)
            acc += taps[i] * w[i];
        return acc + taps[m] * w[m];
    }
    double acc = 0.0;
    const size_t len = std::min (taps.size(), w.size());
    for (size_t i = 0; i < len; ++i)
        acc += taps[i] * w[i];
    return acc;
}

std::optional<double> Halfband::push (double x)
{
    const size_t n = taps.size();
    line[pos] = x;
    line[pos + n] = x;
    pos = pos == 0 ? n - 1 : pos - 1;
    phase = ! phase;
    if (! phase)
        return std::nullopt;
    return sum (std::span<const double> (line.data() + pos + 1, n));
}

double Halfband::filter (double x)
{
    const size_t n = taps.size();
    line[pos] = x;
    line[pos + n] = x;
    const double acc = sum (std::span<const double> (line.data() + pos, n));
    pos = pos == 0 ? n - 1 : pos - 1;
    return acc;
}

double Halfband::filter_stuffed_zero()
{
    const size_t n = taps.size();
    line[pos] = 0.0;
    line[pos + n] = 0.0;
    const size_t m = n / 2;
    const double acc = m % 2 == 1 ? taps[m] * line[pos + m]
                                  : sum (std::span<const double> (line.data() + pos, n));
    pos = pos == 0 ? n - 1 : pos - 1;
    return acc;
}

void Halfband::reset()
{
    std::fill (line.begin(), line.end(), 0.0);
    pos = 0;
    phase = false;
}

Decimator Decimator::new_ (size_t factor) { return with (factor, &Halfband::new_); }

Decimator Decimator::fir (size_t factor)
{
    const bool was = iir_resamplers;
    iir_resamplers = false;
    Decimator d = with (factor, &Halfband::new_);
    iir_resamplers = was;
    return d;
}

Decimator Decimator::fir_sparse (size_t factor)
{
    const bool was = iir_resamplers;
    iir_resamplers = false;
    Decimator d = with (factor, &Halfband::sparse);
    iir_resamplers = was;
    return d;
}

Decimator Decimator::sparse (size_t factor) { return with (factor, &Halfband::sparse); }

namespace
{
constexpr double NARROW[8] { 0.02611300971756304, 0.099935239175956064, 0.20979108281105327, 0.34132447068733562,
                             0.48216249324592159, 0.62497421045409629, 0.76868996039282311, 0.91882587144863237 };
constexpr double WIDE[4] { 0.042454709865267871, 0.17073985049750015, 0.39331989319033134, 0.74571358872024118 };
// Group delays at DC, samples at the stage's high rate (hiir's designer).
constexpr double NARROW_DELAY = 4.1650, WIDE_DELAY = 2.7080;
} // namespace

IirDown IirDown::make (bool narrow)
{
    IirDown s;
    s.narrow = narrow;
    s.n8.set_coefs (NARROW);
    s.n4.set_coefs (WIDE);
    s.reset();
    return s;
}

double IirDown::delay() const { return narrow ? NARROW_DELAY : WIDE_DELAY; }

void IirDown::reset()
{
    n8.clear_buffers();
    n4.clear_buffers();
    pending = 0.0;
    have = false;
}

IirUp IirUp::make (bool narrow)
{
    IirUp s;
    s.narrow = narrow;
    s.n8.set_coefs (NARROW);
    s.n4.set_coefs (WIDE);
    s.reset();
    return s;
}

double IirUp::delay() const { return narrow ? NARROW_DELAY : WIDE_DELAY; }

void IirUp::reset()
{
    n8.clear_buffers();
    n4.clear_buffers();
}

Decimator Decimator::with (size_t factor, Halfband (*make) (size_t, double))
{
    const uint32_t stages = stages_len (factor);
    Decimator d;
    if (iir_resamplers && make != nullptr)
    {
        for (uint32_t i = 0; i < stages; ++i)
            d.iir.push_back (IirDown::make (i + 1 == stages));
        return d;
    }
    d.stages.reserve (stages);
    for (uint32_t i = 0; i < stages; ++i)
        d.stages.push_back (i + 1 == stages ? make (79, 10.06) : make (23, 10.06));
    return d;
}

std::optional<double> Decimator::push (double x)
{
    if (! iir.empty())
    {
        double v = x;
        for (auto& s : iir)
        {
            const auto r = s.push (v);
            if (! r.has_value())
                return std::nullopt;
            v = *r;
        }
        return v;
    }
    double v = x;
    for (auto& s : stages)
    {
        const auto r = s.push (v);
        if (! r.has_value())
            return std::nullopt;
        v = *r;
    }
    return v;
}

double Decimator::delay() const
{
    if (! iir.empty())
    {
        const size_t n = iir.size();
        double acc = 0.0;
        for (size_t k = 0; k < n; ++k)
            acc += iir[k].delay() / (double) (1u << (n - k));
        return acc;
    }
    const size_t n = stages.size();
    double acc = -0.0;
    for (size_t k = 0; k < n; ++k)
        acc += (double) stages[k].delay() / (double) (1u << (n - k));
    return acc;
}

void Decimator::reset()
{
    for (auto& s : iir)
        s.reset();
    for (auto& s : stages)
        s.reset();
}

Interpolator Interpolator::new_ (size_t factor) { return with (factor, &Halfband::new_); }

Interpolator Interpolator::fir (size_t factor)
{
    const bool was = iir_resamplers;
    iir_resamplers = false;
    Interpolator it = with (factor, &Halfband::new_);
    iir_resamplers = was;
    return it;
}

Interpolator Interpolator::fir_sparse (size_t factor)
{
    const bool was = iir_resamplers;
    iir_resamplers = false;
    Interpolator it = with (factor, &Halfband::sparse);
    iir_resamplers = was;
    return it;
}

Interpolator Interpolator::sparse (size_t factor) { return with (factor, &Halfband::sparse); }

Interpolator Interpolator::with (size_t factor, Halfband (*make) (size_t, double))
{
    const uint32_t stages = stages_len (factor);
    Interpolator it;
    if (iir_resamplers && make != nullptr)
    {
        for (uint32_t i = 0; i < stages; ++i)
            it.iir.push_back (IirUp::make (i == 0));
        it.factor_ = (size_t) 1 << stages;
        return it;
    }
    it.stages.reserve (stages);
    for (uint32_t i = 0; i < stages; ++i)
        it.stages.push_back (i == 0 ? make (79, 10.06) : make (23, 10.06));
    it.factor_ = (size_t) 1 << stages_len (factor);
    return it;
}

void Interpolator::push (double x, std::span<double> out)
{
    if (! iir.empty())
    {
        std::array<double, 16> buf {};
        size_t n = 1;
        buf[0] = x;
        for (auto& s : iir)
        {
            std::array<double, 16> next {};
            for (size_t i = 0; i < n; ++i)
                s.push (buf[i], next[2 * i], next[2 * i + 1]);
            n *= 2;
            buf = next;
        }
        assert (out.size() >= n);
        std::copy (buf.begin(), buf.begin() + (ptrdiff_t) n, out.begin());
        return;
    }
    std::array<double, 16> buf {};
    size_t n = 1;
    buf[0] = x;
    for (auto& s : stages)
    {
        std::array<double, 16> next {};
        for (size_t i = 0; i < n; ++i)
        {
            next[2 * i] = s.filter (2.0 * buf[i]);
            next[2 * i + 1] = s.sparse_ ? s.filter_stuffed_zero() : s.filter (0.0);
        }
        n *= 2;
        buf = next;
    }
    assert (out.size() >= n);
    std::copy (buf.begin(), buf.begin() + (ptrdiff_t) n, out.begin());
}

double Interpolator::delay() const
{
    if (! iir.empty())
    {
        double acc = 0.0;
        for (size_t k = 0; k < iir.size(); ++k)
            acc += iir[k].delay() / (double) (1u << (k + 1));
        return acc;
    }
    double acc = -0.0;
    for (size_t k = 0; k < stages.size(); ++k)
        acc += (double) stages[k].delay() / (double) (1u << (k + 1));
    return acc;
}

void Interpolator::reset()
{
    for (auto& s : iir)
        s.reset();
    for (auto& s : stages)
        s.reset();
}

uint32_t stages_len (size_t factor)
{
    switch (factor)
    {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        default: assert (false); return 0;
    }
}
} // namespace ca72::resample
