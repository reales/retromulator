#pragma once
#include "common.h"
#include "devices.h"
#include "mna.h"
#include "resample.h"

namespace ca72::voice
{
enum class Quality;
}

namespace ca72::preamp
{
struct PreampCircuit
{
    double r_lamp;
    double i_lamp;
    devices::Bjt q27;
    devices::Bjt q33;
    devices::Bjt q25;
    devices::Bjt q31;
    devices::Bjt q34;

    static PreampCircuit default_();
};

inline constexpr mna::Node P10 = 1;
inline constexpr mna::Node N10 = 2;
inline constexpr mna::Node SRC = 3;
inline constexpr size_t HELD = 4;
inline constexpr mna::Node N78 = 4;
inline constexpr mna::Node B27 = 5;
inline constexpr mna::Node C27 = 6;
inline constexpr mna::Node N64 = 7;
inline constexpr mna::Node TAIL = 8;
inline constexpr mna::Node B32 = 9;
inline constexpr mna::Node N61 = 10;
inline constexpr mna::Node N62 = 11;
inline constexpr mna::Node AMP = 12;
inline constexpr mna::Node OUT = 13;
inline constexpr size_t NODES = 14;

inline constexpr mna::Node L15 = 1;
inline constexpr mna::Node LN10 = 2;
inline constexpr mna::Node LAMP_IN = 3;
inline constexpr size_t LHELD = 4;
inline constexpr mna::Node B25 = 4;
inline constexpr mna::Node E25 = 5;
inline constexpr mna::Node M = 6;
inline constexpr mna::Node E31 = 7;
inline constexpr mna::Node LAMP = 8;
inline constexpr mna::Node C34 = 9;
inline constexpr size_t LNODES = 10;

inline constexpr size_t SUBSTEPS = 4;

inline constexpr double R61 = 232e3;

struct PreampOut
{
    double amp = 0.0;
    double i_bus = 0.0;
    double lamp = 0.0;

    static PreampOut default_() { return {}; }
    bool operator== (const PreampOut&) const = default;
};

struct Plain
{
    double lp_in = 0.0;
    double v26 = 0.0;
    double lp_20 = 0.0;
    double peak = 0.0;
    std::pair<double, double> fall { 0.0, 0.0 };

    static Plain default_() { return {}; }
    static double high (double x, double& state, double tau, double h);
    static double clip (double u);
    PreampOut tick (double v_in, double r_src, bool on, double h);
};

enum class InLoop
{
    Delayed,
    Circuit,
    Plain,
};

struct Delayed
{
    resample::Interpolator up;
    resample::Decimator down;
    std::array<double, 32> line;
    size_t pos;
    size_t delay;
    double lp_in;
    double v26;
    double lp_20;
    double peak;
    double amp;
    std::pair<double, double> key;
    std::pair<double, double> k_in;
    double k_20;
    double k_26;
    double fall;

    static Delayed new_ (double latency);
    void reset();
    static double high (double x, double& state, double k);
    PreampOut tick (double v_in, double r_src, bool on, double dt);
};

struct Preamp
{
    PreampCircuit circuit;
    mna::Circuit net;
    mna::Circuit lamp;
    size_t r_src;
    size_t r46;
    double dt;
    resample::Interpolator up;
    resample::Decimator down_amp;
    resample::Decimator down_out;
    std::pair<double, double> last;
    double r_in;
    bool on;
    std::optional<Plain> plain;
    Delayed delayed;
    bool delayed_on;
    size_t failed;

    std::pair<const mna::Circuit&, const mna::Circuit&> circuits() const { return { net, lamp }; }
    void set_quality (voice::Quality q);
    static ca72::Result<Preamp, mna::NoConvergence> new_ (PreampCircuit circuit, double rate, double r_src);
    void set_source_resistance (double r_src);
    void set_switch (bool on);
    static bool step (mna::Circuit& net, double h);
    double latency() const;
    void set_delayed (bool on);
    PreampOut tick (double v_in);
    PreampOut outputs();
};
} // namespace ca72::preamp
