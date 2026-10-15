#include "PolyEngine.h"

namespace ca72poly
{
namespace
{
constexpr double CENTS = 4.0, DRIFT_CENTS = 3.0, DRIFT_TAU = 4.0, WAVER_CENTS = 0.4, WAVER_HZ = 30.0;
constexpr double CUTOFF = 0.04, CUTOFF_DRIFT = 0.015, KNOB = 0.012;
constexpr uint32_t STEP = 64;
constexpr double TAU = 6.283185307179586;

uint64_t voiceSeed (uint64_t seed, size_t k) { return k == 0 ? seed : seed ^ ((uint64_t) (k + 1) << 48); }

std::optional<double> outOfStep (size_t k, uint64_t s)
{
    if (k == 0)
        return std::nullopt;
    uint64_t x = (voiceSeed (s, k) + 3) * 0x9E3779B97F4A7C15ull;
    return (double) ((x ^ (x >> 29)) >> 11) / (double) (1ull << 53);
}
} // namespace

Character Character::make (uint64_t seed)
{
    Character c;
    c.rng = (seed ^ 0x5EED0FA1A1060000ull) | 1;
    for (auto& t : c.tune) t = c.normal();
    c.cutoff = c.normal();
    std::array<double, 10> knobs {};
    for (auto& k : knobs) k = c.normal();
    for (size_t i = 0; i < c.knob.size(); ++i) c.knob[i] = knobs[i];
    for (auto& d : c.drift) d = c.normal();
    return c;
}

double Character::uniform()
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    const uint64_t x = rng * 0x2545F4914F6CDD1Dull;
    return (double) (x >> 11) / (double) (1ull << 53) - 0.5;
}

double Character::normal()
{
    const double a = uniform() + 0.5, b = uniform() + 0.5;
    return std::sqrt (-2.0 * std::log (std::fmax (a, 1e-300))) * std::cos (TAU * b);
}

double Character::knob_at (size_t k, double x, double amount) const
{
    return ca72::clamp (x + amount * KNOB * knob[std::min (k, knob.size() - 1)], 0.0, 1.0);
}

std::pair<std::array<double, 3>, double> Character::offsets (double amount, double r)
{
    if (std::bit_cast<uint64_t> (r) != std::bit_cast<uint64_t> (rate))
    {
        rate = r;
        const double a = std::exp (-(double) STEP / (r * DRIFT_TAU));
        drift_ab = { a, std::sqrt (1.0 - a * a) };
        const double w = std::exp (-TAU * WAVER_HZ / r);
        waver_ws = { w, WAVER_CENTS * std::sqrt (12.0 * (1.0 + w) / (1.0 - w)) };
    }
    if (count == 0)
    {
        count = STEP;
        for (auto& d : drift)
        {
            const double g = (uniform() + uniform() + uniform() + uniform()) * std::sqrt (3.0);
            d = drift_ab.first * d + drift_ab.second * g;
        }
    }
    --count;
    for (auto& w : waver)
        w = waver_ws.first * w + (1.0 - waver_ws.first) * uniform() * waver_ws.second;
    std::array<double, 3> cents {};
    for (size_t k = 0; k < 3; ++k)
        cents[k] = amount * (CENTS * tune[k] + DRIFT_CENTS * drift[k] + waver[k]);
    return { cents, amount * (CUTOFF * cutoff + CUTOFF_DRIFT * drift[3]) };
}

PolyKey PolyKey::make (double rate)
{
    PolyKey k;
    k.gap = (uint32_t) std::ceil (ca72::voice::RETRIGGER_GAP * rate);
    k.open = k.gap;
    return k;
}

template <typename F>
void PolyKey::step (bool gate, int key, F&& note)
{
    if (gate)
    {
        if (! down)
        {
            if (open >= gap)
            {
                note (key, true);
                down = key;
            }
        }
        else if (*down != key)
        {
            note (key, true);
            note (*down, false);
            down = key;
        }
    }
    else if (down)
    {
        note (*down, false);
        down.reset();
        open = 0;
    }
    if (! down && open < std::numeric_limits<uint32_t>::max())
        ++open;
}

void Player::note (int k, bool on)
{
    if (light)
        light->note (k, on);
    else
        voice->note (k, on);
}

