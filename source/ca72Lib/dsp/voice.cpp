#include "voice.h"

#include "filter_cal.h"
#include "ulp.h"

#include <mutex>

namespace ca72::voice
{
namespace
{
    template <typename T, typename E>
    T expect (ca72::Result<T, E> r)
    {
        assert (r.is_ok());
        return std::move (r.value());
    }

    double width (Waveform w)
    {
        switch (w)
        {
            case Waveform::WideRectangle: return -1.5;
            case Waveform::NarrowRectangle: return -2.5;
            default: return 0.0;
        }
    }

    double channel_load (double volume, double r_series)
    {
        const double p = volume_track (volume);
        const double bottom = 25e3 * p;
        return 25e3 * (1.0 - p) + bottom * r_series / (bottom + r_series);
    }

    ca72::noise::NoiseLoads noise_loads (const Panel& p)
    {
        const double audio = channel_load (p.noise_volume, 11e3);
        const double modulation = ca72::modulation::Modulation::source_loads (p.mod_mix).first;
        ca72::noise::NoiseLoads l {};
        if (p.noise_pink)
        {
            l.white = std::numeric_limits<double>::infinity();
            l.pink = 24e3 + audio;
            l.red = modulation;
        }
        else
        {
            l.white = audio;
            l.pink = modulation;
            l.red = std::numeric_limits<double>::infinity();
        }
        return l;
    }

    std::pair<double, double> channel (double v, double r_src, double volume, bool on, double r_series)
    {
        if (! on)
            return { 0.0, 1.0 / r_series };
        const double p = volume_track (volume);
        const double up = r_src + 25e3 * (1.0 - p);
        const double down = std::fmax (25e3 * p, 1e-3);
        const double v_w = v * down / (up + down);
        const double r_w = up * down / (up + down);
        const double g = 1.0 / (r_w + r_series);
        return { v_w * g, g };
    }

    std::pair<double, double> waveform_source (Waveform w, const ca72::vco::VcoOut& o, const ca72::revsaw::RevSawOut* rev)
    {
        const double r_saw = 4300.0 * 4700.0 / 9000.0;
        const std::pair<double, double> saw = rev != nullptr
                                                  ? std::pair<double, double> { rev->saw, r_saw * 33e3 / (r_saw + 33e3) }
                                                  : std::pair<double, double> { o.saw, r_saw };
        const std::pair<double, double> tri { o.tri, 750.0 * 8200.0 / 8950.0 };
        const double on = ca72::clamp (-o.rect / 4.3, 0.0, 1.0);
        const std::pair<double, double> rect { o.rect, 4700.0 + (4700.0 * 6200.0 / 10900.0 - 4700.0) * on };
        switch (w)
        {
            case Waveform::Triangle: return tri;
            case Waveform::Sawtooth: return saw;
            case Waveform::SharkTooth:
            case Waveform::ReverseSawtooth:
            {
                if (rev != nullptr)
                    return { rev->rev, rev->r_rev };
                const double ga = 1.0 / (saw.second + 47e3);
                const double gb = 1.0 / (tri.second + 10e3);
                return { (saw.first * ga + tri.first * gb) / (ga + gb), 1.0 / (ga + gb) };
            }
            default: return rect;
        }
    }

