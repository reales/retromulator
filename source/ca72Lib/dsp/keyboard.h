#pragma once
#include "common.h"
#include "devices.h"
#include "mna.h"
#include "vcf.h"
#include <string_view>

namespace ca72::voice
{
enum class Quality;
}

namespace ca72::keyboard
{
inline constexpr size_t KEYS = 44;

inline constexpr double R_FLOOR = 5.0 * 10.0;

struct KeyboardCircuit
{
    double r1;
    double r20;
    double r22;
    double r25;
    devices::Bjt q9;
    devices::Bjt q11;
    double r_string;
    double contact;
    double r_floor;
    double c9;
    double r53;
    double r52;
    double r58;
    double r51;
    double r59;
    double r54;
    double r61;
    devices::Bjt q23;
    devices::Bjt q24;
    mna::Jfet jfet;
    double r64;
    double r31;
    double r65;
    double c13;
    double r34;
    double c6;
    double r21;
    double r2;
    double r18;
    double r30;

    static KeyboardCircuit default_();

    ca72::Result<double, mna::NoConvergence> source_current (double r_load) const;
    ca72::Result<double, mna::NoConvergence> string_current() const;
    ca72::Result<std::pair<double, double>, mna::NoConvergence> source_thevenin() const;
};

inline constexpr mna::Depletion CR5_CAP { 20e-12, 1.0, 0.33, 0.5 };
inline constexpr double CR5_TT = 3e-6;

struct Load
{
    double r = 0.0;
    double v = 0.0;

    static Load of (size_t oscillators, bool kbd_1, bool kbd_2);
    bool operator== (const Load&) const = default;
};

inline const Load LOAD_DEFAULT {
    1.0 / (3.0 / 51.1e3 + 1.0 / vcf::R53),
    -5.0 * (3.0 / 51.1e3) / (3.0 / 51.1e3 + 1.0 / vcf::R53),
};

inline constexpr mna::Node P10 = 1;
inline constexpr mna::Node N10 = 2;
inline constexpr mna::Node ISRC = 3;
inline constexpr mna::Node LOADV = 4;
inline constexpr size_t HELD = 5;
inline constexpr mna::Node KCUR = 5;
inline constexpr mna::Node S_HI = 6;
inline constexpr mna::Node S_LO = 7;
inline constexpr mna::Node TRIG = 8;
inline constexpr mna::Node T2 = 9;
inline constexpr mna::Node GN = 10;
inline constexpr mna::Node G13 = 11;
inline constexpr mna::Node BUS = 12;
inline constexpr mna::Node E23 = 13;
inline constexpr mna::Node C23 = 14;
inline constexpr mna::Node C14 = 15;
inline constexpr mna::Node FB = 16;
inline constexpr mna::Node KO = 17;
inline constexpr mna::Node GIN = 18;
inline constexpr mna::Node C6 = 19;
inline constexpr mna::Node G10 = 20;
inline constexpr mna::Node D10 = 21;
inline constexpr mna::Node OUT = 22;
inline constexpr size_t NODES = 23;

inline constexpr double SHORT = 1e-6;
inline constexpr double OPEN = 1e12;

inline constexpr size_t BLOCK_MAX = 64;
inline constexpr double QUIET_V = 1e-7;
inline constexpr size_t QUIET_STEPS = 4;

inline constexpr size_t SUBSTEPS = 8;

std::pair<double, double> tolerance (voice::Quality q);
size_t substeps (voice::Quality q);

inline constexpr double ACTIVE_S = 2e-3;
inline constexpr double PREDICT_LIMIT = 0.05;
inline constexpr size_t STEP_ITERATIONS = 16;
inline constexpr double FAST_V = 1e-3;

struct Keyboard
{
    KeyboardCircuit circuit;
    mna::Circuit net;
    double rate;
    double string_current_;
    size_t r_top;
    size_t r_mid;
    size_t r_bot;
    size_t r_lo;
    size_t r_hi;
    size_t r_trig;
    size_t r_glide;
    size_t r_load;
    std::optional<std::pair<size_t, size_t>> keys;
    bool trig;
    double glide;
    Load load;
    size_t quiet;
    size_t pending;
    std::array<double, 4> last;
    size_t since_change;
    bool fast;
    voice::Quality quality;
    size_t subdivided;
    size_t failed;

    static Keyboard new_ (KeyboardCircuit circuit, double rate);
    double string_current() const { return string_current_; }
    double key_voltage (size_t k) const;
    const mna::Circuit& circuit_() const { return net; }
    void set_quality (voice::Quality q);
    void set_rate (double rate);
    double out() const;
    std::optional<double> probe (std::string_view name) const;
    void set_glide (double r);
    void set_load (Load load);
    void contacts (std::optional<std::pair<size_t, size_t>> keys, bool trig);
    void apply (std::optional<std::pair<size_t, size_t>> keys, bool trig);
    void flush();
    void step (double h);
    bool halve (double h, size_t depth);
    double tick (std::optional<std::pair<size_t, size_t>> keys, bool trig);
    ca72::Result<void, mna::NoConvergence> settle();
    ca72::Result<double, mna::NoConvergence> static_out (size_t k) const;
};
} // namespace ca72::keyboard