void Player::setPanel (const Panel& p, double entropy)
{
    Panel q = p;
    if (entropy > 0.0)
    {
        q.emphasis = ch.knob_at (0, q.emphasis, entropy);
        q.filter_contour.attack = ch.knob_at (1, q.filter_contour.attack, entropy);
        q.filter_contour.decay = ch.knob_at (2, q.filter_contour.decay, entropy);
        q.loudness_contour.attack = ch.knob_at (3, q.loudness_contour.attack, entropy);
        q.loudness_contour.decay = ch.knob_at (4, q.loudness_contour.decay, entropy);
        q.glide = ch.knob_at (5, q.glide, entropy);
    }
    if (shownValid && q == shown)
        return;
    shown = q;
    shownValid = true;
    if (light)
        light->set_panel (q);
    else
        voice->panel = q;
}

double Player::tick (double detune, double oscillators, double entropy, double at, double rate)
{
    std::array<double, 3> cents { detune, detune, detune };
    double cutoff = 0.0;
    if (at > 0.0)
    {
        const auto [c, cut] = ch.offsets (at, rate);
        for (size_t k = 0; k < 3; ++k)
            cents[k] += c[k] * oscillators / at;
        cutoff = cut * entropy / at;
    }
    if (light)
    {
        light->set_offsets (cents, cutoff);
        return light->tick (0.0);
    }
    auto j = ca72::voice::Jacks::default_();
    j.detune = cents;
    j.cutoff = cutoff;
    return voice->tick_jacks (j);
}

void Engine::build (double hostRate, size_t f, bool light, ca72::voice::Quality q, int bleps, bool corners, uint64_t seed)
{
    factor = f;
    voiceRate = hostRate * (double) f;
    isLight = light;
    quality = q;
    decL = ca72::resample::Decimator::new_ (f);
    decR = ca72::resample::Decimator::new_ (f);

    std::optional<ca72::voice::Voice> proto;
    if (! light)
    {
        proto = f > 1 ? ca72::voice::Voice::prototype (voiceRate, std::pair<size_t, size_t> { 1, 1 })
                      : ca72::voice::Voice::prototype (voiceRate);
        if (bleps != 2 || corners)
            proto->set_oscillator_quality (bleps, corners);
    }

    // Mono: one voice and its DOUBLE twin.
    slots.assign (1, Slot {});
    for (size_t k = 0; k < slots.size(); ++k)
    {
        for (int t = 0; t < 2; ++t)
        {
            const size_t j = k + (t == 1 ? MAX_VOICES : 0);
            auto& p = t == 0 ? slots[k].main : slots[k].twin;
            p.ch = Character::make (voiceSeed (seed, j));
            p.key = PolyKey::make (voiceRate);
            if (light)
            {
                p.light = ca72::light::Light::new_ (voiceRate);
                p.light->set_seed (voiceSeed (seed, j));
                if (auto at = outOfStep (j, seed))
                    p.light->start_oscillators_at (*at);
            }
            else
            {
                p.voice = proto;
                p.voice->set_seed (voiceSeed (seed, j));
                if (auto at = outOfStep (j, seed))
                    p.voice->start_oscillators_at (*at);
            }
        }
    }
}

int Engine::voices() const { return std::clamp (set.voices, 2, (int) MAX_VOICES); }

int Engine::instruments() const
{
    if (set.unison) return voices();
    if (set.poly) return 0;
    return 1;
}

void Engine::oneNote (int key, bool on)
{
    for (int k = 0; k < std::max (1, instruments()); ++k)
    {
        auto& s = slots[(size_t) k];
        s.main.note (key, on);
        if (set.doubled || ! on)
            s.twin.note (key, on);
        if (on)
            s.active = true;
    }
}

void Engine::noteOn (int key)
{
    if (key < 0 || key > 127 || slots.empty())
        return;
    held[(size_t) key] = true;
    if (set.unison || ! set.poly)
    {
        oneNote (key, true);
        return;
    }
    const int n = voices();
    int pick = -1;
    auto better = [&] (int a, int b, bool oldest) { return b < 0 || (oldest ? slots[(size_t) a].age > slots[(size_t) b].age : slots[(size_t) a].age < slots[(size_t) b].age); };
    for (int k = 0; k < n; ++k)
    {
        const auto& s = slots[(size_t) k];
        if (s.note == key && s.age > 0 && ! s.gate && better (k, pick, true)) pick = k;
    }
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (! slots[(size_t) k].active && better (k, pick, false)) pick = k;
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (! slots[(size_t) k].gate && better (k, pick, false)) pick = k;
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (better (k, pick, false)) pick = k;
    if (pick < 0)
        pick = 0;
    auto& s = slots[(size_t) pick];
    s.drop = s.active && s.shown;
    s.note = key;
    s.gate = true;
    s.age = ++counter;
    s.active = true;
    s.peak = 0.0;
    s.quiet = 0;
}