    double calibrate_noise (ca72::vco::Vco& vco, double rate, const ca72::tuning::Tuning& tuning, double key)
    {
        const double vol = NOISE_CALIBRATION_VOLUME;
        const auto d = ca72::tuning::osc_drive_with (ca72::tuning::Osc::One(), tuning, key, ca72::tuning::Buses::RESTING,
                                                     ca72::tuning::Range::R2);
        const double i_in = d.apply (vco.expo);
        vco.reset();
        const size_t n = (size_t) (200.0 * rate / 440.0);
        std::vector<double> samples;
        samples.reserve (n);
        for (size_t k = 0; k < n + 1000; ++k)
        {
            const auto o = vco.tick (i_in, 0.0);
            if (k >= 1000)
            {
                const auto [v, r] = waveform_source (Waveform::Triangle, o, nullptr);
                samples.push_back (channel (v, r, vol, true, 33e3).first);
            }
        }
        double sum = 0.0;
        for (const double s : samples)
            sum += s;
        const double mean = sum / (double) samples.size();
        double var = 0.0;
        for (const double s : samples)
            var += (s - mean) * (s - mean);
        const double i_tri = std::sqrt (var / (double) samples.size());

        ca72::noise::NoiseLoads loads {};
        loads.white = channel_load (vol, 11e3);
        loads.pink = ca72::modulation::Modulation::source_loads (0.0).first;
        loads.red = std::numeric_limits<double>::infinity();
        auto made = ca72::noise::Noise::new_ (ca72::noise::NoiseCircuit::default_(), loads, rate, 0);
        if (made.is_err())
            return 0.0;
        const auto& nz = made.value();
        const auto [model, sys, out] = nz.model_of();
        const auto [f0, f1] = NOISE_CALIBRATION_BAND;
        const size_t points = 400;
        double power = 0.0;
        std::optional<std::pair<double, double>> prev;
        for (size_t k = 0; k <= points; ++k)
        {
            const double f = f0 * ca72::ulp::pow (f1 / f0, (double) k / (double) points);
            const auto [z0, z1] = model.response (out[0], f);
            const double pw = z0 * z0 + z1 * z1;
            if (prev.has_value())
            {
                const auto [fp, pp] = *prev;
                power += 0.5 * (pw + pp) * (f - fp);
            }
            prev = std::pair<double, double> { f, pw };
        }
        const double i_unit = channel (std::sqrt (power), 0.0, vol, true, 11e3).first;
        return i_tri * ca72::ulp::pow (10.0, -NOISE_BELOW_TRIANGLE / 20.0) / i_unit;
    }

    ca72::keyboard::Load keyboard_load (const Panel& p)
    {
        return ca72::keyboard::Load::of (3, p.keyboard_control_1, p.keyboard_control_2);
    }

    std::pair<double, double> ext_volume (double volume)
    {
        const double t = ext_taper (volume);
        return { t, 1e6 * t * (1.0 - t) };
    }

    std::vector<int32_t> with_room()
    {
        std::vector<int32_t> v;
        v.reserve (128);
        return v;
    }

