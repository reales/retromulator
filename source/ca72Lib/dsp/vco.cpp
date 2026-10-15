#include "vco.h"

#include "shared.h"
#include "ulp.h"

#include <bit>

namespace ca72::vco
{
using ca72::expo::ConverterTable;
using ca72::expo::ExpoCircuit;
using ca72::resample::BlepLine;
using ca72::resample::Decimator;

CoreParams CoreParams::default_()
{
    return CoreParams {
        0.01e-6,
        1.594822e-11,
        1.246916e-11,
        -3.970812,
        0.012771,
        2.824424e-07,
        1.203511e-06,
        { 4.4359, 5.18819, 5.94048, 6.69278, 7.44507, 8.19736, 8.94966, 9.70195 },
        { 8.581449e-06, 6.858305e-06, 5.389057e-06, 4.194429e-06, 3.246414e-06, 2.513420e-06, 1.954638e-06, 1.532251e-06 },
    };
}

double CoreParams::delay_at (double slope) const
{
    double x = ca72::ulp::log (ca72::fmax (slope, 1e-9));
    const auto& g = delay_ln_slope;
    if (x <= g[0])
        return delay[0];
    for (size_t i = 1; i < g.size(); ++i)
    {
        if (x <= g[i])
        {
            double a = (x - g[i - 1]) / (g[i] - g[i - 1]);
            return delay[i - 1] + (delay[i] - delay[i - 1]) * a;
        }
    }
    return delay[g.size() - 1];
}

ShaperParams ShaperParams::default_()
{
    return ShaperParams {
        2.0346,
        3.9098,
        4.7 / 9.0,
        -0.0371,
        0.3738,
        -0.9983,
        0.0,
        -4.2954,
        2.74e-6,
        3.75e-6,
        4.94e-3,
        -3.8e-7,
        -4.8e-7,
    };
}

double triangle (double buf)
{
    const auto& t = ca72::tables::TRI;
    double x = (buf - ca72::tables::TRI_MIN) / ca72::tables::TRI_STEP;
    double fl = std::floor (x);
    double hi = (double) ((ptrdiff_t) t.size() - 3);
    ptrdiff_t ii = std::isnan (fl) ? 1 : (ptrdiff_t) ca72::clamp (fl, 1.0, hi);
    size_t i = (size_t) ii;
    double f = ca72::clamp (x - (double) i, 0.0, 1.0);
    double p0 = t[i - 1], p1 = t[i], p2 = t[i + 1], p3 = t[i + 2];
    return p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
}

void Vco::set_quality (int points, bool corners)
{
    blep = points;
    blamp = corners;
    fresh_bleps();
}

void Vco::fresh_bleps()
{
    saw = BlepLine::with_points (blep);
    tri = BlepLine::with_points (blep);
    rect = BlepLine::with_points (blep);
}

Vco Vco::new_ (double sample_rate, size_t oversample)
{
    size_t os;
    if (oversample <= 1)
        os = 1;
    else if (oversample <= 3)
        os = 2;
    else if (oversample <= 7)
        os = 4;
    else if (oversample <= 15)
        os = 8;
    else
        os = 16;
    return Vco {
        .expo = ExpoCircuit::default_(),
        .core = CoreParams::default_(),
        .shaper = ShaperParams::default_(),
        .celsius = 25.0,
        .oversample = os,
        .dt = 1.0 / (sample_rate * (double) os),
        .other = os,
        .dec_other = { Decimator::new_ (os), Decimator::new_ (os), Decimator::new_ (os) },
        .table = std::nullopt,
        .table_on = false,
        .v = CoreParams::default_().v_top0,
        .hold_left = 0.0,
        .reset_in = std::nullopt,
        .rise_in = std::nullopt,
        .fall_in = std::nullopt,
        .rect_is_low = false,
        .guess = std::nullopt,
        .saw = BlepLine::default_(),
        .tri = BlepLine::default_(),
        .rect = BlepLine::default_(),
        .dec = { Decimator::new_ (os), Decimator::new_ (os), Decimator::new_ (os) },
        .ticks = 0,
        .last_reset_ = 0.0,
        .resets = 0,
        .sub = 0,
        .solved = { std::nullopt, std::nullopt },
    };
}

void Vco::prepare (size_t oversample_)
{
    dec_other = { Decimator::new_ (oversample_), Decimator::new_ (oversample_), Decimator::new_ (oversample_) };
    other = oversample_;
    static ca72::shared::Shared<std::pair<ExpoCircuit, uint64_t>, ConverterTable> TABLES;
    ExpoCircuit e = expo;
    double c = celsius;
    table = TABLES.get (std::pair<ExpoCircuit, uint64_t> { e, std::bit_cast<uint64_t> (c) },
                        [&] { return ConverterTable::new_ (e, c); });
}

void Vco::set_oversample (size_t oversample_)
{
    if (oversample_ == oversample || oversample_ != other)
        return;
    double rate_dt = dt * (double) oversample;
    std::swap (dec, dec_other);
    other = oversample;
    oversample = oversample_;
    dt = rate_dt / (double) oversample_;
    for (auto& d : dec)
        d.reset();
    fresh_bleps();
    sub = 0;
}

std::pair<double, uint64_t> Vco::last_reset() const
{
    return { last_reset_, resets };
}

double Vco::latency() const
{
    return dec[0].delay() + 1.0 / (double) oversample;
}

void Vco::start_at (double at)
{
    reset();
    double f = ca72::clamp (at, 0.0, 0.98);
    v = core.v_top0 + f * (core.v_threshold - core.v_top0);
}

void Vco::reset()
{
    v = core.v_top0;
    hold_left = 0.0;
    reset_in = std::nullopt;
    rise_in = std::nullopt;
    fall_in = std::nullopt;
    rect_is_low = false;
    guess = std::nullopt;
    fresh_bleps();
    for (auto& d : dec)
        d.reset();
    ticks = 0;
    last_reset_ = 0.0;
    resets = 0;
}

double Vco::ramp() const
{
    return v;
}

VcoOut Vco::tick (double i_in, double width)
{
    uint64_t key_i_in = std::bit_cast<uint64_t> (i_in);
    std::optional<uint64_t> key_guess = guess ? std::optional<uint64_t> (std::bit_cast<uint64_t> (*guess)) : std::nullopt;
    uint64_t key_celsius = std::bit_cast<uint64_t> (celsius);

    std::optional<std::pair<double, double>> found;
    if (table_on && table.has_value())
        found = (*table)->lookup (expo, celsius, expo.drive_u (i_in, celsius));
    if (! found)
    {
        for (const auto& s : solved)
        {
            if (s && s->i_in == key_i_in && s->guess == key_guess && s->celsius == key_celsius && s->expo == expo)
            {
                found = std::pair<double, double> { s->top, s->bottom };
                break;
            }
        }
    }
    double top, bottom;
    if (found)
    {
        top = found->first;
        bottom = found->second;
    }
    else
    {
        auto st_top = expo.solve_current (i_in, 0.0, celsius, guess);
        auto st_bottom = expo.solve_current (i_in, -4.0, celsius, st_top.ic_exp);
        std::optional<Solved> prev = solved[0];
        solved = { Solved { key_i_in, key_guess, key_celsius, expo, st_top.ic_exp, st_bottom.ic_exp }, prev };
        top = st_top.ic_exp;
        bottom = st_bottom.ic_exp;
    }
    if (! (std::isfinite (top) && std::isfinite (bottom) && top <= TIMING_MAX))
    {
        top = TIMING_MAX;
        bottom = TIMING_MAX * EARLY_AT_MAX;
    }
    guess = top;
    double b = (top - bottom) / 4.0;
    return tick_current (top, b, width);
}

double Vco::timing_current() const
{
    return guess.value_or (0.0);
}

VcoOut Vco::tick_current (double i0, double b, double width)
{
    double a = i0 - core.i_leak;
    double c = core.c1 + core.c_par;
    ShaperParams sh = shaper;
    double fall_buf = sh.rect_fall0 + sh.rect_per_width * width;
    double rise_buf = sh.rect_rise0 + sh.rect_per_width * width;
    double v_fall = (fall_buf - sh.buf_offset) / sh.buf_gain - sh.rect_fall_overdrive;
    VcoOut out = VcoOut::default_();
    for (size_t k = 0; k < oversample; ++k)
    {
        sub = k;
        step (a, b, c, v_fall, rise_buf);
        double buf = sh.buf_gain * v + sh.buf_offset;
        double level = rect_is_low ? sh.rect_low : sh.rect_high;
        double s = saw.push (sh.saw_ratio * buf);
        double t = tri.push (triangle (buf));
        double r = rect.push (level);
        if (auto o = dec[0].push (s))
            out.saw = *o;
        if (auto o = dec[1].push (t))
            out.tri = *o;
        if (auto o = dec[2].push (r))
            out.rect = *o;
    }
    ticks += 1;
    return out;
}

void Vco::step (double a, double b, double c, double v_fall, double rise_buf)
{
    double dt_ = dt;
    double t = 0.0;
    double big_a = a / b;
    auto advance = [&] (double v0, double h) { return -big_a + (v0 + big_a) * ca72::ulp::exp (-b * h / c); };
    auto cross = [&] (double v0, double v1) { return (c / b) * ca72::ulp::log ((v0 + big_a) / (v1 + big_a)); };
    double th = core.v_threshold;
    while (true)
    {
        double left = dt_ - t;
        bool holding = hold_left > 0.0;
        double v_end = holding ? v : advance (v, left);
        double next = left;
        uint8_t which = 0;
        auto take = [&] (double at, uint8_t w) {
            if (at < next)
            {
                next = at;
                which = w;
            }
        };
        if (holding)
            take (hold_left, 1);
        if (reset_in)
            take (*reset_in, 2);
        if (rise_in)
            take (*rise_in, 3);
        if (fall_in)
            take (*fall_in, 4);
        if (! holding && ! reset_in && v > th && v_end <= th)
            take (ca72::clamp (cross (v, th), 0.0, left), 5);
        if (! holding && ! rect_is_low && ! fall_in && v > v_fall && v_end <= v_fall)
            take (ca72::clamp (cross (v, v_fall), 0.0, left), 6);
        if (! holding)
            v = advance (v, next);
        hold_left = ca72::fmax (hold_left - next, 0.0);
        if (reset_in)
            *reset_in -= next;
        if (rise_in)
            *rise_in -= next;
        if (fall_in)
            *fall_in -= next;
        t += next;
        double frac = ca72::clamp (t / dt_, 1e-9, 1.0);
        ShaperParams sh = shaper;
        switch (which)
        {
            case 2:
                reset_in = std::nullopt;
                do_reset (a, b, c, frac, rise_buf);
                break;
            case 3:
                rise_in = std::nullopt;
                if (rect_is_low)
                {
                    rect_is_low = false;
                    rect.step (sh.rect_high - sh.rect_low, frac);
                }
                break;
            case 4:
                fall_in = std::nullopt;
                if (! rect_is_low)
                {
                    rect_is_low = true;
                    rect.step (sh.rect_low - sh.rect_high, frac);
                }
                break;
            case 5:
            {
                v = th;
                double slope = (a + b * th) / c;
                reset_in = core.delay_at (slope);
                break;
            }
            case 6:
                v = v_fall;
                fall_in = sh.rect_fall_delay;
                break;
            default:
                break;
        }
        if (which == 0)
            break;
    }
}

void Vco::do_reset (double a, double b, double c, double frac, double rise_buf)
{
    ShaperParams sh = shaper;
    double slope = (a + b * v) / c;
    double v_top = core.v_top0 - core.top_per_slope * slope;
    double buf0 = sh.buf_gain * v + sh.buf_offset;
    double buf1 = sh.buf_gain * v_top + sh.buf_offset;
    saw.step (sh.saw_ratio * (buf1 - buf0), frac);
    saw.impulse (sh.saw_ratio * sh.buf_lag / dt, frac);
    tri.step (triangle (buf1) - triangle (buf0), frac);
    tri.impulse (sh.tri_glitch / dt, frac);
    if (blamp)
    {
        // dv/dt = -(a + b v) / c; the merged corner sits mid-hold.
        auto dtri = [] (double x) { return (triangle (x + 1e-4) - triangle (x - 1e-4)) / 2e-4; };
        const double dbuf0 = -sh.buf_gain * (a + b * v) / c * dt;
        const double dbuf1 = -sh.buf_gain * (a + b * v_top) / c * dt;
        const double at = frac + 0.5 * core.t_hold / dt;
        tri.ramp (dtri (buf1) * dbuf1 - dtri (buf0) * dbuf0, at);
        saw.ramp (sh.saw_ratio * (dbuf1 - dbuf0), at);
    }
    if (rect_is_low && buf1 >= rise_buf)
        rise_in = sh.rect_rise_delay;
    fall_in = std::nullopt;
    v = v_top;
    hold_left = core.t_hold;
    last_reset_ = (double) ticks + ((double) sub + frac - 1.0) / (double) oversample;
    resets += 1;
}
} // namespace ca72::vco
