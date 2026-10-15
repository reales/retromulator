#pragma once

#include "common.h"
#include "expo.h"
#include "tuning.h"

namespace ca72::voice
{
enum class Waveform
{
    Triangle,
    SharkTooth,
    ReverseSawtooth,
    Sawtooth,
    Square,
    WideRectangle,
    NarrowRectangle,
};

struct ContourKnobs
{
    double attack = 0.0;
    double decay = 0.0;
    double sustain = 0.0;

    bool operator== (const ContourKnobs&) const = default;
};

struct OscPanel
{
    ca72::tuning::Range range;
    Waveform waveform = Waveform::Triangle;
    bool on = false;
    double volume = 0.0;
    double freq = 0.0;

    bool operator== (const OscPanel&) const = default;
};

// Rust #[default]: NoCompromises.
enum class Quality
{
    NoCompromises,
    HighFidelity,
    Potato,
};

// Rust #[default]: Low.
enum class Priority
{
    Low,
    Last,
    High,
};

struct Panel
{
    std::array<OscPanel, 3> osc {};
    bool osc3_control = false;
    double cutoff = 0.0;
    double emphasis = 0.0;
    double contour_amount = 0.0;
    bool keyboard_control_1 = false;
    bool keyboard_control_2 = false;
    ContourKnobs filter_contour {};
    ContourKnobs loudness_contour {};
    double glide = 0.0;
    bool glide_on = false;
    bool decay = false;
    bool noise_on = false;
    double noise_volume = 0.0;
    bool noise_pink = false;
    double mod_mix = 0.0;
    bool osc_mod = false;
    bool filter_mod = false;
    bool filter_hi = false;
    double pitch_wheel = 0.0;
    double mod_wheel = 0.0;
    double ext_volume = 0.0;
    bool ext_on = false;
    bool a440 = false;
    double tune = 0.0;
    Quality quality = Quality::NoCompromises;
    Priority priority = Priority::Low;
    bool retrigger = false;

    static Panel default_();
    bool operator== (const Panel&) const = default;
};

inline constexpr double TUNE_HALF_SPAN = 0.5 * (10.0 - 10.0 * 5.1 / 10.1);

double volume_track (double volume);
double contour_amount_track (double amount);
double sustain_track (double sustain);
ca72::expo::Input contour_input (double amount, double env_f);
double cutoff_track (double cutoff);

inline constexpr double NOISE_BELOW_TRIANGLE = 6.0;
inline constexpr double NOISE_CALIBRATION_VOLUME = 1.0;
inline constexpr std::pair<double, double> NOISE_CALIBRATION_BAND { 20.0, 20e3 };

double audio_taper (double p);
double time_pot_drawn (double p);

inline constexpr std::array<std::pair<double, double>, 8> FILTER_ATTACK { {
    { 0.15, 10.49e3 },
    { 0.2, 13.56e3 },
    { 0.4, 27.64e3 },
    { 0.5, 36.02e3 },
    { 0.6, 46.90e3 },
    { 0.7, 225.9e3 },
    { 0.85, 669.0e3 },
    { 1.0, 946.8e3 },
} };
inline constexpr std::array<std::pair<double, double>, 8> FILTER_DECAY { {
    { 0.15, 10.47e3 },
    { 0.2, 15.94e3 },
    { 0.4, 33.12e3 },
    { 0.5, 43.46e3 },
    { 0.6, 52.19e3 },
    { 0.7, 236.8e3 },
    { 0.85, 581.7e3 },
    { 1.0, 768.2e3 },
} };
inline constexpr std::array<std::pair<double, double>, 8> LOUDNESS_ATTACK { {
    { 0.15, 10.45e3 },
    { 0.2, 14.13e3 },
    { 0.4, 30.97e3 },
    { 0.5, 41.85e3 },
    { 0.6, 47.29e3 },
    { 0.7, 267.0e3 },
    { 0.85, 703.8e3 },
    { 1.0, 982.6e3 },
} };
inline constexpr std::array<std::pair<double, double>, 8> LOUDNESS_DECAY { {
    { 0.15, 10.47e3 },
    { 0.2, 13.00e3 },
    { 0.4, 27.42e3 },
    { 0.5, 37.04e3 },
    { 0.6, 44.53e3 },
    { 0.7, 206.4e3 },
    { 0.85, 558.5e3 },
    { 1.0, 755.4e3 },
} };

double time_pot (double p, std::span<const std::pair<double, double>> law);
double emphasis_r14_drawn (double p);
double emphasis_r14 (double p);
double glide_r (double p, bool on);
double glide_pot_drawn (double p);
double glide_pot (double p);

inline constexpr double RELEASE_LEAD = 2e-3;
inline constexpr double RETRIGGER_GAP = 0.013;
inline constexpr int32_t LOWEST_KEY_MIDI = 41;
inline constexpr int32_t KEYS = 44;
// Rust's 0..=127 (inclusive bounds).
inline constexpr std::pair<int32_t, int32_t> MIDI_NOTES { 0, 127 };

double feedback_law (double knob);
inline constexpr double FEEDBACK_TAPER = 2.356'915'448'856'724;

struct Contacts
{
    std::optional<std::pair<size_t, size_t>> keys;
    bool gate = false;
    bool s_trig = false;
    int32_t foot = 0;

    static Contacts default_() { return {}; }
    bool operator== (const Contacts&) const = default;
};

struct Jacks
{
    double ext = 0.0;
    std::optional<double> osc;
    std::optional<double> filter;
    std::optional<double> loudness;
    bool s_trig = false;
    std::array<double, 3> detune {};
    double cutoff = 0.0;

    static Jacks default_() { return {}; }
    bool operator== (const Jacks&) const = default;
};

inline constexpr double FILTER_VOLTS_PER_OCTAVE = 0.98;

inline constexpr double MIXER_HISS = 2.48e-9;

struct FrontOut
{
    double i_bus = 0.0;
    double g_bus = 0.0;
    double i0 = 0.0;

    static FrontOut default_() { return {}; }
    bool operator== (const FrontOut&) const = default;
};

inline constexpr double INPUT_VOLTS = 5.0;

double ext_taper (double p);
} // namespace ca72::voice