    bool contains (const std::vector<int32_t>& v, int32_t x)
    {
        return std::find (v.begin(), v.end(), x) != v.end();
    }
} // namespace

Voice Voice::new_ (double rate, Panel panel, OversampleOverride oversample_override)
{
    using namespace ca72::tuning;

    auto keyboard = ca72::keyboard::Keyboard::new_ (ca72::keyboard::KeyboardCircuit::default_(), rate);
    keyboard.set_load (keyboard_load (panel));
    std::vector<std::pair<uint32_t, double>> played;
    for (const uint32_t k : { LOW_A, SECOND_A, HIGH_A })
    {
        auto r = keyboard.static_out ((size_t) k);
        const double v = r.is_ok() ? r.value() : (double) k * KEY_STEP;
        played.push_back ({ k, v });
    }
    const auto volts = [&played] (uint32_t k) -> double
    {
        for (const auto& [key, v] : played)
            if (key == k)
                return v;
        return (double) k * KEY_STEP;
    };
    const size_t os = oversampling (Quality::NoCompromises).first;
    std::array<ca72::vco::Vco, 3> vcos { ca72::vco::Vco::new_ (rate, os), ca72::vco::Vco::new_ (rate, os),
                                         ca72::vco::Vco::new_ (rate, os) };
    Buses buses = Buses::RESTING;
    buses.ext = OPEN_BUS;
    const auto [t1, n1] = folkman_1973_buses (vcos[0], rate, Tuning::default_(), volts, buses);
    const Osc two = Osc::Two (FREQ_CENTRE);
    const Osc three = Osc::Three (FREQ_CENTRE, true);
    const auto [t2, n2] = folkman_1973_osc_buses (vcos[1], rate, t1, two, volts, buses);
    const auto [t3, n3] = folkman_1973_osc_buses (vcos[2], rate, t1, three, volts, buses);
    const std::array<Tuning, 3> tunings { t1, t2, t3 };
    for (size_t i = 0; i < 3; ++i)
    {
        vcos[i].expo.r11 = tunings[i].r11;
        vcos[i].expo.a8 = tunings[i].a8;
    }
    auto revsaw = expect (ca72::revsaw::RevSaw::new_ (ca72::revsaw::RevSawCircuit::default_(), rate));
    auto probe_vco = vcos[0];
    const double density = calibrate_noise (probe_vco, rate, t1, volts (LOW_A));
    // The calibration above ran at the Rust's factor; an override then switches each oscillator to its own.
    for (auto& v : vcos)
    {
        if (oversample_override.has_value())
        {
            v.prepare (oversample_override->first);
            v.set_oversample (oversample_override->first);
        }
        else
        {
            v.prepare (oversampling (Quality::Potato).first);
        }
    }
    auto noise = expect (ca72::noise::Noise::new_ (ca72::noise::NoiseCircuit::default_(), noise_loads (panel), rate, 0x4D6F'6F67));
    noise.set_density (density);
    auto modulation = expect (ca72::modulation::Modulation::new_());
    const size_t ce = contour_every (Quality::NoCompromises);
    auto vca = ca72::vca::Vca::new_ (ca72::vca::VcaCircuit::default_().reference_trims(), rate, 0.0);
    vca.prepare_potato();
    ControlPart ctl {
        .contours = ca72::contour::Contours::new_ (ca72::contour::ContourCircuit::default_(), rate / (double) ce),
        .contour_every = ce,
        .rate = rate,
        .count = 0,
        .env_from = { 0.0, 0.0 },
        .env_to = { 0.0, 0.0 },
        .noise = noise,
        .pots = {},
        .last_noise = ca72::noise::NoiseOut::default_(),
        .mix = modulation,
        .noise_gain = {},
    };
    BiasPart bias {
        .vca = vca,
        .j3_normal = { vca.circuit.ext, vca.circuit.r_j3 },
        .vca_every = ca72::vca::Vca::new_ (ca72::vca::VcaCircuit::default_(), rate, 0.0).control_every,
    };
    const auto cp = contour_panel (panel, false, ctl.pots);
    ctl.contours.settle (cp);
    const auto o = ctl.contours.tick (false, cp);
    ctl.env_from = { o.filter, o.loudness };
    ctl.env_to = ctl.env_from;

    const double kbd_out = keyboard.out();
    const double kbd_step = (volts (HIGH_A) - volts (LOW_A)) / (double) (HIGH_A - LOW_A);

    auto vcf = ca72::vcf::Vcf::new_ (rate, oversample_override.has_value() ? oversample_override->second
                                                                           : oversampling (Quality::NoCompromises).second);
    vcf.circuit = ca72::filter_cal::CALIBRATED.circuit();
    vcf.prepare (oversample_override.has_value() ? oversample_override->second : oversampling (Quality::Potato).second);

    auto preamp = expect (ca72::preamp::Preamp::new_ (ca72::preamp::PreampCircuit::default_(), rate, ext_volume (panel.ext_volume).second));

    return Voice {
        .panel = panel,
        .keys = Keys {
            .rate = rate,
            .held_ = with_room(),
            .releasing = [] { std::vector<std::pair<int32_t, uint64_t>> v; v.reserve (128); return v; }(),
            .pressed = {},
            .presses = 0,
            .waiting = with_room(),
            .gap = (uint64_t) std::round (RETRIGGER_GAP * rate),
            .gate = false,
            .opened = std::nullopt,
            .samples = 0,
            .foot = 0,
        },
        .kbd = KeyboardPart {
            .keyboard = std::move (keyboard),
            .rate = rate,
            .every = 1,
            .count = 0,
            .from = kbd_out,
            .to = kbd_out,
            .step = kbd_step,
            .beyond_ = 0.0,
        },
        .ctl = std::move (ctl),
        .bias = std::move (bias),
        .audio = AudioPart {
            .pre = PreampPart {
                .preamp = std::move (preamp),
                .overload = 0.0,
                .overload_peak = 0.0,
                .ext_amp = 0.0,
                .volume = {},
                .accurate = false,
                .in_loop = ca72::preamp::InLoop::Delayed,
            },
            .front = FrontPart {
                .rate = rate,
                .hiss = ca72::noise::Gauss::new_ (HISS_SEED),
                .hiss_rms = MIXER_HISS * std::sqrt (rate / 48e3),
                .tunings = tunings,
                .vcos = std::move (vcos),
                .revsaw = std::move (revsaw),
                .expo = ca72::filter_cal::CALIBRATED.expo(),
                .table = ca72::vcf::ExpoTable::shared (ca72::filter_cal::CALIBRATED.expo(), 25.0),
                .modulation = std::move (modulation),
                .filter_node = 0.0,
                .i0 = 0.0,
                .line = 0.0,
                .last_osc3 = 0.0,
                .last_i = {},
                .wheel = {},
                .oversample_override = oversample_override,
            },
            .back = BackPart {
                .vcf = std::move (vcf),
                .vca = std::move (vca),
                .a440 = ca72::a440::A440::new_ (rate),
                .emphasis = {},
                .oversample_override = oversample_override,
            },
            .last = { 0.0, 0.0, 0.0 },
        },
        .control_quality = std::nullopt,
        .last_ctl = { 0.0, 0.0 },
        .feedback = 0.0,
        .in_loop = ca72::preamp::InLoop::Delayed,
        .fed_back = 0.0,
        .oversample_override = oversample_override,
    };
}

Voice Voice::prototype (double rate, OversampleOverride oversample_override)
{
    static std::mutex lock;
    static std::vector<std::tuple<uint64_t, OversampleOverride, Voice>> built;
    const uint64_t key = std::bit_cast<uint64_t> (rate);
    std::optional<Voice> cached;
    {
        std::lock_guard<std::mutex> g (lock);
        for (const auto& [k, o, v] : built)
            if (k == key && o == oversample_override)
            {
                cached = v;
                break;
            }
    }
    Voice v = cached.has_value() ? std::move (*cached) : [&]
    {
        Voice made = Voice::new_ (rate, Panel::default_(), oversample_override);
        std::lock_guard<std::mutex> g (lock);
        built.emplace_back (key, oversample_override, made);
        return made;
    }();
    v.keys.reserve();
    return v;
}

void Voice::set_drive (double gain) { audio.back.vcf.set_drive (gain); }

void Voice::start_oscillators_at (double at)
{
    for (auto& v : audio.front.vcos)
        v.start_at (at);
}

void Voice::set_seed (uint64_t seed)
{
    ctl.noise.reseed (seed);
    audio.front.hiss = ca72::noise::Gauss::new_ (seed ^ HISS_SEED);
}

void Voice::set_oscillator_quality (int blep_points, bool corners)
{
    for (auto& v : audio.front.vcos)
        v.set_quality (blep_points, corners);
}

double Voice::rate() const { return audio.front.rate; }

std::tuple<double, double, bool> Voice::envelopes() const
{
    return { ctl.env_to.first, ctl.env_to.second, keys.held() };
}

std::optional<std::array<double, 3>> Voice::slow_state() const
{
    if (keys.held() || ! keys.releasing.empty() || keys.gate)
        return std::nullopt;
    return std::array<double, 3> { kbd.to, ctl.env_to.first, ctl.env_to.second };
}

bool Voice::trigger_closed() const { return keys.gate; }

std::tuple<double, double, double> Voice::probe() const { return audio.last; }

std::pair<double, std::array<double, 3>> Voice::probe_pitch() const
{
    return { std::get<0> (audio.last), audio.front.last_i };
}

std::pair<double, uint64_t> Voice::probe_resets (size_t n) const { return audio.front.vcos[n].last_reset(); }

std::pair<double, double> Voice::probe_contours() const { return last_ctl; }

void Voice::hold_control_quality (std::optional<Quality> q) { control_quality = q; }

std::array<ca72::tuning::Tuning, 3> Voice::tunings() const { return audio.front.tunings; }

std::tuple<double, double, double> Voice::filter_probe() const
{
    return { audio.front.i0, audio.front.filter_node, audio.front.line };
}

double Voice::noise_density() const { return ctl.noise.density(); }

const ca72::revsaw::RevSaw& Voice::revsaw() const { return audio.front.revsaw; }

const ca72::keyboard::Keyboard& Voice::keyboard() const { return kbd.keyboard; }

void Voice::note (int32_t midi, bool on) { keys.note (midi, on, panel.retrigger); }

double Voice::tick() { return tick_in (0.0); }

const ca72::preamp::Preamp& Voice::preamp() const { return audio.pre.preamp; }

size_t Voice::preamp_failed() const { return audio.pre.preamp.failed; }

double Voice::ext_probe() const { return audio.pre.ext_amp; }

double Voice::overload() const { return audio.pre.overload; }

double Voice::take_overload_peak() { return audio.take_overload_peak(); }

std::tuple<Panel, Keys, KeyboardPart, ControlPart, BiasPart, AudioPart> Voice::into_parts() const
{
    return { panel, keys, kbd, ctl, bias, audio };
}

double Voice::tick_in (double ext)
{
    Jacks j = Jacks::default_();
    j.ext = ext;
    return tick_jacks (j);
}

double Voice::tick_jacks (const Jacks& j)
{
    audio.pre.accurate = feedback > 0.0;
    audio.pre.in_loop = in_loop;
    double y;
    if (feedback > 0.0)
    {
        Jacks fed = j;
        fed.ext = j.ext + feedback * fed_back;
        y = tick_at (fed);
    }
    else
    {
        y = tick_at (j);
    }
    fed_back = y;
    return y;
}

double Voice::tick_at (const Jacks& j)
{
    const Panel p = panel;
    Contacts c = keys.step (p.priority);
    c.s_trig = j.s_trig;
    Panel pc = p;
    if (control_quality.has_value())
        pc.quality = *control_quality;
    const double v_kbd = kbd.tick (pc, c);
    const ControlOut co = ctl.tick (pc, c);
    last_ctl = { co.env_f, co.env_l };
    const auto d = bias.tick (co.env_l, j.loudness, p.quality);
    return audio.tick (p, j, v_kbd, co, d);
}

ca72::contour::Panel contour_panel (const Panel& p, bool s_trig, std::array<Memo<double>, 4>& pots)
{
    using Law = std::span<const std::pair<double, double>>;
    auto k = [] (ContourKnobs c, Memo<double>& a, Memo<double>& d, Law la, Law ld)
    {
        ca72::contour::Controls ctl {};
        ctl.attack = a.get (c.attack, [la] (double x) { return time_pot (x, la); });
        ctl.decay = d.get (c.decay, [ld] (double x) { return time_pot (x, ld); });
        ctl.sustain = sustain_track (c.sustain);
        return ctl;
    };
    ca72::contour::Panel cp {};
    cp.filter = k (p.filter_contour, pots[0], pots[1], FILTER_ATTACK, FILTER_DECAY);
    cp.loudness = k (p.loudness_contour, pots[2], pots[3], LOUDNESS_ATTACK, LOUDNESS_DECAY);
    cp.decay_on = p.decay;
    cp.s_trig = s_trig;
    return cp;
}

void Keys::note (int32_t midi, bool on, bool retrigger)
{
    if (midi < MIDI_NOTES.first || midi > MIDI_NOTES.second)
        return;
    const int32_t k = midi - LOWEST_KEY_MIDI;
    if (on)
    {
        if (! contains (held_, k) && ! contains (waiting, k))
        {
            if (retrigger)
                waiting.push_back (k);
            else
                held_.push_back (k);
            presses += 1;
            pressed[(size_t) midi] = presses;
        }
        std::erase_if (releasing, [k] (const std::pair<int32_t, uint64_t>& e) { return e.first == k; });
    }
    else if (contains (waiting, k))
    {
        std::erase (waiting, k);
    }
    else if (contains (held_, k))
    {
        std::erase (held_, k);
        const uint64_t until = samples + (uint64_t) std::round (RELEASE_LEAD * rate);
        releasing.push_back ({ k, until });
    }
}

Contacts Keys::step (Priority priority)
{
    const uint64_t now = samples;
    std::erase_if (releasing, [now] (const std::pair<int32_t, uint64_t>& e) { return ! (e.second > now); });
    if (! waiting.empty() && ! gate && (! opened.has_value() || now - *opened >= gap))
    {
        held_.insert (held_.end(), waiting.begin(), waiting.end());
        waiting.clear();
    }
    const bool g = ! held_.empty() && waiting.empty();
    if (gate && ! g)
        opened = now;
    gate = g;

    // keys(): held, then releasing. Rust's max/max_by_key keep the last of equals, min the first.
    std::optional<int32_t> all_min, all_max, on_min, on_max, last;
    auto visit = [&] (int32_t k)
    {
        if (! all_min.has_value() || k < *all_min)
            all_min = k;
        if (! all_max.has_value() || k >= *all_max)
            all_max = k;
        if (k >= 0 && k < KEYS)
        {
            if (! on_min.has_value() || k < *on_min)
                on_min = k;
            if (! on_max.has_value() || k >= *on_max)
                on_max = k;
        }
        if (! last.has_value() || pressed[(size_t) (k + LOWEST_KEY_MIDI)] >= pressed[(size_t) (*last + LOWEST_KEY_MIDI)])
            last = k;
    };
    for (const int32_t k : held_)
        visit (k);
    for (const auto& [k, until] : releasing)
        visit (k);

    const int32_t top = KEYS - 1;
    auto alone = [top] (int32_t k)
    {
        const int32_t end = std::clamp (k, 0, top);
        return std::pair<std::optional<std::pair<size_t, size_t>>, int32_t> { std::pair<size_t, size_t> { (size_t) end, (size_t) end },
                                                                               k - end };
    };
    std::optional<std::optional<int32_t>> chosen;
    switch (priority)
    {
        case Priority::Low: break;
        case Priority::High: chosen = all_max; break;
        case Priority::Last: chosen = last; break;
    }
    std::pair<std::optional<std::pair<size_t, size_t>>, int32_t> r;
    if (chosen.has_value() && chosen->has_value())
        r = alone (**chosen);
    else if (chosen.has_value() || ! all_min.has_value())
        r = { std::nullopt, foot };
    else if (*all_min < 0)
        r = { std::pair<size_t, size_t> { 0, 0 }, *all_min };
    else if (on_min.has_value() && on_max.has_value())
        r = { std::pair<size_t, size_t> { (size_t) *on_min, (size_t) *on_max }, 0 };
    else
        r = alone (*all_min);
    foot = r.second;
    Contacts c;
    c.keys = r.first;
    c.gate = g;
    c.s_trig = false;
    c.foot = r.second;
    samples += 1;
    return c;
}

void Keys::reserve()
{
    held_.reserve (held_.size() + 128);
    releasing.reserve (releasing.size() + 128);
    waiting.reserve (waiting.size() + 128);
}

bool Keys::held() const { return ! held_.empty() || ! waiting.empty(); }

double KeyboardPart::tick (const Panel& p, Contacts c)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    if (count % every == 0)
    {
        const size_t e = keyboard_every (p.quality);
        if (e != every)
        {
            every = e;
            count = 0;
            keyboard.set_rate (rate / (double) e);
        }
        keyboard.set_quality (p.quality);
        keyboard.set_glide (glide_r (p.glide, p.glide_on));
        keyboard.set_load (keyboard_load (p));
        from = to;
        to = keyboard.tick (c.keys, c.gate);
        if (c.foot != 0 || beyond_ != 0.0)
            to += beyond (p, c);
    }
    count += 1;
    if (every == 1)
        return to;
    const double a = (double) ((count - 1) % every + 1) / (double) every;
    return from + (to - from) * a;
}

double KeyboardPart::beyond (const Panel& p, Contacts c)
{
    const double target = step * (double) c.foot;
    if (c.gate)
    {
        const auto& k = keyboard.circuit;
        const double tau = (k.r61 + glide_r (p.glide, p.glide_on)) * k.c6;
        const double h = (double) every / rate;
        const double a = ca72::ulp::exp (-h / tau);
        beyond_ = target + (beyond_ - target) * a;
        if (std::fabs (beyond_ - target) < 1e-12)
            beyond_ = target;
    }
    return beyond_;
}

ControlOut ControlPart::tick (const Panel& p, Contacts c)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    if (count % contour_every == 0)
    {
        const size_t e = ca72::voice::contour_every (p.quality);
        if (e != contour_every)
        {
            contour_every = e;
            count = 0;
            contours.set_step ((double) e / rate);
        }
        env_from = env_to;
        const auto cp = contour_panel (p, c.s_trig, pots);
        contours.set_quality (p.quality);
        const auto o = contours.tick (c.gate, cp);
        env_to = { o.filter, o.loudness };
    }
    count += 1;
    const double a = (double) ((count - 1) % contour_every + 1) / (double) contour_every;
    const double env_f = env_from.first + (env_to.first - env_from.first) * a;
    const double env_l = env_from.second + (env_to.second - env_from.second) * a;
    const bool heard = p.noise_on
                       || ((p.osc_mod || p.filter_mod)
                           && noise_gain.get (p.mod_mix, [this] (double m) { return mix.mix (m).gain_noise; }) != 0.0);
    ca72::noise::NoiseOut nz;
    if (heard)
    {
        noise.set_quality (p.quality);
        noise.set_loads (noise_loads (p));
        last_noise = noise.tick();
        nz = last_noise;
    }
    else
    {
        nz = last_noise;
    }
    ControlOut out;
    out.env_f = env_f;
    out.env_l = env_l;
    out.noise = nz;
    return out;
}

ca72::vca::VcaDrive BiasPart::drive() const { return vca.drive(); }

ca72::vca::VcaDrive BiasPart::tick (double env_l, std::optional<double> loudness, Quality q)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    const auto [ext_l, r_j3] = loudness.has_value() ? std::pair<double, double> { *loudness, 0.0 } : j3_normal;
    if (vca.circuit.ext != ext_l || vca.circuit.r_j3 != r_j3)
    {
        vca.circuit.ext = ext_l;
        vca.circuit.r_j3 = r_j3;
    }
    vca.potato_on = q == Quality::Potato;
    vca.control_every = (loudness.has_value() && q != Quality::Potato) ? 1 : vca_every;
    return vca.control (env_l);
}

