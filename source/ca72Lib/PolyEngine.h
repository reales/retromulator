#pragma once

#include "dsp/voice.h"
#include "dsp/light.h"
#include "dsp/resample.h"

// POLY, UNISON, DOUBLE, SPREAD and ENTROPY, after upstream's ca72-plugin engine.rs and character.rs.
namespace ca72poly
{
using ca72::voice::Panel;

inline constexpr size_t MAX_VOICES = 10;
inline constexpr double ENTROPY_DEPTH = 2.0;
inline constexpr double ENTROPY_FLOOR = 0.75;
inline constexpr double DOUBLE_CENTS = 100.0;
inline constexpr double SILENT = 1e-6;

struct Character
{
    std::array<double, 3> tune {};
    double cutoff = 0.0;
    std::array<double, 6> knob {};
    std::array<double, 4> drift {};
    std::array<double, 3> waver {};
    uint64_t rng = 1;
    uint32_t count = 0;
    double rate = std::numeric_limits<double>::quiet_NaN();
    std::pair<double, double> drift_ab {}, waver_ws {};

    static Character make (uint64_t seed);
    double uniform();
    double normal();
    double knob_at (size_t k, double x, double amount) const;
    std::pair<std::array<double, 3>, double> offsets (double amount, double rate);
};

struct PolyKey
{
    std::optional<int> down;
    uint32_t open = 0, gap = 0;

    static PolyKey make (double rate);
    template <typename F> void step (bool gate, int key, F&& note);
    template <typename F> void lift (F&& note)
    {
        if (down) { note (*down, false); down.reset(); }
    }
};

struct Settings
{
    bool poly = false;
    int voices = 4;
    bool unison = false;
    bool doubled = false;
    double detune = 0.0;  // 0..1 of DOUBLE_CENTS
    double spread = 0.0;  // 0..1
    double entropy = 0.0; // 0..1
    double drive = 1.0;    // DRIVE's gain into the filter
    double feedback = 0.0; // FEEDBACK's send (voice::feedback_law of the knob)
};

struct Player
{
    std::optional<ca72::voice::Voice> voice;
    std::optional<ca72::light::Light> light;
    Character ch;
    PolyKey key;
    Panel shown {};
    bool shownValid = false;

    void note (int k, bool on);
    double tick (double cents_sign_offset, double amount_osc, double amount_cut, double at, double rate);
    void setPanel (const Panel& p, double entropy);
};

struct Slot
{
    Player main, twin;
    int note = -1;
    bool gate = false, shown = false, drop = false, active = false;
    uint64_t age = 0;
    double peak = 0.0;
    int quiet = 0;
};

class Engine
{
public:
    // factor > 1: voices run at rate * factor and the mix is decimated once.
    void build (double hostRate, size_t factor, bool light, ca72::voice::Quality q, int bleps, bool corners, uint64_t seed);

    void noteOn (int key);
    void noteOff (int key);
    void allNotesOff();
    void render (float* left, float* right, int n, const Panel& p, const Settings& s);

    ca72::voice::Quality quality = ca72::voice::Quality::Potato;
    int mode = -1; // the processor's QualityMode it was built for

private:
    void oneNote (int key, bool on);
    int instruments() const;
    int voices() const;
    void pan (int k, double& l, double& r, double& tl, double& tr) const;
    static bool step (double& v, double target, double coef);
    bool glide (const Panel& target, double coef);

    std::vector<Slot> slots;
    Settings set;
    size_t factor = 1;
    double voiceRate = 48000.0;
    bool isLight = false;
    uint64_t counter = 0;
    std::array<bool, 128> held {};
    ca72::resample::Decimator decL, decR;
    Panel smooth {};
    double smoothSpread = 0.0, smoothDetune = 0.0;
    double drive = -1.0, feedback = -1.0;
    bool primed = false;
};
} // namespace ca72poly
