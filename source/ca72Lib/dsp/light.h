#pragma once

#include "common.h"
#include "resample.h"
#include "tuning.h"
#include "voice_types.h"

namespace ca72::light
{
inline constexpr size_t CONTROL_EVERY = 16;

inline constexpr size_t FILTER_OVERSAMPLE = 8;

namespace laws
{
    inline constexpr double A4 = 440.0;
    inline constexpr std::array<double, 4> OSC2_CENTS { -855.506, 1753.088, -94.505, 19.578 };
    inline constexpr std::array<double, 4> OSC3_CENTS { -862.370, 1806.991, -206.463, 83.185 };
    inline constexpr double TUNE_CENTS = 267.44;
    inline constexpr double OSC3_FREE_KEY = 55.479;
    inline constexpr std::array<double, 6> OSC3_FREE_CENTS { -3797.431, 9228.8, -4699.331, 3541.6, -1559.71, 402.07 };

    inline constexpr float TRIANGLE = 0.857f;
    inline constexpr float SAWTOOTH = 1.0f;
    inline constexpr float REVERSE_SAWTOOTH = 1.0f;
    inline constexpr float SQUARE = 1.032f;
    inline constexpr float WIDE = 1.046f;
    inline constexpr float NARROW = 1.059f;
    inline constexpr std::array<float, 3> DUTY { 0.484f, 0.30f, 0.179f };
    inline constexpr float TRIANGLE_BOTTOM = 0.4915f;
    inline constexpr float SHARK_TRIANGLE = 0.704f;
    inline constexpr float SHARK_SAWTOOTH = 0.189f;
    inline constexpr float SHARK_AHEAD = 0.0175f;
    inline constexpr std::array<double, 4> SOURCE_OHMS { 687.0, 2246.0, 3500.0, 8790.0 };
    inline constexpr float OSC3_LEVEL = 0.914f;
    inline constexpr double COUPLING_HZ = 13.0;
    inline constexpr double DROOP_HZ = 4.4;
    inline constexpr float MODE_GAIN = 1.0f;
    inline constexpr double MODE_HZ = 3.0;
    inline constexpr float THUMP = 0.0385f;
    inline constexpr std::pair<double, double> THUMP_TIMES { 0.11, 0.035 };