double AudioPart::overload() const { return pre.overload; }

double AudioPart::take_overload_peak() { return std::exchange (pre.overload_peak, 0.0); }

double AudioPart::tick (const Panel& p, const Jacks& j, double v_kbd, const ControlOut& c, const ca72::vca::VcaDrive& d)
{
    const double i_pre = pre.tick (p, j.ext);
    const FrontOut f = front.tick (p, j, v_kbd, c, i_pre);
    const auto [y, out] = back.tick (p, f, d);
    last = { v_kbd, f.i_bus, y };
    return out;
}

double PreampPart::tick (const Panel& p, double ext)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    preamp.set_quality ((accurate && p.quality == Quality::Potato && in_loop == ca72::preamp::InLoop::Circuit)
                            ? Quality::HighFidelity
                            : p.quality);
    preamp.set_delayed (accurate && p.quality == Quality::Potato && in_loop == ca72::preamp::InLoop::Delayed);
    double i;
    if (p.ext_on)
    {
        const auto [t, r] = volume.get (p.ext_volume, [] (double v) { return ext_volume (v); });
        preamp.set_source_resistance (r);
        const auto o = preamp.tick (ext * INPUT_VOLTS * t);
        overload = o.lamp;
        overload_peak = std::fmax (overload_peak, o.lamp);
        ext_amp = o.amp;
        i = o.i_bus;
    }
    else
    {
        overload = 0.0;
        i = 0.0;
    }
    return i;
}

