#pragma once
#include "common.h"
#include "devices.h"
#include "unconverged.h"

namespace ca72::voice
{
enum class Quality;
}

namespace ca72::contour
{
inline constexpr devices::Bjt Q2N3392 {
    .is = 5e-15,
    .bf = 212.0,
    .ise = 1e-14,
    .ne = 1.5,
    .vaf = 100.0,
    .ikf = 0.1,
    .br = 4.0,
    .rb = 10.0,
    .re = 0.5,
    .rc = 1.0,
    .cje = 4.5e-12,
    .vje = 0.75,
    .mje = 0.33,
    .cjc = 3.5e-12,
    .vjc = 0.75,
    .mjc = 0.33,
    .tf = 0.4e-9,
    .xti = 3.0,
    .xtb = 1.5,
    .eg = 1.11,
    .tnom = 25.0,
};

inline constexpr devices::Bjt Q12HG = [] {
    devices::Bjt b = Q2N3392;
    b.bf = 650.0;
    return b;
}();

inline constexpr devices::Diode D1N34A { .is = 1e-6, .n = 1.5, .rs = 20.0, .eg = 0.67, .xti = 3.0 };
inline constexpr devices::Diode DCR36 { .is = 9.45e-9, .n = 1.75, .rs = 0.6, .eg = 1.11, .xti = 3.0 };
inline constexpr devices::Diode D1N4004 { .is = 7e-9, .n = 1.9, .rs = 0.04, .eg = 1.11, .xti = 3.0 };

enum class Follower
{
    Complementary,
    Darlington,
};

struct Section
{
    double r7;
    double c;
    std::array<std::pair<double, double>, 3> absorption;
    double esr;
    double r_in;
    Follower follower;
    double r10;
    double r_sus;
    double r_neg;
    double r_gnd;
    double r_top;
    double r_bot;
    double r3;
    double r4;
    double r14;
    double r5;
    double r6;
    double r_reset;
    double r8;
    double r9;

    bool operator== (const Section&) const = default;
};

inline constexpr std::array<std::pair<double, double>, 3> C_ABSORPTION {
    std::pair<double, double> { 0.769e-6, 779.0 },
    std::pair<double, double> { 0.202e-6, 28.26e3 },
    std::pair<double, double> { 0.223e-6, 237.6e3 },
};

inline constexpr double C_ESR = 1.53;

inline constexpr double R7 = 86.6;

inline constexpr Section FILTER_SECTION {
    R7, 10e-6, C_ABSORPTION, C_ESR, 10e3, Follower::Complementary, 27e3, 4.7e3, 150e3, 3e3,
    5.6e3, 16.9e3, 10e3, 10e3, 33e3, 3.3e3, 560.0, 10e3, 10e3, 4.7e3,
};

inline constexpr Section LOUDNESS_SECTION {
    R7, 10e-6, C_ABSORPTION, C_ESR, 10e3, Follower::Darlington, 27e3, 4.7e3, std::numeric_limits<double>::infinity(), 4.7e3,
    6.8e3, 16.9e3, 10e3, 10e3, 33e3, 3.3e3, 560.0, 10e3, 10e3, 4.7e3,
};

struct ContourCircuit
{
    double p93;
    double vp;
    double vn;
    double r60;
    double c7;
    double r55;
    double r37;
    double r19;
    double r23;
    double r32;
    double r49;
    double r65;
    double r34;
    double c13;
    double r1401;
    bool dump_each;
    Section filter;
    Section loudness;
    devices::Bjt npn;
    devices::Bjt pnp;
    devices::Bjt q12;
    devices::Diode ge;
    devices::Diode si;
    devices::Diode peak_diode;

    static ContourCircuit default_();
};

struct Controls
{
    double attack = 0.0;
    double decay = 0.0;
    double sustain = 0.0;

    bool operator== (const Controls&) const = default;
};

struct Panel
{
    Controls filter;
    Controls loudness;
    bool decay_on = false;
    bool s_trig = false;

    bool operator== (const Panel&) const = default;
};

struct Loads
{
    double filter = 0.0;
    double loudness = 0.0;

    static Loads default_() { return Loads { 1.0 / 5e3, 1.0 / 2e6 }; }
    bool operator== (const Loads&) const = default;
};

struct SectionState
{
    double v = 0.0;
    bool set = false;
    double out = 0.0;
    double e7 = 0.0;
    double j7 = 0.0;
    double j8 = 0.0;
    bool attacked = false;
    std::array<double, 3> da { 0.0, 0.0, 0.0 };

    static SectionState default_() { return {}; }
    bool operator== (const SectionState&) const = default;

    std::tuple<double, double, double> absorption (const Section& s, double h) const;
    void absorb (const Section& s, double h, double v_old);
    bool absorbed (const Section& s) const;
};

struct SectionSetup
{
    double v_th8 = 0.0;
    double r_e8 = 0.0;
    double v_e6 = 0.0;
    double peak = 0.0;

