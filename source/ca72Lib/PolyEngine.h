#pragma once

#include "dsp/voice.h"
#include "dsp/light.h"
#include "dsp/resample.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// POLY, UNISON, DOUBLE, SPREAD and ENTROPY, after upstream's ca72-plugin engine.rs and character.rs.
namespace ca72poly
{
using ca72::voice::Panel;

inline constexpr size_t MAX_VOICES = 10;
inline constexpr int POLY_VOICES = 4;      // POLY's voices, one per core
inline constexpr int UNISON_VOICES = 3;    // mono UNISON: left, right and centre
inline constexpr double UNISON_CENTS = 10.0; // its outer voices' detune, flat left and sharp right
inline constexpr double HUSH_FADE = 0.005;  // All Sound Off's fade, s
inline constexpr double HUSH_WAIT = 0.1;    // the longest it waits for a late slot, s
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

struct Gain
{
    double l = 0.0, r = 0.0, tl = 0.0, tr = 0.0;
};

// A note change for one slot, at a sample of the block.
struct SlotEvent
{
    int pos = 0;
    bool poly = false; // true: the slot's POLY gate; false: a note straight to its players
    int key = 0;
    bool on = false, twin = false, drop = false;
};

// A stretch of the block with one panel target (blocks are split at MIDI events).
struct Segment
{
    int from = 0, to = 0;
    Panel target {};
};

// The voice allocator's view of a slot (main thread). Notes queue here until the slot's next render.
struct Alloc
{
    int note = -1;
    bool gate = false, shown = false, drop = false, active = false;
    bool late = false; // a worker still has it, past a block's deadline
    bool rest = false; // to be put back as built once All Sound Off has faded out
    uint64_t age = 0;
    int quiet = 0;
    std::vector<SlotEvent> events;
};

// The block as a slot renders it: copied in, so a worker running late reads nothing the next block changes.
struct BlockPlan
{
    std::vector<Segment> segments;
    double coef = 0.0, entropy = 0.0, oscillators = 0.0, at = 0.0, pairTrim = 1.0;
    double spread = 0.0, detune = 0.0, unisonCents = 0.0;
    bool doubled = false, polyMode = false, many = false;
    int places = 1;
};

// What one slot's renderer owns: it can run on its own thread during finish().
struct Slot
{
    Player main, twin;
    BlockPlan plan;
    int note = -1;
    bool gate = false, drop = false;
    Panel smooth {};
    double smoothSpread = 0.0, smoothDetune = 0.0;
    std::vector<SlotEvent> events;
    std::vector<double> outL, outR;
    int from = 0;
    bool run = false;
    double peak = 0.0;
    double drive = -1.0, feedback = -1.0;
    bool lift = false; // let go of its keys when it is back (POLY switched while it was late)
};

// Real-time threads for slots 1.. of a block; the audio thread takes jobs too and waits for the rest
// until the deadline. Without a host audio workgroup they join a macOS work interval of their own.
class Workers
{
public:
    using Clock = std::chrono::steady_clock;

    Workers (size_t threads, std::function<void (int)> job, std::function<bool()> joinHost);
    ~Workers();
    // Returns the jobs not done by the deadline, as a bit mask of slots.
    uint32_t run (const int* jobs, int n, std::optional<Clock::time_point> deadline);
    bool finished (int slot) const { return done[(size_t) slot].load (std::memory_order_acquire); }
    void rejoin() { joinGen.fetch_add (1, std::memory_order_release); }

private:
    void loop();
    void work (uint32_t g);

    std::function<void (int)> job;
    std::function<bool()> joinHost;
    std::array<int, POLY_VOICES> list {};
    std::array<std::atomic<bool>, POLY_VOICES> done {};
    std::atomic<uint64_t> ticket { 0 }; // generation << 32 | next job
    std::atomic<uint32_t> gen { 0 };
    std::atomic<int> njobs { 0 };
    std::atomic<uint32_t> joinGen { 0 }, opened { 0 };
    std::atomic<int64_t> budgetNs { 0 };
    std::atomic<uint64_t> fp { 0 };
    std::atomic<bool> quit { false };
    void* interval = nullptr; // os_workgroup_interval_t
    std::vector<std::thread> threads;
};

class Engine
{
public:
    // factor > 1: voices run at rate * factor and the mix is decimated once.
    void build (double hostRate, size_t factor, bool light, ca72::voice::Quality q, int bleps, bool corners, uint64_t seed);

    // A block: begin, then advance() up to each MIDI event and handle it, then finish().
    void begin (int n, const Settings& s);
    void advance (int until, const Panel& p);
    void noteOn (int key);
    void noteOff (int key);
    void allNotesOff();
    // All Sound Off: keys let go, the output faded out, the voices put back as built, faded in.
    void allSoundOff();
    // deadline: when to stop waiting for late workers (none offline: wait for all).
    void finish (float* left, float* right, std::optional<Workers::Clock::time_point> deadline = std::nullopt);

    // begin + advance + finish with no events.
    void render (float* left, float* right, int n, const Panel& p, const Settings& s);

    // One slot's share of the block (thread safe across slots).
    void renderSlot (size_t k);

    // Renders slots on worker threads from now on; joinHost runs on each worker (audio workgroup).
    void startThreads (std::function<bool()> joinHost = {});
    void rejoinThreads() { if (workers) workers->rejoin(); }

    ca72::voice::Quality quality = ca72::voice::Quality::Potato;
    int mode = -1; // the processor's QualityMode it was built for

private:
    void oneNote (int key, bool on);
    void startSlot (size_t k);
    int instruments() const;
    int voices() const;
    static void pan (int k, int places, bool many, double spread, bool doubled, Gain& g);
    void letGo (Slot& sl);
    void settle();
    void restore (size_t k);
    static bool step (double& v, double target, double coef);
    static bool glide (Panel& smooth, const Panel& target, double coef);

    std::vector<Slot> slots;
    std::vector<Alloc> alloc;
    std::vector<Segment> segments;
    Settings set;
    size_t factor = 1;
    double voiceRate = 48000.0;
    bool isLight = false;
    uint64_t counter = 0;
    std::array<bool, 128> held {};
    ca72::resample::Decimator decL, decR;
    Panel smooth {};
    double smoothSpread = 0.0, smoothDetune = 0.0;
    bool primed = false;
    std::optional<ca72::voice::Voice> proto;
    uint64_t seed = 0;
    bool hush = false;
    double fade = 1.0, fadeStep = 1.0;
    int hushWaited = 0;

    int blockN = 0, pos = 0, count = 1;
    bool polyMode = false;
    double coef = 0.0, entropy = 0.0, oscillators = 0.0, at = 0.0, trim = 1.0, pairTrim = 1.0;

    std::unique_ptr<Workers> workers; // last: stops before the slots go
};
} // namespace ca72poly