std::pair<double, double> PreampPart::lamp()
{
    return { overload, std::exchange (overload_peak, 0.0) };
}

FrontOut FrontPart::tick (const Panel& p_, const Jacks& j, double v_kbd, const ControlOut& c, double i_pre)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    const Panel p = p_;
    const double env_f = c.env_f;
    const auto nz = c.noise;
    const auto mix = modulation.mix (p.mod_mix);
    double noise_to_mod;
    std::pair<double, double> noise_to_mixer;
    if (p.noise_pink)
    {
        noise_to_mod = nz.red;
        noise_to_mixer = { nz.pink, 24e3 };
    }
    else
    {
        noise_to_mod = nz.pink;
        noise_to_mixer = { nz.white, 0.0 };
    }
    const double amp = mix.offset + mix.gain_noise * noise_to_mod + mix.gain_osc3 * last_osc3;
    ca72::modulation::LineLoads line_loads {};
    line_loads.osc_mod = p.osc_mod;
    line_loads.filter_mod = p.filter_mod;
    line_loads.filter_node = filter_node;
    const double r_wheel = wheel.get (p.mod_wheel, [] (double w) { return ca72::modulation::mod_wheel_r (w); });
    const double line_v = ca72::modulation::Modulation::line (amp, mix.r_out, r_wheel, line_loads);
    line = line_v;
    ca72::tuning::Buses buses {};
    buses.bend = ca72::modulation::pitch_wheel_volts (p.pitch_wheel);
    buses.modulation = p.osc_mod ? line_v : 0.0;
    buses.ext = j.osc.value_or (ca72::tuning::OPEN_BUS);

    double i_bus = hiss_rms * hiss.next_uniform();
    if (p.ext_on)
        i_bus += i_pre;
    double g_bus = 1.0 / 33e3;
    const double mod_load_b = ca72::modulation::Modulation::source_loads (p.mod_mix).second;
    double osc3_node = 0.0;
    for (size_t n = 0; n < 3; ++n)
    {
        const OscPanel op = p.osc[n];
        if (! (op.on || (n == 2 && (p.osc_mod || p.filter_mod))))
        {
            g_bus += 1.0 / 33e3;
            continue;
        }
        const ca72::tuning::Osc osc = n == 0   ? ca72::tuning::Osc::One()
                                      : n == 1 ? ca72::tuning::Osc::Two (op.freq)
                                               : ca72::tuning::Osc::Three (op.freq, p.osc3_control);
        ca72::tuning::Tuning tuning = tunings[n];
        tuning.tune += ca72::clamp (p.tune, -1.0, 1.0) * TUNE_HALF_SPAN;
        auto d = ca72::tuning::osc_drive_with (osc, tuning, v_kbd, buses, op.range);
        if (j.detune[n] != 0.0)
            d.inputs[5].v += j.detune[n] / 1200.0 * tuning.octave_step;
        const double i_in = d.apply (vcos[n].expo);
        vcos[n].set_oversample (oversampling_with (p.quality, oversample_override).first);
        vcos[n].table_on = p.quality == Quality::Potato;
        const auto o = vcos[n].tick (i_in, width (op.waveform));
        last_i[n] = vcos[n].timing_current();
        const bool second = op.waveform == Waveform::Sawtooth || op.waveform == Waveform::SharkTooth
                            || op.waveform == Waveform::ReverseSawtooth;
        std::optional<ca72::revsaw::RevSawOut> rev;
        if (n == 2 && second)
        {
            revsaw.set_quality (p.quality);
            rev = revsaw.tick (o.saw);
        }
        const auto [v_src, r_src] = waveform_source (op.waveform, o, rev.has_value() ? &*rev : nullptr);
        std::pair<double, double> ig;
        if (n == 2)
        {
            const double z_ch = channel_load (op.volume, 33e3);
            const double z = z_ch * mod_load_b / (z_ch + mod_load_b);
            osc3_node = v_src * z / (r_src + z);
            ig = channel (osc3_node, 0.0, op.volume, op.on, 33e3);
        }
        else
        {
            ig = channel (v_src, r_src, op.volume, op.on, 33e3);
        }
        i_bus += ig.first;
        g_bus += ig.second;
    }
    last_osc3 = osc3_node;
    {
        const auto [i, g] = channel (noise_to_mixer.first, noise_to_mixer.second, p.noise_volume, p.noise_on, 11e3);
        i_bus += i;
        g_bus += g;
    }
    const double pos = cutoff_track (p.cutoff);
    const double r_cut = 200e3 + 5e3 * pos * (1.0 - pos);
    const double amt = ca72::clamp (p.contour_amount, 0.0, 1.0);
    const ca72::expo::Input open { std::numeric_limits<double>::infinity(), 0.0 };
    const std::array<ca72::expo::Input, 6> ins {
        ca72::expo::Input { r_cut, -10.0 + 20.0 * pos },
        contour_input (amt, env_f),
        p.keyboard_control_1 ? ca72::expo::Input { ca72::vcf::R53, v_kbd } : open,
        p.keyboard_control_2 ? ca72::expo::Input { ca72::vcf::R54, v_kbd } : open,
        ca72::expo::Input { 33e3, p.filter_mod ? line_v : 0.0 },
        ca72::expo::Input { 100e3, j.cutoff == 0.0 ? j.filter.value_or (0.0)
                                                   : j.filter.value_or (0.0) + j.cutoff * FILTER_VOLTS_PER_OCTAVE },
    };
    const auto [i_node, g_node] = expo.node (ins);
    const auto [i0_now, v_node] = table->current_and_node (i_node, g_node);
    i0 = i0_now;
    filter_node = v_node;
    FrontOut out;
    out.i_bus = i_bus;
    out.g_bus = g_bus;
    out.i0 = i0_now;
    return out;
}

std::pair<double, double> BackPart::tick (const Panel& p, const FrontOut& f, const ca72::vca::VcaDrive& d)
{
    auto fast = ca72::ulp::FastScope::new_ (true);
    vcf.circuit.r14 = emphasis.get (p.emphasis, [] (double e) { return emphasis_r14 (e); });
    vcf.set_oversample (oversampling_with (p.quality, oversample_override).second);
    vcf.set_quality (p.quality);
    vcf.high_pass = p.filter_hi;
    const double y = vcf.tick (f.i_bus, f.g_bus, f.i0);
    vca.a440 = a440.tick (p.a440);
    vca.plain_signal = p.quality != Quality::NoCompromises;
    const double out = vca.signal (y, d);
    return { y, out / 5.0 };
}
} // namespace ca72::voice