    static SectionSetup default_() { return {}; }
    bool operator== (const SectionSetup&) const = default;
};

using DevicesAt = std::tuple<uint64_t, std::array<devices::Bjt, 3>, std::array<devices::Diode, 2>, std::array<devices::BjtAt, 3>, std::array<devices::DiodeAt, 2>>;

inline constexpr double SETTLED = 1e-13;
inline constexpr double COUPLED = 1e-6;
inline constexpr double COUPLING_CURRENT = 0.5e-3;
inline constexpr size_t COUPLING_PASSES = 40;

struct ContourOut
{
    double filter = 0.0;
    double loudness = 0.0;
    double vtrig = 0.0;

    static ContourOut default_() { return {}; }
    bool operator== (const ContourOut&) const = default;
};

template <typename F>
double root_slope (F f, double lo, double hi, double x, bool rising, double tol)
{
    x = ca72::clamp (x, std::fmin (lo, hi), std::fmax (lo, hi));
    for (int it = 0; it < 80; ++it)
    {
        const auto [fx, slope] = f (x);
        if ((fx < 0.0) == rising)
            lo = x;
        else
            hi = x;
        const double next = slope != 0.0 ? x - fx / slope : std::numeric_limits<double>::quiet_NaN();
        if (std::fabs (next - x) < tol && next >= std::fmin (lo, hi) - 1e-9 && next <= std::fmax (lo, hi) + 1e-9)
            return next;
        const double nx = (next > std::fmin (lo, hi) && next < std::fmax (lo, hi)) ? next : 0.5 * (lo + hi);
        if (std::fabs (hi - lo) < tol)
            return nx;
        x = nx;
    }
    unconverged::note (unconverged::Solver::ContourRoot);
    return x;
}

template <typename F>
double root_dir (F f, double lo, double hi, double x, bool rising, double tol)
{
    x = ca72::clamp (x, std::fmin (lo, hi), std::fmax (lo, hi));
    for (int it = 0; it < 80; ++it)
    {
        const double fx = f (x);
        if ((fx < 0.0) == rising)
            lo = x;
        else
            hi = x;
        const double d = 1e-7 * (1.0 + std::fabs (x));
        const double slope = (f (x + d) - fx) / d;
        const double next = slope != 0.0 ? x - fx / slope : std::numeric_limits<double>::quiet_NaN();
        if (std::fabs (next - x) < tol && next >= std::fmin (lo, hi) - 1e-9 && next <= std::fmax (lo, hi) + 1e-9)
            return next;
        const double nx = (next > std::fmin (lo, hi) && next < std::fmax (lo, hi)) ? next : 0.5 * (lo + hi);
        if (std::fabs (hi - lo) < tol)
            return nx;
        x = nx;
    }
    unconverged::note (unconverged::Solver::ContourRoot);
    return x;
}

template <typename F>
double root (F f, double lo, double hi, double x, double tol)
{
    const bool rising = f (lo) < 0.0;
    return root_dir (f, lo, hi, x, rising, tol);
}

std::tuple<std::array<double, 2>, double, double> grounded_npn (
    const devices::BjtAt& m, const devices::Bjt& q, std::array<double, 2> x, double vb, double r, double vc, double tol);

std::tuple<std::array<double, 2>, double, double> grounded_npn_with (
    const devices::BjtAt& m, const devices::Bjt& q, std::array<double, 2> x, std::tuple<double, double, double> vb_r_vc, double tol, bool pnjlim);

std::pair<std::array<double, 2>, std::array<double, 2>> grounded_npn_slopes (
    const devices::BjtAt& m, const devices::Bjt& q, std::array<double, 2> x, double r);

struct Contours
{
    ContourCircuit circuit;
    double celsius;
    Loads loads;
    double h;
    double tol;
    double tol_decay;
    double t2;
    double v7;
    std::array<SectionState, 2> sections;
    double rst;
    double vtrig;
    double j10;
    std::array<double, 2> j_feed;
    double j12b;
    std::array<double, 2> x20;
    std::array<double, 2> x12;
    std::optional<std::tuple<Panel, Loads, double, std::array<SectionSetup, 2>>> setup_;
    std::optional<std::tuple<bool, Panel, Loads, uint64_t>> inputs;
    bool trig_settled;
    std::array<bool, 2> sec_settled;
    bool lean;
    std::optional<std::tuple<bool, bool, uint64_t>> trig_inputs;
    std::array<double, 2> dump;
    mutable std::optional<DevicesAt> devices;

    void set_quality (voice::Quality q);
    void set_step (double h);
    static Contours new_ (ContourCircuit circuit, double rate);
    std::pair<std::array<devices::BjtAt, 3>, std::array<devices::DiodeAt, 2>> devices_and_diodes() const;
    void settle (const Panel& panel);
    std::array<SectionSetup, 2> setup (const Panel& panel);
    double peak (const Section& s) const;
    double q6_emitter (const Section& s) const;
    double follow (const Section& s, double g, double v, double guess, std::pair<devices::BjtAt, devices::BjtAt> np) const;
    ContourOut tick (bool key, const Panel& panel);
    std::array<double, 2> capacitors() const { return { sections[0].v, sections[1].v }; }
    std::array<double, 2> peaks (const Panel& panel);
    std::array<double, 3> trigger_nodes() const { return { v7, rst, vtrig }; }
};
} // namespace ca72::contour