void Engine::noteOff (int key)
{
    if (key < 0 || key > 127 || slots.empty())
        return;
    held[(size_t) key] = false;
    if (set.unison || ! set.poly)
    {
        oneNote (key, false);
        return;
    }
    for (auto& s : slots)
        if (s.gate && s.note == key)
            s.gate = false;
}

void Engine::allNotesOff()
{
    for (int k = 0; k < 128; ++k)
        if (held[(size_t) k])
            noteOff (k);
}

void Engine::pan (int k, double& l, double& r, double& tl, double& tr) const
{
    const int n = std::max (1, set.poly || set.unison ? voices() : 1);
    double pos = 0.0;
    if (n > 1 && k > 0)
    {
        const int ring = (k + 1) / 2;
        const int rings = n / 2;
        pos = (k % 2 ? 1.0 : -1.0) * (double) ring / (double) std::max (1, rings);
    }
    pos *= set.spread;
    auto gains = [] (double p, double& gl, double& gr)
    {
        const double a = (ca72::clamp (p, -1.0, 1.0) + 1.0) * 0.25 * 3.141592653589793;
        gl = std::cos (a) * std::sqrt (2.0);
        gr = std::sin (a) * std::sqrt (2.0);
    };
    if (set.doubled)
    {
        const double half = 0.5 * set.spread;
        gains (pos - half, l, r);
        gains (pos + half, tl, tr);
    }
    else
    {
        gains (pos, l, r);
        tl = tr = 0.0;
    }
}

bool Engine::step (double& v, double target, double coef)
{
    if (v == target)
        return false;
    v += (target - v) * coef;
    if (std::fabs (target - v) < 1e-7)
        v = target;
    return true;
}

bool Engine::glide (const Panel& t, double coef)
{
    Panel next = t;
    bool moving = false;
    auto s = [&] (double& out, double cur, double tgt)
    {
        out = cur;
        moving |= step (out, tgt, coef);
    };
    s (next.tune, smooth.tune, t.tune);
    s (next.glide, smooth.glide, t.glide);
    s (next.mod_mix, smooth.mod_mix, t.mod_mix);
    for (size_t o = 0; o < 3; ++o)
    {
        s (next.osc[o].volume, smooth.osc[o].volume, t.osc[o].volume);
        s (next.osc[o].freq, smooth.osc[o].freq, t.osc[o].freq);
    }
    s (next.noise_volume, smooth.noise_volume, t.noise_volume);
    s (next.cutoff, smooth.cutoff, t.cutoff);
    s (next.emphasis, smooth.emphasis, t.emphasis);
    s (next.contour_amount, smooth.contour_amount, t.contour_amount);
    for (auto [o, c, tc] : { std::tuple { &next.filter_contour, &smooth.filter_contour, &t.filter_contour },
                             std::tuple { &next.loudness_contour, &smooth.loudness_contour, &t.loudness_contour } })
    {
        s (o->attack, c->attack, tc->attack);
        s (o->decay, c->decay, tc->decay);
        s (o->sustain, c->sustain, tc->sustain);
    }
    s (next.pitch_wheel, smooth.pitch_wheel, t.pitch_wheel);
    s (next.mod_wheel, smooth.mod_wheel, t.mod_wheel);
    s (next.ext_volume, smooth.ext_volume, t.ext_volume);
    const bool discrete = ! (next == smooth) && ! moving;
    smooth = next;
    return moving || discrete;
}

