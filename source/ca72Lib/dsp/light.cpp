#include "light.h"

#include "modulation.h"
#include "unconverged.h"

#include <bit>
#include <numbers>

namespace ca72::light
{
namespace
{
    using ca72::voice::Waveform;

    constexpr double TAU = 2.0 * std::numbers::pi;

    constexpr std::array<float, 64> A440_SHAPE {
        -0.014f, 0.045f, 0.106f, 0.170f, 0.238f, 0.304f, 0.362f, 0.415f, 0.459f, 0.491f, 0.504f, 0.496f, 0.480f,
        0.464f, 0.448f, 0.433f, 0.418f, 0.403f, 0.389f, 0.375f, 0.361f, 0.346f, 0.329f, 0.311f, 0.289f, 0.265f,
        0.239f, 0.210f, 0.179f, 0.146f, 0.112f, 0.076f, 0.038f, -0.001f, -0.042f, -0.085f, -0.130f, -0.176f,
        -0.221f, -0.264f, -0.304f, -0.340f, -0.374f, -0.404f, -0.430f, -0.452f, -0.470f, -0.483f, -0.492f, -0.496f,
        -0.495f, -0.490f, -0.479f, -0.464f, -0.443f, -0.418f, -0.388f, -0.354f, -0.316f, -0.273f, -0.227f, -0.178f,
        -0.126f, -0.071f,
    };

