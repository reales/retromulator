#pragma once

#include "common.h"
#include "voice_types.h"

#include "a440.h"
#include "contour.h"
#include "expo.h"
#include "keyboard.h"
#include "modulation.h"
#include "noise.h"
#include "preamp.h"
#include "revsaw.h"
#include "tuning.h"
#include "vca.h"
#include "vcf.h"
#include "vco.h"

#include <bit>

namespace ca72::voice
{
// Oscillator and filter oversampling forced for every quality mode (None: the Rust's).
using OversampleOverride = std::optional<std::pair<size_t, size_t>>;

template <typename T>
struct Memo
{
    std::optional<std::pair<uint64_t, T>> v;

    template <typename F>
    T get (double x, F law)
    {
        const uint64_t bits = std::bit_cast<uint64_t> (x);
        if (v.has_value() && v->first == bits)
            return v->second;
        const T r = law (x);
        v = std::pair<uint64_t, T> { bits, r };
        return r;
    }
};

struct Keys
{
    double rate = 0.0;
    std::vector<int32_t> held_;
    std::vector<std::pair<int32_t, uint64_t>> releasing;
    std::array<uint64_t, 128> pressed {};
    uint64_t presses = 0;
    std::vector<int32_t> waiting;
    uint64_t gap = 0;
    bool gate = false;
    std::optional<uint64_t> opened;
    uint64_t samples = 0;
    int32_t foot = 0;

    void note (int32_t midi, bool on, bool retrigger);
    Contacts step (Priority priority);
    void reserve();
    bool held() const;
};

struct KeyboardPart
{
    ca72::keyboard::Keyboard keyboard;
    double rate = 0.0;
    size_t every = 1;
    size_t count = 0;
    double from = 0.0;
    double to = 0.0;
    double step = 0.0;
    double beyond_ = 0.0;

    double tick (const Panel& p, Contacts c);
    double beyond (const Panel& p, Contacts c);
};

struct ControlOut
{
    double env_f = 0.0;
    double env_l = 0.0;
    ca72::noise::NoiseOut noise {};

    static ControlOut default_() { return {}; }
    bool operator== (const ControlOut&) const = default;
};

struct ControlPart
{
    ca72::contour::Contours contours;
    size_t contour_every = 1;
    double rate = 0.0;
    size_t count = 0;
    std::pair<double, double> env_from {};
    std::pair<double, double> env_to {};
    ca72::noise::Noise noise;
    std::array<Memo<double>, 4> pots {};
    ca72::noise::NoiseOut last_noise {};
    ca72::modulation::Modulation mix;
    Memo<double> noise_gain {};

    ControlOut tick (const Panel& p, Contacts c);
};

struct BiasPart
{
    ca72::vca::Vca vca;
    std::pair<double, double> j3_normal {};
    size_t vca_every = 1;

    ca72::vca::VcaDrive drive() const;
    ca72::vca::VcaDrive tick (double env_l, std::optional<double> loudness, Quality q);
};

struct PreampPart
{
    ca72::preamp::Preamp preamp;
    double overload = 0.0;
    double overload_peak = 0.0;
    double ext_amp = 0.0;
    Memo<std::pair<double, double>> volume {};
    bool accurate = false;
    ca72::preamp::InLoop in_loop = ca72::preamp::InLoop::Delayed;

    double tick (const Panel& p, double ext);
    std::pair<double, double> lamp();
};

inline constexpr uint64_t HISS_SEED = 0x6869'7373;

struct FrontPart
{
    double rate = 0.0;
    ca72::noise::Gauss hiss;
    double hiss_rms = 0.0;
    std::array<ca72::tuning::Tuning, 3> tunings;
    std::array<ca72::vco::Vco, 3> vcos;
    ca72::revsaw::RevSaw revsaw;
    ca72::vcf::FilterExpo expo;
    std::shared_ptr<const ca72::vcf::ExpoTable> table;
    ca72::modulation::Modulation modulation;
    double filter_node = 0.0;
    double i0 = 0.0;
    double line = 0.0;
    double last_osc3 = 0.0;
    std::array<double, 3> last_i {};
    Memo<double> wheel {};
    OversampleOverride oversample_override;