void Engine::render (float* left, float* right, int n, const Panel& p, const Settings& s)
{
    const bool modeChanged = s.poly != set.poly || s.unison != set.unison;
    if (modeChanged)
    {
        for (auto& sl : slots)
        {
            sl.main.key.lift ([&] (int k, bool on) { sl.main.note (k, on); });
            sl.twin.key.lift ([&] (int k, bool on) { sl.twin.note (k, on); });
            for (int k = 0; k < 128; ++k)
                if (held[(size_t) k])
                {
                    sl.main.note (k, false);
                    sl.twin.note (k, false);
                }
            sl.gate = false;
        }
        held.fill (false);
    }
    set = s;

    const int count = set.poly || set.unison ? voices() : 1;
    // A-440 sounds with no key held, so the first voice keeps running while it is on.
    if (p.a440 && ! slots.empty())
        slots[0].active = true;
    const double entropy = set.entropy * ENTROPY_DEPTH;
    const double oscillators = ENTROPY_FLOOR + entropy;
    const double at = std::fmax (oscillators, entropy);
    const double trim = set.unison ? 1.0 / std::sqrt ((double) count) : 1.0;
    const double pairTrim = set.doubled ? std::sqrt (0.5) : 1.0;

    Panel target = p;
    target.quality = quality;
    if (! primed)
    {
        smooth = target;
        smoothSpread = set.spread;
        smoothDetune = set.detune;
        primed = true;
    }
    // About 10 ms to settle, sample by sample at the host's rate.
    const double coef = 1.0 - std::exp (-1.0 / (0.010 * voiceRate / (double) factor));

    struct Gain { double l, r, tl, tr; };
    std::array<Gain, MAX_VOICES> g {};
    double half = 0.0;
    auto place = [&]
    {
        Settings at_ = set;
        at_.spread = smoothSpread;
        const Settings keep = set;
        set = at_;
        for (int k = 0; k < count; ++k)
            pan (k, g[(size_t) k].l, g[(size_t) k].r, g[(size_t) k].tl, g[(size_t) k].tr);
        set = keep;
        half = set.doubled ? 0.5 * smoothDetune * DOUBLE_CENTS : 0.0;
    };
    place();
    if (set.drive != drive || set.feedback != feedback)
    {
        drive = set.drive;
        feedback = set.feedback;
        for (auto& sl : slots)
            for (auto* pl : { &sl.main, &sl.twin })
            {
                if (pl->light)
                {
                    pl->light->set_drive (drive);
                    pl->light->feedback = feedback;
                }
                else
                {
                    pl->voice->set_drive (drive);
                    pl->voice->feedback = feedback;
                }
            }
    }
    for (int k = 0; k < count; ++k)
    {
        slots[(size_t) k].main.setPanel (smooth, entropy);
        if (set.doubled)
            slots[(size_t) k].twin.setPanel (smooth, entropy);
        slots[(size_t) k].peak = 0.0;
    }

    const bool polyMode = set.poly && ! set.unison;
    for (int i = 0; i < n; ++i)
    {
        if (glide (target, coef))
        {
            // The light model works its laws out again on every change: at most every 16 samples.
            if (! isLight || (i & 15) == 0)
                for (int k = 0; k < count; ++k)
                {
                    slots[(size_t) k].main.setPanel (smooth, entropy);
                    if (set.doubled)
                        slots[(size_t) k].twin.setPanel (smooth, entropy);
                }
        }
        if (step (smoothSpread, set.spread, coef) | step (smoothDetune, set.detune, coef))
            place();

        double outL = 0.0, outR = 0.0;
        for (size_t sub = 0; sub < factor; ++sub)
        {
            double sumL = 0.0, sumR = 0.0;
            for (int k = 0; k < count; ++k)
            {
                auto& sl = slots[(size_t) k];
                if (! sl.active)
                    continue;
                if (polyMode)
                {
                    const bool gate = sl.gate && ! sl.drop;
                    sl.drop = false;
                    sl.main.key.step (gate, sl.note, [&] (int key, bool on) { sl.main.note (key, on); });
                    if (set.doubled)
                        sl.twin.key.step (gate, sl.note, [&] (int key, bool on) { sl.twin.note (key, on); });
                    else
                        sl.twin.key.lift ([&] (int key, bool on) { sl.twin.note (key, on); });
                    sl.shown = gate;
                }
                const double a = sl.main.tick (-half, oscillators, entropy, at, voiceRate) * pairTrim;
                double b = 0.0;
                if (set.doubled)
                    b = sl.twin.tick (half, oscillators, entropy, at, voiceRate) * pairTrim;
                const auto& gk = g[(size_t) k];
                sumL += a * gk.l + b * gk.tl;
                sumR += a * gk.r + b * gk.tr;
                sl.peak = std::fmax (sl.peak, std::fmax (std::fabs (a), std::fabs (b)));
            }
            sumL *= trim;
            sumR *= trim;
            if (factor == 1)
            {
                outL = sumL;
                outR = sumR;
            }
            else
            {
                if (auto y = decL.push (sumL)) outL = *y;
                if (auto y = decR.push (sumR)) outR = *y;
            }
        }
        left[i] = (float) outL;
        right[i] = (float) outR;
    }

    bool anyHeld = false;
    for (bool h : held) anyHeld |= h;
    for (int k = 0; k < count; ++k)
    {
        auto& sl = slots[(size_t) k];
        if (! sl.active)
            continue;
        const bool keysUp = polyMode ? (! sl.gate && ! sl.main.key.down) : ! anyHeld;
        if (keysUp && sl.peak < SILENT && ! (p.a440 && k == 0))
        {
            sl.quiet += n;
            if (sl.quiet >= 256)
                sl.active = false;
        }
        else
        {
            sl.quiet = 0;
        }
    }
}
} // namespace ca72poly