    inline constexpr float WHITE = 0.600f;
    inline constexpr float PINK = 0.440f;
    inline constexpr std::pair<double, double> NOISE_OHMS { 700.0, 26.2e3 };
    inline constexpr std::pair<double, double> WHITE_BAND { 60.0, 10'000.0 };
    inline constexpr std::pair<double, double> PINK_BAND { 40.0, 6'500.0 };
    inline constexpr std::array<double, 2> NOISE_TOP_AT { 48'000.0, 88'200.0 };
    inline constexpr std::array<double, 2> WHITE_TOP { 10'000.0, 19'000.0 };
    inline constexpr std::array<double, 2> PINK_TOP { 6'500.0, 9'000.0 };

    inline constexpr float DRIVE = 0.69f;
    inline constexpr float BIAS = 0.10f;
    inline constexpr double LOOP_SCALE = 0.941'52;
    inline constexpr double DRIVE_CEILING = -0.045;
    inline constexpr float OUT = 0.613f;

    inline constexpr std::array<float, 21> CUTOFF_LOG2 {
        5.2f, 5.5f, 5.81f, 6.345f, 6.787f, 7.257f, 7.745f, 8.306f, 8.878f, 9.459f, 10.045f,
        10.626f, 11.208f, 11.789f, 12.366f, 12.874f, 13.375f, 13.866f, 14.229f, 14.42f, 14.55f,
    };
    inline constexpr std::array<double, 9> CUTOFF_SAT_AT { 14.55, 15.05, 15.55, 16.05, 16.55, 17.55, 18.55, 20.55, 24.55 };
    inline constexpr std::array<double, 9> CUTOFF_SAT {
        14.55, 14.76425192, 14.79149723, 14.80223027, 14.80798777, 14.81368897, 14.81613421, 14.81781641, 14.81830415,
    };
    inline constexpr std::array<double, 3> KEYBOARD_CONTROL { 0.3276, 0.6486, 0.9555 };
    inline constexpr double TRACKING_FROM = 36.0;
    inline constexpr double CONTOUR_OCTAVES_PER_VOLT = 2.10;
    inline constexpr std::array<double, 5> AMOUNT { 0.0, 0.192, 0.487, 0.778, 1.0 };
    inline constexpr std::array<double, 11> EMPHASIS_AT { 0.0, 0.25, 0.5, 0.7, 0.72, 0.735, 0.744, 0.79, 0.85, 0.9, 1.0 };
    inline constexpr std::array<double, 10> LOOP_AT_LOG2 { 5.9, 6.44, 6.864, 7.783, 8.75, 9.745, 10.749, 11.752, 12.747, 13.711 };
    inline constexpr std::array<std::array<double, 11>, 10> LOOP { {
        { 0.146, 0.502, 1.08, 1.95, 2.06, 2.14, 2.2, 2.45, 2.8, 3.2, 3.8 },
        { 0.154, 0.528, 1.2, 2.3, 2.45, 2.56, 2.65, 3.05, 3.55, 3.9, 4.212 },
        { 0.158, 0.54, 1.502, 2.79, 2.955, 3.079, 3.445, 3.805, 4.1, 4.229, 4.429 },
        { 0.166, 0.567, 1.575, 3.306, 3.509, 3.678, 3.693, 4.13, 4.407, 4.652, 5.229 },
        { 0.201, 0.616, 1.675, 3.595, 3.815, 3.962, 3.964, 4.313, 5.043, 5.669, 6.713 },
        { 0.238, 0.668, 1.779, 3.749, 3.986, 4.079, 4.127, 4.651, 5.436, 6.112, 7.695 },
        { 0.244, 0.676, 1.798, 3.82, 4.033, 4.121, 4.127, 4.707, 5.502, 6.186, 8.055 },
        { 0.248, 0.68, 1.81, 3.849, 4.06, 4.123, 4.127, 4.737, 5.536, 6.225, 8.29 },
        { 0.248, 0.68, 1.811, 3.845, 4.061, 4.125, 4.127, 4.74, 5.54, 6.23, 8.267 },
        { 0.248, 0.68, 1.811, 4.05, 4.123, 4.125, 4.127, 4.74, 5.54, 6.23, 8.267 },
    } };
    inline constexpr double RING_GAIN = 2.5;
    inline constexpr std::array<double, 3> THRESHOLD_AT_LOG2 { 7.74, 10.04, 12.37 };
    inline constexpr std::array<double, 3> THRESHOLD { 0.7923, 0.7274, 0.7164 };
    inline constexpr std::array<double, 3> SHIFT { -0.0032, 0.0195, 0.0176 };
    inline constexpr double EMPHASIS_NEAR = 0.012;
    inline constexpr std::array<double, 4> CORNER { -0.1, 0.048, 0.088, 0.111 };
    inline constexpr std::array<double, 4> CORNER_AT_LOG2 { 6.2, 7.78, 9.74, 11.75 };
    inline constexpr std::array<double, 13> PASS_AT_LOG2 {
        5.2, 5.5, 5.81, 6.345, 6.787, 7.257, 7.745, 8.306, 8.878, 10.045, 10.626, 14.229, 14.55,
    };
    inline constexpr std::array<double, 13> PASS_DB { -4.1, -3.4, -2.8, -2.0, -1.5, -1.1, -0.8, -0.45, -0.2, 0.1, 0.3, 0.1, 0.0 };
    inline constexpr std::array<double, 5> RING_RAISE { 0.001, -0.0015, 0.0085, 0.0065, -0.0025 };
    inline constexpr std::array<double, 5> RING_RAISE_AT_LOG2 { 6.44, 7.78, 8.75, 11.75, 12.75 };
    inline constexpr std::array<double, 15> RING_LEVEL_AT_LOG2 {
        6.34, 6.79, 7.26, 7.74, 8.31, 8.88, 9.46, 10.04, 10.63, 11.21, 11.79, 12.37, 12.87, 13.38, 13.87,
    };
    inline constexpr std::array<double, 15> RING_LEVEL {
        1.738, 1.352, 1.239, 1.236, 1.205, 1.212, 1.228, 1.235, 1.226, 1.244, 1.264, 1.300, 1.350, 1.376, 1.451,
    };

    inline constexpr std::array<double, 2> REST { 0.0743, -0.516 };
    inline constexpr std::array<double, 2> PEAK { 4.7917, 5.6014 };
    inline constexpr std::array<double, 2> ATTACK_AIM { 1.82, 1.40 };
    inline constexpr std::array<double, 2> ATTACK_ESR { 1.93e-5, 1.66e-5 };
    inline constexpr std::array<std::tuple<double, double, double>, 2> ATTACK_TIME { {
        { 1.114'91, 1.007'46, 0.0007 },
        { 1.115'32, 1.003'32, 0.0008 },
    } };
    inline constexpr std::array<std::tuple<double, double, double>, 2> DECAY_TIME { {
        { 1.168, 1.000'67, 0.0003 },
        { 1.167, 1.000'55, 0.0003 },
    } };
    inline constexpr std::array<double, 2> DECAY_FALL { 0.05, 0.035 };
    inline constexpr std::array<std::tuple<double, double, double>, 2> RELEASE_TIME { {
        { 1.119'76, 1.002'92, 0.0003 },
        { 1.123'86, 1.004'29, 0.0003 },
    } };
    inline constexpr std::array<std::array<double, 4>, 2> SUSTAIN { {
        { 0.0431, 3.7727, -2.2079, 2.2245 },
        { -0.4729, 4.9704, -2.4266, 2.3829 },
    } };
    inline constexpr std::array<double, 2> RELEASED { 0.1426, -0.4964 };
    inline constexpr std::array<double, 11> RELEASE_OFF_AT { 0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 };
    inline constexpr std::array<std::array<double, 11>, 2> RELEASED_OFF { {
        { 0.1392, -0.0179, -0.0688, -0.0872, -0.1049, -0.1178, -0.1263, -0.1936, -0.2195, -0.2363, -0.2443 },
        { -0.4998, -0.6582, -0.6988, -0.7182, -0.7368, -0.7513, -0.7601, -0.8294, -0.8585, -0.8773, -0.886 },
    } };
    inline constexpr std::array<std::array<double, 11>, 2> RELEASE_OFF { {
        { 0.0005, 0.0178, 0.0209, 0.0217, 0.0224, 0.0228, 0.0231, 0.0249, 0.0256, 0.026, 0.0262 },
        { 0.0005, 0.0174, 0.0200, 0.0208, 0.0215, 0.0220, 0.0222, 0.0238, 0.0243, 0.0246, 0.0247 },
    } };
    inline constexpr double TRIGGER_ON = 0.0078;
    inline constexpr double TRIGGER_OFF = 0.0141;
    inline constexpr double REARM = 0.0115;

    inline constexpr double VCA_STEP = 0.1;
    inline constexpr std::array<float, 54> VCA_GAIN {
        0.0f, 0.001f, 0.0096f, 0.0281f, 0.0503f, 0.0737f, 0.0976f, 0.1215f, 0.1454f, 0.1691f, 0.1927f,
        0.219f, 0.245f, 0.2706f, 0.296f, 0.3211f, 0.3458f, 0.3702f, 0.3969f, 0.4232f, 0.4491f, 0.4746f,
        0.4997f, 0.5245f, 0.5512f, 0.5775f, 0.601f, 0.6264f, 0.6536f, 0.6803f, 0.7065f, 0.7321f, 0.7573f,
        0.782f, 0.8082f, 0.8338f, 0.8589f, 0.8854f, 0.9112f, 0.9364f, 0.961f, 0.9839f, 0.9959f, 0.9992f,
        1.0001f, 1.0f, 0.9994f, 0.9985f, 0.9974f, 0.9961f, 0.9947f, 0.9933f, 0.9917f, 0.99f,
    };

    inline constexpr double GLIDE_RATE = 9.76e7;
    inline constexpr double GLIDE_FALLING = 0.535;
    inline constexpr std::pair<double, double> KEYBOARD_KEYS { 41.0, 84.0 };
    inline constexpr double GLIDE_FARADS = 1e-6;
    inline constexpr double PITCH_AT_REST = 53.75;
    inline constexpr double RELEASE_LEAD = 0.002;

    inline constexpr std::array<double, 4> MOD_SWING { -8.93, -8.85, -8.81, -9.32 };
    inline constexpr std::array<double, 4> MOD_CENTRE { 0.0, -1.47, -0.69, 8.54 };
    inline constexpr double FILTER_PER_SEMITONE = 0.2632;
    inline constexpr std::pair<double, double> MODULATION_OFFSET { 1.06, 0.29 };
    inline constexpr double NOISE_SEMITONES_RMS = 2.80;
    inline constexpr double NOISE_OCTAVES_RMS = 0.679;
    inline constexpr std::pair<double, double> NOISE_OFFSET { 0.25, 0.062 };
    inline constexpr std::array<double, 3> WHEEL { 0.828, 0.584, -0.412 };
    inline constexpr std::pair<double, double> OFFSET { 0.9915, 218.3 };

    inline constexpr double EXT_GAIN = 173.5;
    inline constexpr double EXT_LOAD = 10.16;
    inline constexpr double EXT_CLIP = 0.765;
    inline constexpr std::pair<double, double> OVERLOAD { 0.24, 0.36 };
    inline constexpr double EXT_COUPLING_HZ = 9.0;
    inline constexpr double PREAMP_COUPLING_HZ = 8.0;

    inline constexpr float A440_LEVEL = 0.2385f;

    inline constexpr std::array<double, 4> FEEDBACK_AT { 44'100.0, 48'000.0, 88'200.0, 96'000.0 };
    inline constexpr std::array<double, 4> FEEDBACK_DELAY { 87.9, 87.0, 94.0, 94.2 };
} // namespace laws

struct NoiseLaw
{
    float scale = 0.0f;
    std::array<float, 3> poles {};
    std::array<float, 3> gains {};
    std::pair<float, float> red {};
    std::array<float, 4> band {};

    static NoiseLaw new_ (double rate);
};

// Rust's u128 key set, as two 64-bit words (bit k of the whole: MIDI note k).
struct Held
{
    std::array<uint64_t, 2> bits {};

    static Held default_() { return {}; }
    void set (int32_t key, bool on);
    std::optional<int32_t> lowest() const;
    std::optional<int32_t> highest() const;
    std::optional<int32_t> newest (const std::array<uint32_t, 128>& pressed) const;
    bool any() const { return (bits[0] | bits[1]) != 0; }
    bool has (int32_t key) const { return ((bits[(size_t) key >> 6] >> ((size_t) key & 63)) & 1u) != 0; }
};

struct Osc
{
    float phase = 0.0f;
    ca72::voice::Waveform wave = ca72::voice::Waveform::Sawtooth;

    float tick (float dt, float inv);
    static float peak (ca72::voice::Waveform wave);
};

struct Noise
{
    uint32_t state = 0;
    std::array<float, 3> pink {};
    float red = 0.0f;
    std::pair<float, float> hp {};
    float lp = 0.0f;
    std::pair<float, float> pink_hp {};
    float pink_lp = 0.0f;

    float white();
    std::tuple<float, float, float> tick (const NoiseLaw& c);
};

struct Ladder
{
    std::array<float, 4> s {};

    static Ladder default_() { return {}; }
    float tick (float x, float g, float k, float bias, std::tuple<float, float, float> limit);
};

struct ContourLaw
{
    double attack = 0.0;
    double decay = 0.0;
    double release = 0.0;
    double aim = 0.0;
    double stop = 0.0;
    double sustain = 0.0;
    double released = 0.0;
    double rest = 0.0;
    double fall = 0.0;

    static ContourLaw default_() { return {}; }
    static ContourLaw new_ (ca72::voice::ContourKnobs k, bool decay_switch, size_t which, double rate);
};

struct Contour
{
    double volts = 0.0;
    bool attacking = false;
    bool fresh = false;

    static Contour at_rest (const ContourLaw& law);
    double tick (bool gate, const ContourLaw& law);
};

struct Setting
{
    std::array<double, 3> offset {};
    std::array<bool, 3> follows {};
    std::array<float, 3> gain {};
    float noise = 0.0f;
    float ext = 0.0f;
    double cutoff = 0.0;
    double tracking = 0.0;
    double contour = 0.0;
    std::array<double, 10> emphasis {};
    double corner = 0.0;
    double depth = 0.0;
    double offset_depth = 0.0;
    float mix = 0.0f;
    bool osc_mod = false;
    bool filter_mod = false;
    float swing = 0.0f;
    float swing_bias = 0.0f;
    float mod_swing = 0.0f;
    float mod_centre = 0.0f;
    std::pair<double, double> glide {};
    double beyond = 0.0;
    double ext_gain = 0.0;
    std::array<ContourLaw, 2> contours {};

    static Setting default_() { return {}; }
};

struct Light
{
    double rate_ = 0.0;
    ca72::voice::Panel panel;
    bool set_for_panel = false;
    Setting set {};
    Held held_ {};
    std::array<uint32_t, 128> pressed {};
    uint32_t presses = 0;
    Held waiting {};
    uint32_t gap = 0;
    std::pair<int32_t, uint32_t> lead {};
    uint32_t lead_samples = 0;
    double target = 0.0;
    double pitch = 0.0;
    double foot = 0.0;
    double beyond = 0.0;
    bool gate = false;
    bool trigger = false;
    uint32_t pending = 0;
    bool attack = false;
    uint32_t open = 0;
    uint32_t rearm = 0;
    uint32_t trigger_on = 0;
    uint32_t trigger_off = 0;
    std::array<Osc, 3> osc {};
    std::array<float, 3> dt {};
    std::array<float, 3> dt_step {};
    std::array<float, 3> inv {};
    Noise noise {};
    NoiseLaw noise_c {};
    float feedback_delay = 0.0f;
    std::tuple<float, float, float> coupling {};
    std::tuple<float, float, float> droop {};
    std::tuple<float, float, float> mode {};
    float direct = 0.0f;
    float pass = 0.0f;
    std::pair<float, float> thump_c {};
    std::tuple<float, float, float> thump {};
    float bias = 0.0f;
    Ladder ladder {};
    ca72::resample::Interpolator filter_up;
    ca72::resample::Decimator filter_down;
    float g = 0.0f;
    float g_step = 0.0f;
    float k = 0.0f;
    std::array<Contour, 2> contours_ {};
    size_t count = 0;
    std::array<double, 3> detune {};
    double cutoff_offset = 0.0;
    float osc3 = 0.0f;
    std::pair<float, float> a440 {};
    double feedback = 0.0;
    std::array<float, 128> fed_back {};
    size_t fed_at = 0;
    double lamp = 0.0;
    std::tuple<double, double, double> ext_hp {};
    std::tuple<float, float, float> pre_hp {};
    double overload_peak = 0.0;
    float drive = 0.0f;
    float drive_top = 0.0f;
    std::tuple<float, float, float> limit {};

    static Light new_ (double rate);
    double rate() const { return rate_; }
    void set_drive (double gain);
    void start_oscillators_at (double at);
    void set_seed (uint64_t seed);
    void set_panel (const ca72::voice::Panel& p);
    void set_offsets (std::array<double, 3> detune, double cutoff);
    void note (int32_t midi, bool on);
    std::optional<int32_t> sounding() const;
    bool held() const;
    bool trigger_closed() const;
    std::optional<std::array<double, 3>> slow_state() const;
    std::pair<double, double> contours() const;
    double overload() const;
    double take_overload_peak();
    void rest();
    void control();
    std::pair<float, float> coefficients (double extra) const;
    double tick (double ext);
};
} // namespace ca72::light
