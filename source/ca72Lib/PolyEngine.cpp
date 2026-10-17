#include "PolyEngine.h"

#if defined(__APPLE__)
 #include <mach/mach.h>
 #include <mach/mach_time.h>
 #include <mach/thread_policy.h>
 #include <pthread.h>
#elif defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <pthread.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
 #include <immintrin.h>
#endif

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

namespace
{
void makeRealtime()
{
#if defined(__APPLE__)
    // Audio workgroups only take real-time threads.
    mach_timebase_info_data_t tb {};
    mach_timebase_info (&tb);
    auto ticks = [&] (double ms) { return (uint32_t) (ms * 1e6 * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t policy { 0, ticks (5.0), ticks (10.0), true };
    thread_policy_set (pthread_mach_thread_np (pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                       (thread_policy_t) &policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
#elif defined(_WIN32)
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    sched_param sp { sched_get_priority_max (SCHED_FIFO) - 1 };
    pthread_setschedparam (pthread_self(), SCHED_FIFO, &sp);
#endif
}

// The floating-point control word (flush-to-zero), carried from the audio thread to each run.
inline uint64_t fpControl()
{
#if defined(__aarch64__)
    uint64_t v;
    __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(v));
    return v;
#elif defined(__x86_64__) || defined(_M_X64)
    return _mm_getcsr();
#else
    return 0;
#endif
}

inline void setFpControl (uint64_t v)
{
#if defined(__aarch64__)
    __asm__ __volatile__ ("msr fpcr, %0" : : "r"(v));
#elif defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr ((unsigned) v);
#else
    (void) v;
#endif
}

inline void relax()
{
#if defined(__aarch64__)
    __asm__ __volatile__ ("yield");
#elif defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#endif
}
} // namespace

#if defined(__APPLE__)
extern "C" void* AudioWorkIntervalCreate (const char* name, uint32_t clock, void* attr);
extern "C" int os_workgroup_join (void* wg, void* token);
extern "C" void os_workgroup_leave (void* wg, void* token);
extern "C" int os_workgroup_interval_start (void* wg, uint64_t start, uint64_t deadline, void* data);
extern "C" int os_workgroup_interval_finish (void* wg, void* data);
extern "C" void os_release (void* object);
#endif

Workers::Workers (size_t n, std::function<void (int)> j, std::function<bool()> join)
    : job (std::move (j)), joinHost (std::move (join))
{
    for (auto& d : done)
        d.store (true);
#if defined(__APPLE__)
    // macOS 11 on; OS_CLOCK_MACH_ABSOLUTE_TIME is 32.
    if (__builtin_available (macOS 11.0, *))
        interval = AudioWorkIntervalCreate ("CA-72 voices", 32, nullptr);
#endif
    for (size_t i = 0; i < n; ++i)
        threads.emplace_back ([this] { loop(); });
}

Workers::~Workers()
{
    quit.store (true);
    gen.fetch_add (1, std::memory_order_release);
    gen.notify_all();
    for (auto& t : threads)
        t.join();
#if defined(__APPLE__)
    if (interval != nullptr)
        os_release (interval);
#endif
}

void Workers::work (uint32_t g)
{
    for (;;)
    {
        const uint64_t v = ticket.fetch_add (1, std::memory_order_acq_rel);
        if ((uint32_t) (v >> 32) != g)
            return;
        const int k = (int) (uint32_t) v;
        if (k >= njobs.load (std::memory_order_relaxed))
            return;
        const int slot = list[(size_t) k];
        job (slot);
        done[(size_t) slot].store (true, std::memory_order_release);
    }
}

uint32_t Workers::run (const int* jobs, int n, std::optional<Clock::time_point> deadline)
{
    for (int i = 0; i < n; ++i)
    {
        list[(size_t) i] = jobs[i];
        done[(size_t) jobs[i]].store (false, std::memory_order_relaxed);
    }
    njobs.store (n, std::memory_order_relaxed);
    fp.store (fpControl(), std::memory_order_relaxed);
    budgetNs.store (deadline ? std::chrono::duration_cast<std::chrono::nanoseconds> (*deadline - Clock::now()).count() : 0,
                    std::memory_order_relaxed);
    const uint32_t g = gen.load (std::memory_order_relaxed) + 1;
    ticket.store ((uint64_t) g << 32, std::memory_order_release);
    gen.store (g, std::memory_order_release);
    gen.notify_all();
    // Every job no worker has taken is played here, then the workers' are waited for.
    work (g);
    auto allDone = [&]
    {
        for (int i = 0; i < n; ++i)
            if (! done[(size_t) jobs[i]].load (std::memory_order_acquire))
                return false;
        return true;
    };
    for (int spin = 0; ! allDone(); ++spin)
    {
        if (deadline && (spin & 63) == 0 && Clock::now() >= *deadline)
            break;
        if (spin < 20000)
            relax();
        else
            std::this_thread::yield();
    }
    uint32_t late = 0;
    for (int i = 0; i < n; ++i)
        if (! done[(size_t) jobs[i]].load (std::memory_order_acquire))
            late |= 1u << jobs[i];
    return late;
}

void Workers::loop()
{
    makeRealtime();
    alignas (8) unsigned char token[64] {}; // os_workgroup_join_token_s
    bool own = false;
    uint32_t seen = gen.load (std::memory_order_acquire), joined = ~0u;
    while (! quit.load (std::memory_order_relaxed))
    {
        if (const uint32_t j = joinGen.load (std::memory_order_acquire); j != joined)
        {
            joined = j;
#if defined(__APPLE__)
            if (own)
                os_workgroup_leave (interval, token);
            own = false;
#endif
            const bool host = joinHost && joinHost();
#if defined(__APPLE__)
            // Without one, macOS ran these threads about half as fast (upstream's decisions.md R11).
            if (! host && interval != nullptr)
                own = os_workgroup_join (interval, token) == 0;
#endif
        }
        uint32_t g = gen.load (std::memory_order_acquire);
        // Spin a little (the next block is often close), then sleep until the next one.
        for (int spin = 0; g == seen && spin < 4000; ++spin)
        {
            relax();
            g = gen.load (std::memory_order_acquire);
        }
        if (g == seen)
        {
            gen.wait (seen, std::memory_order_acquire);
            continue;
        }
        seen = g;
        if (const uint64_t f = fp.load (std::memory_order_relaxed); f != fpControl())
            setFpControl (f);
#if defined(__APPLE__)
        // The first worker of a block opens the interval to its deadline and closes it after.
        uint32_t was = opened.load (std::memory_order_relaxed);
        const int64_t budget = budgetNs.load (std::memory_order_relaxed);
        const bool opener = own && budget > 0 && was != g && opened.compare_exchange_strong (was, g);
        if (opener)
        {
            mach_timebase_info_data_t tb {};
            mach_timebase_info (&tb);
            const uint64_t now = mach_absolute_time();
            os_workgroup_interval_start (interval, now, now + (uint64_t) budget * tb.denom / tb.numer, nullptr);
        }
#endif
        work (g);
#if defined(__APPLE__)
        if (opener)
        {
            const auto until = Clock::now() + std::chrono::nanoseconds (budget);
            for (int i = 0, n = njobs.load(); i < n && Clock::now() < until && gen.load() == g; )
                if (done[(size_t) list[(size_t) i]].load (std::memory_order_acquire)) ++i; else relax();
            os_workgroup_interval_finish (interval, nullptr);
        }
#endif
    }
#if defined(__APPLE__)
    if (own)
        os_workgroup_leave (interval, token);
#endif
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

void Engine::build (double hostRate, size_t f, bool light, ca72::voice::Quality q, int bleps, bool corners, uint64_t seed_)
{
    factor = f;
    voiceRate = hostRate * (double) f;
    isLight = light;
    quality = q;
    decL = ca72::resample::Decimator::new_ (f);
    decR = ca72::resample::Decimator::new_ (f);

    seed = seed_;
    proto.reset();
    fadeStep = std::fmin (1.0, 1.0 / (HUSH_FADE * hostRate));
    if (! light)
    {
        proto = f > 1 ? ca72::voice::Voice::prototype (voiceRate, std::pair<size_t, size_t> { 1, 1 })
                      : ca72::voice::Voice::prototype (voiceRate);
        if (bleps != 2 || corners)
            proto->set_oscillator_quality (bleps, corners);
    }

    // POLY_VOICES slots, each a voice and its DOUBLE twin; mono plays the first.
    slots.assign ((size_t) POLY_VOICES, Slot {});
    alloc.assign ((size_t) POLY_VOICES, Alloc {});
    segments.reserve (256);
    for (size_t k = 0; k < slots.size(); ++k)
    {
        alloc[k].events.reserve (256);
        slots[k].events.reserve (256);
        slots[k].plan.segments.reserve (256);
        slots[k].outL.assign (8192 * f, 0.0);
        slots[k].outR.assign (8192 * f, 0.0);
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

int Engine::voices() const { return std::clamp (set.voices, 2, (int) slots.size()); }

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
        alloc[(size_t) k].events.push_back ({ pos, false, key, on, set.doubled || ! on, false });
        if (on)
            alloc[(size_t) k].active = true;
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
    auto better = [&] (int a, int b, bool oldest) { return b < 0 || (oldest ? alloc[(size_t) a].age > alloc[(size_t) b].age : alloc[(size_t) a].age < alloc[(size_t) b].age); };
    for (int k = 0; k < n; ++k)
    {
        const auto& s = alloc[(size_t) k];
        if (s.note == key && s.age > 0 && ! s.gate && better (k, pick, true)) pick = k;
    }
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (! alloc[(size_t) k].active && better (k, pick, false)) pick = k;
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (! alloc[(size_t) k].gate && better (k, pick, false)) pick = k;
    if (pick < 0)
        for (int k = 0; k < n; ++k)
            if (better (k, pick, false)) pick = k;
    if (pick < 0)
        pick = 0;
    auto& s = alloc[(size_t) pick];
    s.drop = s.active && s.shown;
    s.note = key;
    s.gate = true;
    s.age = ++counter;
    s.active = true;
    s.quiet = 0;
    s.events.push_back ({ pos, true, key, true, false, s.drop });
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
    for (size_t k = 0; k < alloc.size(); ++k)
        if (alloc[k].gate && alloc[k].note == key)
        {
            alloc[k].gate = false;
            alloc[k].events.push_back ({ pos, true, key, false, false, false });
        }
}

void Engine::allNotesOff()
{
    for (int k = 0; k < 128; ++k)
        if (held[(size_t) k])
            noteOff (k);
}

namespace
{
// Upstream's plugin-kit-stereo place.rs (EVEN placement, INNER 0): voice k of n's place, -1..1,
// the first pair at the edges and the rest filling in evenly; an odd last voice in the centre.
double place (int k, int n)
{
    n = std::clamp (n, 2, (int) MAX_VOICES);
    k %= n;
    const int side = n / 2;
    if (k >= 2 * side)
        return 0.0;
    const int i = k / 2, middle = side / 2;
    const int j = i == 0 ? 0 : i == 1 ? middle : i <= middle ? i - 1 : i;
    const double at = 1.0 - 2.0 * j / (double) (n - 1);
    return k % 2 == 0 ? -at : at;
}

// Constant power, whole at the centre; 0 is hard left. As the kit's, through float.
void panGains (double x, double& l, double& r)
{
    const double a = ca72::clamp (x, 0.0, 1.0) * 1.5707963267948966;
    l = (double) (float) (1.4142135623730951 * std::cos (a));
    r = (double) (float) (1.4142135623730951 * std::sin (a));
}
} // namespace

// POLY and UNISON's voices at their places by SPREAD; DOUBLE's pair about the centre, the
// voice (half flat) right and its twin (half sharp) as far left, as far out as the voice's
// place, all of SPREAD in mono (upstream's engine.rs places()).
void Engine::pan (int k, int n, bool many, double spread, bool doubled, Gain& g)
{
    if (doubled)
    {
        const double at = spread * (many ? std::fabs (place (k, n)) : 1.0);
        panGains (0.5 + 0.5 * at, g.l, g.r);
        panGains (0.5 - 0.5 * at, g.tl, g.tr);
        return;
    }
    g.tl = g.tr = 0.0;
    if (many && spread > 0.0)
        panGains (0.5 + 0.5 * spread * place (k, n), g.l, g.r);
    else
        g.l = g.r = 1.0;
}

bool Engine::step (double& v, double target, double coef_)
{
    if (v == target)
        return false;
    v += (target - v) * coef_;
    if (std::fabs (target - v) < 1e-7)
        v = target;
    return true;
}

bool Engine::glide (Panel& smooth_, const Panel& t, double coef_)
{
    Panel next = t;
    bool moving = false;
    auto s = [&] (double& out, double cur, double tgt)
    {
        out = cur;
        moving |= step (out, tgt, coef_);
    };
    s (next.tune, smooth_.tune, t.tune);
    s (next.glide, smooth_.glide, t.glide);
    s (next.mod_mix, smooth_.mod_mix, t.mod_mix);
    for (size_t o = 0; o < 3; ++o)
    {
        s (next.osc[o].volume, smooth_.osc[o].volume, t.osc[o].volume);
        s (next.osc[o].freq, smooth_.osc[o].freq, t.osc[o].freq);
    }
    s (next.noise_volume, smooth_.noise_volume, t.noise_volume);
    s (next.cutoff, smooth_.cutoff, t.cutoff);
    s (next.emphasis, smooth_.emphasis, t.emphasis);
    s (next.contour_amount, smooth_.contour_amount, t.contour_amount);
    for (auto [o, c, tc] : { std::tuple { &next.filter_contour, &smooth_.filter_contour, &t.filter_contour },
                             std::tuple { &next.loudness_contour, &smooth_.loudness_contour, &t.loudness_contour } })
    {
        s (o->attack, c->attack, tc->attack);
        s (o->decay, c->decay, tc->decay);
        s (o->sustain, c->sustain, tc->sustain);
    }
    s (next.pitch_wheel, smooth_.pitch_wheel, t.pitch_wheel);
    s (next.mod_wheel, smooth_.mod_wheel, t.mod_wheel);
    s (next.ext_volume, smooth_.ext_volume, t.ext_volume);
    const bool discrete = ! (next == smooth_) && ! moving;
    smooth_ = next;
    return moving || discrete;
}

void Engine::letGo (Slot& sl)
{
    sl.main.key.lift ([&] (int key, bool on) { sl.main.note (key, on); });
    sl.twin.key.lift ([&] (int key, bool on) { sl.twin.note (key, on); });
    for (int key = 0; key < 128; ++key)
        if (held[(size_t) key] || sl.lift)
        {
            sl.main.note (key, false);
            sl.twin.note (key, false);
        }
    sl.gate = false;
    sl.lift = false;
}

void Engine::allSoundOff()
{
    allNotesOff();
    const bool many = set.poly || set.unison;
    const int inst = instruments();
    for (size_t k = 0; k < alloc.size(); ++k)
        alloc[k].rest = many ? (alloc[k].active || (int) k < inst) : k == 0;
    hush = ! slots.empty();
    hushWaited = 0;
}

// Once All Sound Off has faded out: the voices it marked put back as built, the output let back in.
// A late slot is waited for, silent, up to HUSH_WAIT.
void Engine::settle()
{
    if (! hush || fade > 0.0)
        return;
    const bool poly = set.poly || set.unison;
    bool waiting = false;
    for (size_t k = 0; k < alloc.size(); ++k)
    {
        auto& a = alloc[k];
        if (! a.rest)
            continue;
        if (poly && a.gate)
        {
            a.rest = false;
            continue;
        }
        if (a.late)
        {
            waiting = true;
            continue;
        }
        restore (k);
        a.rest = false;
        if (poly)
            a.active = false;
    }
    hushWaited += blockN;
    if (! waiting || hushWaited >= (int) (HUSH_WAIT * voiceRate / (double) factor))
    {
        hush = false;
        for (auto& a : alloc)
            a.rest = false;
    }
}

void Engine::restore (size_t k)
{
    auto& sl = slots[k];
    for (int t = 0; t < 2; ++t)
    {
        const size_t j = k + (t == 1 ? MAX_VOICES : 0);
        auto& p = t == 0 ? sl.main : sl.twin;
        if (p.light)
        {
            p.light->rest();
            p.light->set_seed (voiceSeed (seed, j));
            if (auto at_ = outOfStep (j, seed))
                p.light->start_oscillators_at (*at_);
        }
        else
        {
            p.voice = proto;
            p.voice->set_seed (voiceSeed (seed, j));
            if (auto at_ = outOfStep (j, seed))
                p.voice->start_oscillators_at (*at_);
        }
        p.key.down.reset();
        p.key.open = p.key.gap;
        p.shownValid = false;
    }
    sl.gate = false;
    sl.drop = false;
    sl.drive = sl.feedback = -1.0;
}

void Engine::begin (int n, const Settings& s)
{
    // A slot a worker had past a deadline is back once that worker lets it go.
    for (size_t k = 0; k < alloc.size(); ++k)
        if (alloc[k].late && workers && workers->finished ((int) k))
            alloc[k].late = false;

    settle();

    // POLY or UNISON switched, or VOICES changed under UNISON: every key let go.
    const bool modeChanged = s.poly != set.poly || s.unison != set.unison || (s.unison && s.voices != set.voices);
    if (! modeChanged && s.poly && ! s.unison && s.voices < set.voices)
    {
        // Fewer POLY voices: the ones left out let go of their notes.
        for (size_t k = (size_t) std::max (2, s.voices); k < alloc.size(); ++k)
            if (alloc[k].gate)
            {
                alloc[k].gate = false;
                alloc[k].events.push_back ({ 0, true, alloc[k].note, false, false, false });
            }
    }
    if (modeChanged)
    {
        for (size_t k = 0; k < slots.size(); ++k)
        {
            if (alloc[k].late)
                slots[k].lift = true;
            else
                letGo (slots[k]);
            alloc[k].gate = false;
        }
        held.fill (false);
    }
    set = s;
    blockN = n;
    segments.clear();
    count = set.poly || set.unison ? voices() : 1;
    polyMode = set.poly && ! set.unison;
    entropy = set.entropy * ENTROPY_DEPTH;
    oscillators = ENTROPY_FLOOR + entropy;
    at = std::fmax (oscillators, entropy);
    trim = set.unison ? 1.0 / std::sqrt ((double) count) : 1.0;
    pairTrim = set.doubled ? std::sqrt (0.5) : 1.0;
    // About 10 ms to settle, sample by sample at the host's rate.
    coef = 1.0 - std::exp (-1.0 / (0.010 * voiceRate / (double) factor));

    for (size_t k = 0; k < slots.size(); ++k)
    {
        // Notes queued for it in earlier blocks start this one.
        for (auto& ev : alloc[k].events)
            ev.pos = 0;
        if (alloc[k].late)
            continue;
        auto& sl = slots[k];
        if (sl.lift)
            letGo (sl);
        if (sl.drive != set.drive || sl.feedback != set.feedback)
        {
            sl.drive = set.drive;
            sl.feedback = set.feedback;
            for (auto* pl : { &sl.main, &sl.twin })
            {
                if (pl->light)
                {
                    pl->light->set_drive (sl.drive);
                    pl->light->feedback = sl.feedback;
                }
                else
                {
                    pl->voice->set_drive (sl.drive);
                    pl->voice->feedback = sl.feedback;
                }
            }
        }
        sl.run = false;
        sl.peak = 0.0;
        if (sl.outL.size() < (size_t) n * factor)
        {
            sl.outL.resize ((size_t) n * factor);
            sl.outR.resize ((size_t) n * factor);
        }
    }
}

// The slot sounds from here on, starting from the engine's control path at this sample.
void Engine::startSlot (size_t k)
{
    auto& sl = slots[k];
    sl.run = true;
    sl.from = pos;
    sl.smooth = smooth;
    sl.smoothSpread = smoothSpread;
    sl.smoothDetune = smoothDetune;
}

void Engine::advance (int until, const Panel& p)
{
    until = std::min (until, blockN);
    if (until <= pos)
        return;
    Panel target = p;
    target.quality = quality;
    if (! primed)
    {
        smooth = target;
        smoothSpread = set.spread;
        smoothDetune = set.detune;
        primed = true;
    }
    // A-440 sounds with no key held, so the first voice keeps running while it is on.
    if (p.a440 && ! slots.empty())
        alloc[0].active = true;
    for (int k = 0; k < count; ++k)
        if (alloc[(size_t) k].active && ! alloc[(size_t) k].late && ! slots[(size_t) k].run)
            startSlot ((size_t) k);
    segments.push_back ({ pos, until, target });

    const int len = until - pos;
    for (int i = 0; i < len; ++i)
    {
        glide (smooth, target, coef);
        step (smoothSpread, set.spread, coef);
        step (smoothDetune, set.detune, coef);
    }
    if (polyMode)
        for (int k = 0; k < count; ++k)
        {
            auto& a = alloc[(size_t) k];
            if (a.late || ! slots[(size_t) k].run)
                continue;
            a.shown = a.gate && ! (a.drop && (size_t) len * factor == 1);
            a.drop = false;
        }
    pos = until;
}

void Engine::renderSlot (size_t k)
{
    auto& sl = slots[k];
    const auto& pl = sl.plan;
    size_t e = 0;
    auto apply = [&] (int upTo)
    {
        for (; e < sl.events.size() && sl.events[e].pos <= upTo; ++e)
        {
            const auto& ev = sl.events[e];
            if (ev.poly)
            {
                sl.gate = ev.on;
                if (ev.on)
                {
                    sl.note = ev.key;
                    sl.drop = ev.drop;
                    sl.peak = 0.0;
                }
            }
            else
            {
                sl.main.note (ev.key, ev.on);
                if (ev.twin)
                    sl.twin.note (ev.key, ev.on);
            }
        }
    };

    for (const auto& seg : pl.segments)
    {
        apply (seg.from);
        if (! sl.run || seg.to <= sl.from)
            continue;
        sl.main.setPanel (sl.smooth, pl.entropy);
        if (pl.doubled)
            sl.twin.setPanel (sl.smooth, pl.entropy);
        Gain g;
        pan ((int) k, pl.places, pl.many, sl.smoothSpread, pl.doubled, g);
        double half = pl.doubled ? 0.5 * sl.smoothDetune * DOUBLE_CENTS : 0.0;

        for (int i = seg.from; i < seg.to; ++i)
        {
            if (glide (sl.smooth, seg.target, pl.coef))
            {
                // The light model works its laws out again on every change: at most every 16 samples.
                if (! isLight || ((i - seg.from) & 15) == 0)
                {
                    sl.main.setPanel (sl.smooth, pl.entropy);
                    if (pl.doubled)
                        sl.twin.setPanel (sl.smooth, pl.entropy);
                }
            }
            if (step (sl.smoothSpread, pl.spread, pl.coef) | step (sl.smoothDetune, pl.detune, pl.coef))
            {
                pan ((int) k, pl.places, pl.many, sl.smoothSpread, pl.doubled, g);
                half = pl.doubled ? 0.5 * sl.smoothDetune * DOUBLE_CENTS : 0.0;
            }
            for (size_t sub = 0; sub < factor; ++sub)
            {
                if (pl.polyMode)
                {
                    const bool gate = sl.gate && ! sl.drop;
                    sl.drop = false;
                    sl.main.key.step (gate, sl.note, [&] (int key, bool on) { sl.main.note (key, on); });
                    if (pl.doubled)
                        sl.twin.key.step (gate, sl.note, [&] (int key, bool on) { sl.twin.note (key, on); });
                    else
                        sl.twin.key.lift ([&] (int key, bool on) { sl.twin.note (key, on); });
                }
                const double a = sl.main.tick (pl.unisonCents - half, pl.oscillators, pl.entropy, pl.at, voiceRate) * pl.pairTrim;
                double b = 0.0;
                if (pl.doubled)
                    b = sl.twin.tick (pl.unisonCents + half, pl.oscillators, pl.entropy, pl.at, voiceRate) * pl.pairTrim;
                const size_t j = (size_t) i * factor + sub;
                sl.outL[j] = a * g.l + b * g.tl;
                sl.outR[j] = a * g.r + b * g.tr;
                sl.peak = std::fmax (sl.peak, std::fmax (std::fabs (a), std::fabs (b)));
            }
        }
    }
    apply (std::numeric_limits<int>::max());
}

void Engine::startThreads (std::function<bool()> joinHost)
{
    // One voice per core: the audio thread plays one, a worker each of the others the cores allow.
    const size_t cores = std::max (1u, std::thread::hardware_concurrency());
    const size_t n = std::min (slots.size() - 1, cores - 1);
    if (! workers && n > 0)
        workers = std::make_unique<Workers> (n, [this] (int k) { renderSlot ((size_t) k); }, std::move (joinHost));
}

void Engine::finish (float* left, float* right, std::optional<Workers::Clock::time_point> deadline)
{
    std::array<int, POLY_VOICES> jobs {};
    int n = 0;
    const int places = std::max (1, set.poly || set.unison ? voices() : 1);
    for (int k = 0; k < (int) slots.size(); ++k)
    {
        auto& a = alloc[(size_t) k];
        auto& sl = slots[(size_t) k];
        if (a.late || ! (sl.run || ! a.events.empty()))
            continue;
        // The slot takes its notes and the block's plan.
        std::swap (sl.events, a.events);
        a.events.clear();
        auto& pl = sl.plan;
        pl.segments.assign (segments.begin(), segments.end());
        pl.coef = coef;
        pl.entropy = entropy;
        pl.oscillators = oscillators;
        pl.at = at;
        pl.pairTrim = pairTrim;
        pl.spread = set.spread;
        pl.detune = set.detune;
        pl.doubled = set.doubled;
        pl.polyMode = polyMode;
        pl.many = set.poly || set.unison;
        // UNISON's voices at EVEN's places for three: left, right, centre.
        pl.unisonCents = set.unison ? UNISON_CENTS * place (k, places) : 0.0;
        pl.places = places;
        jobs[(size_t) n++] = k;
    }
    if (workers && n > 1)
    {
        // A voice not done by the deadline is silent for this block and sits out until its worker is.
        const uint32_t late = workers->run (jobs.data(), n, deadline);
        for (size_t k = 0; k < alloc.size(); ++k)
            if (late & (1u << k))
                alloc[k].late = true;
    }
    else
        for (int i = 0; i < n; ++i)
            renderSlot ((size_t) jobs[(size_t) i]);

    for (int i = 0; i < blockN; ++i)
    {
        double outL = 0.0, outR = 0.0;
        for (size_t sub = 0; sub < factor; ++sub)
        {
            const size_t j = (size_t) i * factor + sub;
            double sumL = 0.0, sumR = 0.0;
            for (int k = 0; k < count; ++k)
            {
                const auto& sl = slots[(size_t) k];
                if (alloc[(size_t) k].late || ! sl.run || i < sl.from)
                    continue;
                sumL += sl.outL[j];
                sumR += sl.outR[j];
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
        if (hush || fade < 1.0)
        {
            fade = hush ? std::fmax (0.0, fade - fadeStep) : std::fmin (1.0, fade + fadeStep);
            outL *= fade;
            outR *= fade;
        }
        left[i] = (float) outL;
        right[i] = (float) outR;
    }
    pos = 0;

    bool anyHeld = false;
    for (bool h : held) anyHeld |= h;
    const bool a440 = ! segments.empty() && segments.back().target.a440;
    for (int k = 0; k < count; ++k)
    {
        auto& a = alloc[(size_t) k];
        const auto& sl = slots[(size_t) k];
        if (! a.active || a.late)
            continue;
        const bool keysUp = polyMode ? (! a.gate && ! sl.main.key.down) : ! anyHeld;
        if (keysUp && sl.peak < SILENT && ! (a440 && k == 0))
        {
            a.quiet += blockN;
            if (a.quiet >= 256)
                a.active = false;
        }
        else
        {
            a.quiet = 0;
        }
    }
}

void Engine::render (float* left, float* right, int n, const Panel& p, const Settings& s)
{
    begin (n, s);
    advance (n, p);
    finish (left, right);
}
} // namespace ca72poly