    inline float tanh (float x_)
    {
        const float x = ca72::clamp (x_, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float exp2 (float x_)
    {
        constexpr float SHIFT = 12'582'912.0f;
        const float x = ca72::clamp (x_, -60.0f, 60.0f);
        const float i = (x + SHIFT) - SHIFT;
        const float f = x - i;
        const float p = 1.0f
                        + f * (std::numbers::ln2_v<float>
                               + f * (0.240'226'5f
                                      + f * (0.055'504'11f + f * (0.009'618'129f + f * (0.001'333'355f + f * 0.000'154'035f)))));
        const int32_t e = std::bit_cast<int32_t> (p) + ((int32_t) i << 23);
        return std::bit_cast<float> ((uint32_t) e);
    }

    inline float blep (float t, float dt, float inv)
    {
        if (t < dt)
        {
            const float x = t * inv;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            const float x = (t - 1.0f) * inv;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    double interpolate (std::span<const double> xs, std::span<const double> ys, double x)
    {
        if (x <= xs[0])
            return ys[0];
        for (size_t i = 1; i < xs.size(); ++i)
        {
            if (x <= xs[i])
            {
                const double a = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
                return ys[i - 1] + (ys[i] - ys[i - 1]) * a;
            }
        }
        return ys[ys.size() - 1];
    }

    float channel (double volume, double source, double series)
    {
        auto law = [series] (double p_, double source_)
        {
            const double p = ca72::clamp (p_, 0.0, 1.0);
            const double up = source_ + 25e3 * (1.0 - p);
            const double down = std::fmax (25e3 * p, 1e-3);
            return (down / (up + down)) / (up * down / (up + down) + series);
        };
        return (float) (law (volume, source) / law (1.0, laws::SOURCE_OHMS[1]));
    }

    // Rust's saturating `as usize` from a float.
    inline size_t as_usize (float v)
    {
        if (! (v > 0.0f))
            return 0;
        if (v >= 18446744073709551615.0f)
            return std::numeric_limits<size_t>::max();
        return (size_t) v;
    }

    inline uint32_t as_u32 (double v)
    {
        if (! (v > 0.0))
            return 0;
        if (v >= 4294967295.0)
            return std::numeric_limits<uint32_t>::max();
        return (uint32_t) v;
    }
} // namespace

NoiseLaw NoiseLaw::new_ (double rate)
{
    const float r = (float) (48'000.0 / rate);
    auto pole = [r] (float p) { return std::pow (p, r); };
    auto gain = [&pole] (float g, float p) { return g * ((1.0f - pole (p)) / (1.0f - p)); };
    auto one_pole = [rate] (double hz) { return 1.0 - std::exp (-TAU * hz / rate); };
    NoiseLaw n;
    n.scale = (float) std::sqrt (rate / 48'000.0);
    n.poles = { pole (0.99765f), pole (0.963f), pole (0.57f) };
    n.gains = { gain (0.099'046f, 0.99765f), gain (0.296'516'4f, 0.963f), gain (1.052'691'3f, 0.57f) };
    n.red = { pole (0.995f), gain (0.05f, 0.995f) };
    n.band = {
        (float) std::exp (-TAU * laws::WHITE_BAND.first / rate),
        (float) one_pole (std::fmin (interpolate (laws::NOISE_TOP_AT, laws::WHITE_TOP, rate), 0.45 * rate)),
        (float) std::exp (-TAU * laws::PINK_BAND.first / rate),
        (float) one_pole (std::fmin (interpolate (laws::NOISE_TOP_AT, laws::PINK_TOP, rate), 0.45 * rate)),
    };
    return n;
}

void Held::set (int32_t key, bool on)
{
    if (key >= 0 && key < 128)
    {
        const uint64_t b = (uint64_t) 1 << ((size_t) key & 63);
        auto& w = bits[(size_t) key >> 6];
        if (on)
            w |= b;
        else
            w &= ~b;
    }
}

std::optional<int32_t> Held::lowest() const
{
    if (bits[0] != 0)
        return (int32_t) std::countr_zero (bits[0]);
    if (bits[1] != 0)
        return 64 + (int32_t) std::countr_zero (bits[1]);
    return std::nullopt;
}

std::optional<int32_t> Held::highest() const
{
    if (bits[1] != 0)
        return 127 - (int32_t) std::countl_zero (bits[1]);
    if (bits[0] != 0)
        return 63 - (int32_t) std::countl_zero (bits[0]);
    return std::nullopt;
}

std::optional<int32_t> Held::newest (const std::array<uint32_t, 128>& pressed) const
{
    std::optional<size_t> newest;
    for (size_t word = 0; word < 2; ++word)
    {
        uint64_t b = bits[word];
        while (b != 0)
        {
            const size_t k = word * 64 + (size_t) std::countr_zero (b);
            b &= b - 1;
            if (! newest.has_value() || pressed[k] > pressed[*newest])
                newest = k;
        }
    }
    if (! newest.has_value())
        return std::nullopt;
    return (int32_t) *newest;
}

float Osc::tick (float dt, float inv)
{
    float p = phase + dt;
    if (p >= 1.0f)
        p -= 1.0f;
    phase = p;
    auto fall = [&] { return 1.0f - 2.0f * p + blep (p, dt, inv); };
    auto rect = [&] (float d)
    {
        const float q = p >= d ? p - d : p - d + 1.0f;
        const float naive = p < d ? 1.0f : -1.0f;
        return naive + blep (p, dt, inv) - blep (q, dt, inv) - (2.0f * d - 1.0f);
    };
    switch (wave)
    {
        case Waveform::Triangle:
        {
            constexpr float B = laws::TRIANGLE_BOTTOM;
            const float t = p < B ? 1.0f - p * (2.0f / B) : (p - B) * (2.0f / (1.0f - B)) - 1.0f;
            return laws::TRIANGLE * t;
        }
        case Waveform::SharkTooth:
        {
            float q = p + laws::SHARK_AHEAD;
            q = q >= 1.0f ? q - 1.0f : q;
            return laws::SHARK_TRIANGLE * (4.0f * std::fabs (q - 0.5f) - 1.0f) + laws::SHARK_SAWTOOTH * fall();
        }
        case Waveform::Sawtooth: return laws::SAWTOOTH * fall();
        case Waveform::ReverseSawtooth: return -laws::REVERSE_SAWTOOTH * fall();
        case Waveform::Square: return laws::SQUARE * rect (laws::DUTY[0]);
        case Waveform::WideRectangle: return laws::WIDE * rect (laws::DUTY[1]);
        case Waveform::NarrowRectangle: return laws::NARROW * rect (laws::DUTY[2]);
    }
    return 0.0f;
}

float Osc::peak (Waveform wave)
{
    switch (wave)
    {
        case Waveform::Triangle: return laws::TRIANGLE;
        case Waveform::SharkTooth: return laws::SHARK_TRIANGLE + laws::SHARK_SAWTOOTH;
        case Waveform::Sawtooth: return laws::SAWTOOTH;
        case Waveform::ReverseSawtooth: return laws::REVERSE_SAWTOOTH;
        case Waveform::Square: return laws::SQUARE;
        case Waveform::WideRectangle: return laws::WIDE;
        case Waveform::NarrowRectangle: return laws::NARROW;
    }
    return laws::SAWTOOTH;
}

float Noise::white()
{
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return (float) (int32_t) x * (1.0f / 2'147'483'648.0f);
}

std::tuple<float, float, float> Noise::tick (const NoiseLaw& c)
{
    const float w = white() * c.scale;
    auto& b = pink;
    b[0] = c.poles[0] * b[0] + w * c.gains[0];
    b[1] = c.poles[1] * b[1] + w * c.gains[1];
    b[2] = c.poles[2] * b[2] + w * c.gains[2];
    const float pk = 0.25f * (b[0] + b[1] + b[2] + w * 0.1848f);
    red = c.red.first * red + c.red.second * pk;
    {
        const auto [x0, y0] = hp;
        const float h = c.band[0] * (y0 + w - x0);
        hp = { w, h };
        lp += (h - lp) * c.band[1];
    }
    {
        const auto [x0, y0] = pink_hp;
        const float h = c.band[2] * (y0 + pk - x0);
        pink_hp = { pk, h };
        pink_lp += (h - pink_lp) * c.band[3];
    }
    return { lp, pink_lp, red };
}

float Ladder::tick (float x, float g, float k, float bias, std::tuple<float, float, float> limit)
{
    const float h = 1.0f - g;
    const float g2 = g * g;
    const float g4 = g2 * g2;
    const float rest = h * (g * (g * (g * s[0] + s[1]) + s[2]) + s[3]);
    const float y4 = (g4 * x + rest) / (1.0f + k * g4);
    const auto [top, inv, rest_bias] = limit;
    const float u = x - k * y4 + bias;
    float y = (top == 1.0f ? tanh (u) : top * tanh (u * inv)) - rest_bias;
    const float q = k * g4;
    const float v = x - k * rest + bias;
    float lo = -top - rest_bias;
    float hi = top - rest_bias;
    bool converged = false;
    for (int it = 0; it < 24; ++it)
    {
        const float a = (v - q * y) * inv;
        const float t = tanh (a);
        const float r = y - (top * t - rest_bias);
        if (std::fabs (r) < 1e-6f)
        {
            converged = true;
            break;
        }
        if (r < 0.0f)
            lo = y;
        else
            hi = y;
        float slope;
        if (std::fabs (a) >= 3.0f)
        {
            slope = 0.0f;
        }
        else
        {
            const float a2 = a * a;
            const float d = (a2 - 9.0f) / (a2 + 3.0f);
            slope = d * d / 9.0f;
        }
        const float next = y - r / (1.0f + q * top * inv * slope);
        y = (next >= lo && next <= hi) ? next : (lo + hi) * 0.5f;
    }
    if (! converged)
        ca72::unconverged::note (ca72::unconverged::Solver::FilterLoop);
    for (auto& st : s)
    {
        const float vv = (y - st) * g;
        const float out = vv + st;
        st = out + vv;
        y = out;
    }
    return y;
}

ContourLaw ContourLaw::new_ (ca72::voice::ContourKnobs k, bool decay_switch, size_t which, double rate)
{
    using Law = std::span<const std::pair<double, double>>;
    const Law attack_law = which == 0 ? Law (ca72::voice::FILTER_ATTACK) : Law (ca72::voice::LOUDNESS_ATTACK);
    const Law decay_law = which == 0 ? Law (ca72::voice::FILTER_DECAY) : Law (ca72::voice::LOUDNESS_DECAY);
    auto time = [] (std::tuple<double, double, double> abt, double ohms)
    {
        const auto [a, b, t0] = abt;
        return a * std::pow (ohms / 1e5, b) + t0;
    };
    const double r_decay = ca72::voice::time_pot (k.decay, decay_law);
    const double tau_a = time (laws::ATTACK_TIME[which], ca72::voice::time_pot (k.attack, attack_law));
    const double rest = laws::REST[which];
    const double peak = laws::PEAK[which];
    const double aim = rest + laws::ATTACK_AIM[which] * (peak - rest);
    const auto [c0, c1, c2, c3] = laws::SUSTAIN[which];
    const double s = ca72::voice::sustain_track (k.sustain);
    double tau_r, released;
    if (decay_switch)
    {
        tau_r = time (laws::RELEASE_TIME[which], r_decay);
        released = laws::RELEASED[which];
    }
    else
    {
        const double d = ca72::clamp (k.decay, 0.0, 1.0);
        tau_r = interpolate (laws::RELEASE_OFF_AT, laws::RELEASE_OFF[which], d);
        released = interpolate (laws::RELEASE_OFF_AT, laws::RELEASED_OFF[which], d);
    }
    auto per = [rate] (double tau) { return 1.0 - std::exp (-1.0 / (tau * rate)); };
    ContourLaw l;
    l.attack = per (tau_a);
    l.decay = per (time (laws::DECAY_TIME[which], r_decay));
    l.release = per (tau_r);
    l.aim = aim;
    l.stop = peak - (aim - peak) * laws::ATTACK_ESR[which] / tau_a;
    l.sustain = c0 + s * (c1 + s * (c2 + s * c3));
    l.released = released;
    l.rest = rest;
    l.fall = laws::DECAY_FALL[which];
    return l;
}

Contour Contour::at_rest (const ContourLaw& law)
{
    return Contour { law.rest, false, true };
}

double Contour::tick (bool gate, const ContourLaw& law)
{
    if (gate)
    {
        fresh = false;
        if (attacking)
        {
            const double next = volts + (law.aim - volts) * law.attack;
            if (next >= law.stop)
            {
                volts = std::fmax (law.stop, volts);
                attacking = false;
            }
            else
            {
                volts = next;
            }
        }
        else
        {
            const double to_go = volts - law.sustain;
            volts -= to_go * law.decay * (1.0 + law.fall * to_go);
        }
    }
    else if (! fresh)
    {
        attacking = false;
        volts += (law.released - volts) * law.release;
    }
    return volts;
}

Light Light::new_ (double rate)
{
    const double filter_rate = rate * (double) FILTER_OVERSAMPLE;
    auto filter_up = ca72::resample::Interpolator::sparse (FILTER_OVERSAMPLE);
    auto filter_down = ca72::resample::Decimator::sparse (FILTER_OVERSAMPLE);
    const double filter_delay = filter_up.delay() + filter_down.delay();
    auto at = [rate] (double s) { return as_u32 (std::round (s * rate)); };
    const auto law = ContourLaw::new_ (ca72::voice::Panel::default_().filter_contour, true, 0, rate);
    Noise nz;
    nz.state = 0x4D6F'6F67;
    Light l {
        .rate_ = rate,
        .panel = ca72::voice::Panel::default_(),
        .set_for_panel = false,
        .set = Setting::default_(),
        .held_ = Held::default_(),
        .pressed = {},
        .presses = 0,
        .waiting = Held::default_(),
        .gap = at (ca72::voice::RETRIGGER_GAP),
        .lead = { 0, 0 },
        .lead_samples = at (laws::RELEASE_LEAD),
        .target = laws::PITCH_AT_REST,
        .pitch = laws::PITCH_AT_REST,
        .foot = 0.0,
        .beyond = 0.0,
        .gate = false,
        .trigger = false,
        .pending = 0,
        .attack = false,
        .open = std::numeric_limits<uint32_t>::max(),
        .rearm = at (laws::REARM),
        .trigger_on = at (laws::TRIGGER_ON),
        .trigger_off = at (laws::TRIGGER_OFF),
        .osc = { Osc { 0.0f, Waveform::Sawtooth }, Osc { 0.0f, Waveform::Sawtooth }, Osc { 0.0f, Waveform::Sawtooth } },
        .dt = {},
        .dt_step = {},
        .inv = {},
        .noise = nz,
        .noise_c = NoiseLaw::new_ (rate),
        .feedback_delay = (float) std::fmax (interpolate (laws::FEEDBACK_AT, laws::FEEDBACK_DELAY, rate) - filter_delay, 1.0),
        .coupling = { (float) std::exp (-TAU * laws::COUPLING_HZ / filter_rate), 0.0f, 0.0f },
        .droop = { (float) std::exp (-TAU * laws::DROOP_HZ / rate), 0.0f, 0.0f },
        .mode = { (float) std::exp (-TAU * laws::MODE_HZ / filter_rate), 0.0f, 0.0f },
        .direct = 0.0f,
        .pass = 1.0f,
        .thump_c = { (float) std::exp (-1.0 / (laws::THUMP_TIMES.first * rate)),
                     (float) (1.0 - std::exp (-1.0 / (laws::THUMP_TIMES.second * rate))) },
        .thump = { 0.0f, 0.0f, 0.0f },
        .bias = tanh (laws::BIAS),
        .ladder = Ladder::default_(),
        .filter_up = std::move (filter_up),
        .filter_down = std::move (filter_down),
        .g = 0.0f,
        .g_step = 0.0f,
        .k = 0.0f,
        .contours_ = { Contour::at_rest (law), Contour::at_rest (law) },
        .count = 0,
        .detune = {},
        .cutoff_offset = 0.0,
        .osc3 = 0.0f,
        .a440 = { 0.0f, (float) (440.0 / rate) },
        .feedback = 0.0,
        .fed_back = {},
        .fed_at = 0,
        .lamp = 0.0,
        .ext_hp = { std::exp (-TAU * laws::EXT_COUPLING_HZ / rate), 0.0, 0.0 },
        .pre_hp = { (float) std::exp (-TAU * laws::PREAMP_COUPLING_HZ / rate), 0.0f, 0.0f },
        .overload_peak = 0.0,
        .drive = 1.0f,
        .drive_top = 1.0f,
        .limit = { 1.0f, 1.0f, 0.0f },
    };
    l.set_drive (1.0);
    l.set_panel (ca72::voice::Panel::default_());
    l.contours_ = { Contour::at_rest (l.set.contours[0]), Contour::at_rest (l.set.contours[1]) };
    return l;
}

void Light::set_drive (double gain_)
{
    const double gain = std::fmax (gain_, 1e-3);
    drive = (float) gain;
    drive_top = (float) std::fmin (std::pow (gain, laws::DRIVE_CEILING), 1.0);
    const float top = drive_top;
    limit = { top, 1.0f / top, top * tanh (laws::BIAS / top) };
}

void Light::start_oscillators_at (double at)
{
    const float a = (float) ca72::clamp (at, 0.0, 0.98);
    for (auto& o : osc)
        o.phase = a;
}

void Light::set_seed (uint64_t seed)
{
    const uint32_t s = (uint32_t) (seed ^ (seed >> 32));
    noise.state = s == 0 ? 0x4D6F'6F67u : s;
}

void Light::set_panel (const ca72::voice::Panel& p)
{
    if (set_for_panel && p == panel)
        return;
    set_for_panel = true;
    panel = p;
    auto cubic = [] (const std::array<double, 4>& c, double x) { return c[0] + x * (c[1] + x * (c[2] + x * c[3])); };
    for (size_t o = 0; o < 3; ++o)
    {
        const double range = (2.0 - p.osc[o].range.octaves_below_2()) * 12.0;
        const double x = ca72::clamp (p.osc[o].freq, 0.0, 1.0);
        const bool free = o == 2 && ! p.osc3_control;
        double freq;
        if (o == 0)
        {
            freq = 0.0;
        }
        else if (o == 1)
        {
            auto at = [&] (double v) { return cubic (laws::OSC2_CENTS, v); };
            freq = (at (x) - at (ca72::tuning::FREQ_CENTRE)) / 100.0;
        }
        else if (free)
        {
            auto quintic = [] (double v)
            {
                double a = 0.0;
                for (size_t i = laws::OSC3_FREE_CENTS.size(); i-- > 0;)
                    a = a * v + laws::OSC3_FREE_CENTS[i];
                return a;
            };
            freq = (quintic (x) - quintic (ca72::tuning::FREQ_CENTRE)) / 100.0;
        }
        else
        {
            auto at = [&] (double v) { return cubic (laws::OSC3_CENTS, v); };
            freq = (at (x) - at (ca72::tuning::FREQ_CENTRE)) / 100.0;
        }
        osc[o].wave = p.osc[o].waveform;
        const double tune = free ? 0.0 : p.tune * laws::TUNE_CENTS / 100.0;
        set.offset[o] = range + freq + tune;
        double source;
        switch (p.osc[o].waveform)
        {
            case Waveform::Triangle: source = laws::SOURCE_OHMS[0]; break;
            case Waveform::Sawtooth:
            case Waveform::ReverseSawtooth: source = laws::SOURCE_OHMS[1]; break;
            case Waveform::SharkTooth: source = laws::SOURCE_OHMS[3]; break;
            default: source = laws::SOURCE_OHMS[2]; break;
        }
        const float level = o == 2 ? laws::OSC3_LEVEL : 1.0f;
        set.gain[o] = p.osc[o].on ? channel (ca72::voice::volume_track (p.osc[o].volume), source, 33e3) * level : 0.0f;
    }
    set.follows = { true, true, p.osc3_control };
    if (p.noise_on)
    {
        const auto [ohms, level] = p.noise_pink ? std::pair<double, float> { laws::NOISE_OHMS.second, laws::PINK }
                                                : std::pair<double, float> { laws::NOISE_OHMS.first, laws::WHITE };
        set.noise = channel (ca72::voice::volume_track (p.noise_volume), ohms, 11e3) / channel (1.0, ohms, 11e3) * level;
    }
    else
    {
        set.noise = 0.0f;
    }
    const float slope = 1.0f - bias * bias;
    const double passband = (double) (laws::OUT * laws::DRIVE * slope) / (1.0 + laws::LOOP[3][0] * laws::LOOP_SCALE);
    direct = laws::MODE_GAIN * laws::DRIVE * slope / (1.0f + (float) (laws::LOOP[9][0] * laws::LOOP_SCALE));
    set.ext = p.ext_on ? (float) (1.0 / passband) : 0.0f;
    const double t = ca72::voice::ext_taper (p.ext_volume);
    set.ext_gain = laws::EXT_GAIN * t / (1.0 + laws::EXT_LOAD * t * (1.0 - t));
    const double knob = ca72::clamp (p.cutoff, 0.0, 1.0) * 20.0;
    const size_t i = std::min ((size_t) std::floor (knob), (size_t) 19);
    const double a = knob - (double) i;
    const auto& c = laws::CUTOFF_LOG2;
    set.cutoff = (double) c[i] + ((double) c[i + 1] - (double) c[i]) * a;
    if (! p.keyboard_control_1 && ! p.keyboard_control_2)
        set.tracking = 0.0;
    else if (p.keyboard_control_1 && ! p.keyboard_control_2)
        set.tracking = laws::KEYBOARD_CONTROL[0];
    else if (! p.keyboard_control_1 && p.keyboard_control_2)
        set.tracking = laws::KEYBOARD_CONTROL[1];
    else
        set.tracking = laws::KEYBOARD_CONTROL[2];
    constexpr std::array<double, 5> AMOUNT_AT { 0.0, 0.25, 0.5, 0.75, 1.0 };
    set.contour = laws::CONTOUR_OCTAVES_PER_VOLT * interpolate (AMOUNT_AT, laws::AMOUNT, p.contour_amount);
    const double emphasis = 1.0 - std::log (1.0 + 80.0 * ca72::voice::emphasis_r14 (p.emphasis) / 50e3) / std::log (81.0);
    set.corner = ca72::clamp ((0.744 - emphasis) / 0.044, 0.0, 1.0);
    for (size_t r = 0; r < set.emphasis.size(); ++r)
    {
        const double log2 = laws::LOOP_AT_LOG2[r];
        const double threshold = interpolate (laws::THRESHOLD_AT_LOG2, laws::THRESHOLD, log2);
        const double shift = interpolate (laws::THRESHOLD_AT_LOG2, laws::SHIFT, log2);
        const double near = 1.0 - ca72::clamp ((threshold - emphasis) / laws::EMPHASIS_NEAR, 0.0, 1.0);
        const double at = interpolate (laws::EMPHASIS_AT, laws::LOOP[r], emphasis + near * shift);
        set.emphasis[r] = laws::LOOP_SCALE * (at > 4.0 ? 4.0 + (at - 4.0) * laws::RING_GAIN : at);
    }
    const double rw = ca72::modulation::mod_wheel_r (p.mod_wheel);
    double w = 3.0 * std::log (1.0 + rw / 1.2e3 * (std::pow (81.0, 1.0 / 3.0) - 1.0)) / std::log (81.0);
    w = ca72::clamp (w, 0.0, 1.0);
    const auto [wa, wb, wc] = laws::WHEEL;
    set.depth = w * (wa + w * (wb + w * wc));
    set.offset_depth = laws::OFFSET.first * rw / (rw + laws::OFFSET.second);
    set.mix = (float) ca72::clamp (p.mod_mix, 0.0, 1.0);
    set.osc_mod = p.osc_mod;
    set.filter_mod = p.filter_mod;
    const Waveform w3 = p.osc[2].waveform;
    set.swing = 1.0f / Osc::peak (w3);
    size_t kind = 0;
    std::optional<float> duty;
    switch (w3)
    {
        case Waveform::Triangle: kind = 0; break;
        case Waveform::ReverseSawtooth:
        case Waveform::SharkTooth: kind = 1; break;
        case Waveform::Sawtooth: kind = 2; break;
        case Waveform::Square: kind = 3; duty = laws::DUTY[0]; break;
        case Waveform::WideRectangle: kind = 3; duty = laws::DUTY[1]; break;
        case Waveform::NarrowRectangle: kind = 3; duty = laws::DUTY[2]; break;
    }
    set.swing_bias = duty.has_value() ? 2.0f * *duty - 1.0f : 0.0f;
    set.mod_swing = (float) laws::MOD_SWING[kind];
    set.mod_centre = (float) laws::MOD_CENTRE[kind];
    const double ohms = ca72::voice::glide_r (p.glide, p.glide_on) + 330.0;
    const double step = laws::GLIDE_RATE / ohms * (double) CONTROL_EVERY / rate_;
    set.glide = { step, step * laws::GLIDE_FALLING };
    set.beyond = std::exp (-((double) CONTROL_EVERY / rate_) / (ohms * laws::GLIDE_FARADS));
    set.contours = { ContourLaw::new_ (p.filter_contour, p.decay, 0, rate_),
                     ContourLaw::new_ (p.loudness_contour, p.decay, 1, rate_) };
}

void Light::set_offsets (std::array<double, 3> detune_, double cutoff)
{
    detune = detune_;
    cutoff_offset = cutoff;
}

void Light::note (int32_t midi, bool on)
{
    if (midi < 0 || midi >= 128)
        return;
    if (on && ! held_.has (midi) && ! waiting.has (midi))
    {
        presses = presses + 1u;
        pressed[(size_t) midi] = presses;
        if (panel.retrigger)
        {
            waiting.set (midi, true);
            return;
        }
    }
    if (! on && waiting.has (midi))
    {
        waiting.set (midi, false);
        return;
    }
    if (! on && sounding() == std::optional<int32_t> (midi))
        lead = { midi, lead_samples };
    held_.set (midi, on);
}

std::optional<int32_t> Light::sounding() const
{
    switch (panel.priority)
    {
        case ca72::voice::Priority::Low: return held_.lowest();
        case ca72::voice::Priority::High: return held_.highest();
        case ca72::voice::Priority::Last: return held_.newest (pressed);
    }
    return held_.lowest();
}

bool Light::held() const { return held_.any() || waiting.any(); }

bool Light::trigger_closed() const { return gate; }

std::optional<std::array<double, 3>> Light::slow_state() const
{
    if (held() || trigger || pending > 0)
        return std::nullopt;
    return std::array<double, 3> { (pitch + beyond) / 12.0, contours_[0].volts, contours_[1].volts };
}

std::pair<double, double> Light::contours() const { return { contours_[0].volts, contours_[1].volts }; }

double Light::overload() const { return lamp; }

double Light::take_overload_peak() { return std::exchange (overload_peak, 0.0); }

void Light::rest()
{
    held_ = Held::default_();
    waiting = Held::default_();
    lead = { 0, 0 };
    gate = false;
    trigger = false;
    pending = 0;
    open = std::numeric_limits<uint32_t>::max();
    contours_ = { Contour::at_rest (set.contours[0]), Contour::at_rest (set.contours[1]) };
    ladder = Ladder::default_();
    filter_up.reset();
    filter_down.reset();
    coupling = { std::get<0> (coupling), 0.0f, 0.0f };
    droop = { std::get<0> (droop), 0.0f, 0.0f };
    mode = { std::get<0> (mode), 0.0f, 0.0f };
    thump = { 0.0f, 0.0f, 0.0f };
    ext_hp = { std::get<0> (ext_hp), 0.0, 0.0 };
    pre_hp = { std::get<0> (pre_hp), 0.0f, 0.0f };
    fed_back = {};
    for (auto& o : osc)
        o.phase = 0.0f;
    pitch = target;
    beyond = foot;
}

void Light::control()
{
    using ca72::voice::Priority;
    std::optional<int32_t> snd = sounding();
    if (lead.second > 0)
    {
        lead.second = lead.second >= (uint32_t) CONTROL_EVERY ? lead.second - (uint32_t) CONTROL_EVERY : 0u;
        const int32_t ld = lead.first;
        if (snd.has_value())
        {
            const int32_t k = *snd;
            switch (panel.priority)
            {
                case Priority::Low: snd = std::min (k, ld); break;
                case Priority::High: snd = std::max (k, ld); break;
                case Priority::Last: snd = pressed[(size_t) ld] > pressed[(size_t) k] ? ld : k; break;
            }
        }
    }
    if (snd.has_value())
    {
        const auto [lo, hi] = laws::KEYBOARD_KEYS;
        const double key = (double) *snd;
        target = ca72::clamp (key, lo, hi);
        foot = key - target;
        const auto [up, down] = set.glide;
        pitch += ca72::clamp (target - pitch, -down, up);
        beyond = foot + (beyond - foot) * set.beyond;
    }
    const double pt = pitch + beyond;
    const double wheel = panel.pitch_wheel * ca72::modulation::PITCH_WHEEL_SEMITONES;
    for (size_t o = 0; o < 3; ++o)
    {
        const double key = set.follows[o] ? pt + wheel : laws::OSC3_FREE_KEY;
        const double semis = key - 69.0 + set.offset[o] + detune[o] / 100.0;
        const double hz = laws::A4 * (double) exp2 ((float) (semis / 12.0));
        const float to = std::fmin ((float) (hz / rate_), 0.45f);
        dt_step[o] = (to - dt[o]) / (float) CONTROL_EVERY;
        inv[o] = 1.0f / std::fmax (to, 1e-9f);
    }
    const auto [g_to, k_to] = coefficients (0.0);
    g_step = (g_to - g) / (float) CONTROL_EVERY;
    k = k_to;
    const double log2 = set.cutoff + set.tracking * (pitch + beyond - laws::TRACKING_FROM) / 12.0
                        + set.contour * (contours_[0].volts - set.contours[0].rest) + cutoff_offset;
    const double pss = interpolate (laws::PASS_AT_LOG2, laws::PASS_DB, log2);
    pass = exp2 ((float) (pss * (3.321928094887362 / 20.0)));
    const double ringing = 1.0 - set.corner;
    float top;
    if (ringing > 0.0)
    {
        const double lift = interpolate (laws::RING_LEVEL_AT_LOG2, laws::RING_LEVEL, log2);
        top = drive_top * (float) (1.0 + ringing * (lift - 1.0));
    }
    else
    {
        top = drive_top;
    }
    if (top != std::get<0> (limit))
        limit = { top, 1.0f / top, top * tanh (laws::BIAS / top) };
}

std::pair<float, float> Light::coefficients (double extra) const
{
    const Setting& s = set;
    const double rise = contours_[0].volts - s.contours[0].rest;
    const double log2 = s.cutoff + s.tracking * (pitch + beyond - laws::TRACKING_FROM) / 12.0 + s.contour * rise + cutoff_offset + extra;
    const double corner = interpolate (laws::CORNER_AT_LOG2, laws::CORNER, log2) * s.corner
                          + interpolate (laws::RING_RAISE_AT_LOG2, laws::RING_RAISE, log2) * (1.0 - s.corner);
    double frequency_log = log2 + corner;
    if (frequency_log > laws::CUTOFF_SAT_AT[0])
        frequency_log = interpolate (laws::CUTOFF_SAT_AT, laws::CUTOFF_SAT, frequency_log);
    const double hz = (double) exp2 ((float) frequency_log);
    const double w = std::fmin (std::numbers::pi * hz / (rate_ * (double) FILTER_OVERSAMPLE), 1.4);
    const double w2 = w * w;
    double t = w * (15.0 - w2) / (15.0 - 6.0 * w2);
    t = (std::isfinite (t) && t > 0.0) ? t : 1e3;
    const double kk = interpolate (laws::LOOP_AT_LOG2, s.emphasis, log2);
    return { (float) (t / (1.0 + t)), (float) kk };
}

double Light::tick (double ext)
{
    if (count == 0)
    {
        count = CONTROL_EVERY;
        control();
    }
    count -= 1;
    if (waiting.any() && ! gate && open >= gap)
    {
        held_.bits[0] |= waiting.bits[0];
        held_.bits[1] |= waiting.bits[1];
        waiting = Held::default_();
    }
    const bool gt = held_.any() && ! waiting.any();
    if (gt != gate)
    {
        gate = gt;
        if (gt)
        {
            attack = open >= rearm;
            if (attack)
                pending = std::max (trigger_on, 1u);
            else if (trigger)
                pending = 0;
            else
                pending = 1;
        }
        else
        {
            pending = std::max (trigger_off, 1u);
        }
        open = 0;
    }
    if (! gt)
        open = open == std::numeric_limits<uint32_t>::max() ? open : open + 1;
    if (pending > 0)
    {
        pending -= 1;
        if (pending == 0)
        {
            trigger = gt;
            if (gt)
                for (auto& c : contours_)
                    c.attacking = attack;
        }
    }
    const bool trig = trigger;
    contours_[0].tick (trig, set.contours[0]);
    const double loud = contours_[1].tick (trig, set.contours[1]);

    const Setting& s = set;
    const auto gain = s.gain;
    const float noise_gain = s.noise;
    const float ext_bus = s.ext;
    const double ext_gain = s.ext_gain;
    const double depth = s.depth;
    const double offset_depth = s.offset_depth;
    const float mix_k = s.mix;
    const float swing = s.swing;
    const float swing_bias = s.swing_bias;
    const float mod_swing = s.mod_swing;
    const float mod_centre = s.mod_centre;
    const bool osc_mod = s.osc_mod;
    const bool filter_mod = s.filter_mod;
    const bool follows3 = s.follows[2];
    for (size_t o = 0; o < 3; ++o)
        dt[o] += dt_step[o];
    auto dts = dt;
    auto invs = inv;
    float gg = g + g_step;
    g = gg;
    float kk = k;
    const bool modulating = depth > 0.0 && (osc_mod || filter_mod);
    const bool needs_noise = noise_gain != 0.0f || (modulating && mix_k > 0.0f);
    float white = 0.0f, pink = 0.0f, red = 0.0f;
    if (needs_noise)
        std::tie (white, pink, red) = noise.tick (noise_c);
    if (modulating)
    {
        float n;
        double rms;
        if (panel.noise_pink)
        {
            n = red;
            rms = 2.163;
        }
        else
        {
            n = pink;
            rms = 0.425;
        }
        const float o3 = (osc3 * swing + swing_bias) * mod_swing + mod_centre;
        const double mix = (double) mix_k;
        const float dep = (float) depth;
        const float offset = (float) offset_depth;
        if (osc_mod)
        {
            const float semis = dep * ((1.0f - mix_k) * o3 + mix_k * n * (float) (laws::NOISE_SEMITONES_RMS / rms))
                                + offset * (float) ((1.0 - mix) * laws::MODULATION_OFFSET.first + mix * laws::NOISE_OFFSET.first);
            const float r = exp2 (semis * (1.0f / 12.0f));
            const float ri = 1.0f / r;
            for (size_t o = 0; o < (follows3 ? 3u : 2u); ++o)
            {
                dts[o] = std::fmin (dts[o] * r, 0.45f);
                invs[o] *= ri;
            }
        }
        if (filter_mod)
        {
            const float octaves = dep * ((1.0f - mix_k) * o3 * (float) laws::FILTER_PER_SEMITONE + mix_k * n * (float) (laws::NOISE_OCTAVES_RMS / rms))
                                  + offset * (float) ((1.0 - mix) * laws::MODULATION_OFFSET.second + mix * laws::NOISE_OFFSET.second);
            std::tie (gg, kk) = coefficients ((double) octaves);
        }
    }
    const float w0 = gain[0] != 0.0f ? osc[0].tick (dts[0], invs[0]) : 0.0f;
    const float w1 = gain[1] != 0.0f ? osc[1].tick (dts[1], invs[1]) : 0.0f;
    const float w2 = (gain[2] != 0.0f || modulating) ? osc[2].tick (dts[2], invs[2]) : 0.0f;
    osc3 = w2;
    const float nz = panel.noise_pink ? pink : white;
    double ext_in;
    if (feedback > 0.0)
    {
        const float d = feedback_delay;
        auto back = [this] (size_t n) { return fed_back[(fed_at + 128 - n) & 127]; };
        const size_t n = as_usize (d);
        const float f = d - std::trunc (d);
        ext_in = ext + feedback * (double) (back (n) + (back (n + 1) - back (n)) * f);
    }
    else
    {
        ext_in = ext;
    }
    float pre = 0.0f;
    if (ext_in != 0.0 || std::get<2> (ext_hp) != 0.0)
    {
        const auto [a, x0, y0] = ext_hp;
        double y = a * (y0 + ext_in - x0);
        if (std::fabs (y) < 1e-15 && ext_in == 0.0)
            y = 0.0;
        ext_hp = { a, ext_in, y };
        ext_in = y;
    }
    else
    {
        ext_in = 0.0;
    }
    if (ext_in != 0.0)
    {
        const double x = ext_in * ext_gain;
        const auto [on, full] = laws::OVERLOAD;
        const double lmp = ca72::clamp ((std::fabs (x) - on) / (full - on), 0.0, 1.0);
        lamp = lmp;
        overload_peak = std::fmax (overload_peak, lmp);
        pre = tanh ((float) (x / laws::EXT_CLIP)) * (float) laws::EXT_CLIP;
    }
    if (pre != 0.0f || std::get<2> (pre_hp) != 0.0f)
    {
        const auto [a, x0, y0] = pre_hp;
        float y = a * (y0 + pre - x0);
        if (std::fabs (y) < 1e-12f && pre == 0.0f)
            y = 0.0f;
        pre_hp = { a, pre, y };
        pre = y;
    }
    const float bus = gain[0] * w0 + gain[1] * w1 + gain[2] * w2 + noise_gain * nz + ext_bus * pre;
    std::array<double, FILTER_OVERSAMPLE> inputs {};
    filter_up.push ((double) bus, inputs);
    double filtered = 0.0;
    for (const double input : inputs)
    {
        const float b = (float) input;
        float y = ladder.tick (laws::DRIVE * drive * b, ca72::clamp (gg, 0.0f, 0.999f), kk, laws::BIAS, limit);
        y = y * pass;
        const auto [ca, cx0, cy0] = coupling;
        const float lo = ca * (cy0 + y - cx0);
        coupling = { ca, y, lo };
        float yo;
        if (panel.filter_hi)
        {
            const auto [ma, mx0, my0] = mode;
            const float d = direct * b;
            const float dir = ma * (my0 + d - mx0);
            mode = { ma, d, dir };
            yo = dir - lo;
        }
        else
        {
            std::get<1> (mode) = 0.0f;
            std::get<2> (mode) = 0.0f;
            yo = lo;
        }
        if (const auto out = filter_down.push ((double) yo); out.has_value())
            filtered = *out;
    }
    float y = (float) filtered;
    const auto& vg = laws::VCA_GAIN;
    const float v = (float) (std::fmax (loud, 0.0) * (1.0 / laws::VCA_STEP));
    const size_t i = std::min (as_usize (v), vg.size() - 2);
    const float f = std::fmin (v - (float) i, 1.0f);
    const float gn = vg[i] + (vg[i + 1] - vg[i]) * f;
    y = y * laws::OUT * gn;
    const auto [da, dx0, dy0] = droop;
    const float coupled = da * (dy0 + y - dx0);
    droop = { da, y, coupled };
    const auto [ha, lb] = thump_c;
    const auto [g0, h0, l0] = thump;
    const float h = ha * (h0 + gn - g0);
    const float l = l0 + lb * (h - l0);
    thump = { gn, h, l };
    double out = (double) (coupled + laws::THUMP * l);
    if (panel.a440)
    {
        const auto [phase, step] = a440;
        float p = phase + step;
        p = p >= 1.0f ? p - 1.0f : p;
        a440.first = p;
        const float at = p * 64.0f;
        const size_t j = std::min (as_usize (at), (size_t) 63);
        const float fr = at - (float) j;
        const float a = A440_SHAPE[j];
        const float b = A440_SHAPE[(j + 1) % 64];
        out += (double) ((a + (b - a) * fr) * laws::A440_LEVEL);
    }
    fed_back[fed_at] = (float) out;
    fed_at = (fed_at + 1) & 127;
    return out;
}
} // namespace ca72::light