    FrontOut tick (const Panel& p, const Jacks& j, double v_kbd, const ControlOut& c, double i_pre);
};

struct BackPart
{
    ca72::vcf::Vcf vcf;
    ca72::vca::Vca vca;
    ca72::a440::A440 a440;
    Memo<double> emphasis {};
    OversampleOverride oversample_override;

    std::pair<double, double> tick (const Panel& p, const FrontOut& f, const ca72::vca::VcaDrive& d);
};

struct AudioPart
{
    PreampPart pre;
    FrontPart front;
    BackPart back;
    std::tuple<double, double, double> last {};

    double overload() const;
    double take_overload_peak();
    double tick (const Panel& p, const Jacks& j, double v_kbd, const ControlOut& c, const ca72::vca::VcaDrive& d);
};

struct Voice
{
    Panel panel;
    Keys keys;
    KeyboardPart kbd;
    ControlPart ctl;
    BiasPart bias;
    AudioPart audio;
    std::optional<Quality> control_quality;
    std::pair<double, double> last_ctl {};
    double feedback = 0.0;
    ca72::preamp::InLoop in_loop = ca72::preamp::InLoop::Delayed;
    double fed_back = 0.0;
    // Set by new_/prototype only (it shapes the resamplers built there).
    OversampleOverride oversample_override;

    static Voice new_ (double rate, Panel panel, OversampleOverride oversample_override = std::nullopt);
    static Voice prototype (double rate, OversampleOverride oversample_override = std::nullopt);

    void set_drive (double gain);
    void start_oscillators_at (double at);
    void set_seed (uint64_t seed);
    // Not upstream: 4-point BLEP and the BLAMP at the reset on all three oscillators.
    void set_oscillator_quality (int blep_points, bool corners);
    double rate() const;
    std::tuple<double, double, bool> envelopes() const;
    std::optional<std::array<double, 3>> slow_state() const;
    bool trigger_closed() const;
    std::tuple<double, double, double> probe() const;
    std::pair<double, std::array<double, 3>> probe_pitch() const;
    std::pair<double, uint64_t> probe_resets (size_t n) const;
    std::pair<double, double> probe_contours() const;
    void hold_control_quality (std::optional<Quality> q);
    std::array<ca72::tuning::Tuning, 3> tunings() const;
    std::tuple<double, double, double> filter_probe() const;
    double noise_density() const;
    const ca72::revsaw::RevSaw& revsaw() const;
    const ca72::keyboard::Keyboard& keyboard() const;
    void note (int32_t midi, bool on);
    double tick();
    const ca72::preamp::Preamp& preamp() const;
    size_t preamp_failed() const;
    double ext_probe() const;
    double overload() const;
    double take_overload_peak();
    std::tuple<Panel, Keys, KeyboardPart, ControlPart, BiasPart, AudioPart> into_parts() const;
    double tick_in (double ext);
    double tick_jacks (const Jacks& j);

private:
    double tick_at (const Jacks& j);
};

ca72::contour::Panel contour_panel (const Panel& p, bool s_trig, std::array<Memo<double>, 4>& pots);

// Not upstream: No Compromises also runs the keyboard circuit every 8 samples, and the voice's
// ticks use the fast elementary functions in every mode (FastScope::new_ (true) in voice.cpp).
inline size_t keyboard_every (Quality q)
{
    switch (q)
    {
        case Quality::HighFidelity: return 1;
        case Quality::NoCompromises:
        case Quality::Potato: return 8;
    }
    return 1;
}

inline std::pair<size_t, size_t> oversampling (Quality q)
{
    switch (q)
    {
        case Quality::NoCompromises:
        case Quality::HighFidelity: return { 4, 4 };
        case Quality::Potato: return { 1, 4 };
    }
    return { 4, 4 };
}

// oversampling(q), or the override when one is set.
inline std::pair<size_t, size_t> oversampling_with (Quality q, const OversampleOverride& o)
{
    return o.has_value() ? *o : oversampling (q);
}

inline size_t contour_every (Quality q)
{
    switch (q)
    {
        case Quality::NoCompromises:
        case Quality::HighFidelity: return 2;
        case Quality::Potato: return 8;
    }
    return 2;
}
} // namespace ca72::voice
