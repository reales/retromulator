#pragma once

#include "common.h"
#include "devices.h"
#include "expo.h"
#include "resample.h"

namespace ca72::voice
{
enum class Quality;
}

namespace ca72::vcf
{
inline constexpr double R53 = 312e3;
inline constexpr double R54 = 156e3;
inline constexpr double R74 = 48.1e3;

inline constexpr ca72::devices::Bjt TIS97 {
    .is = 5e-15,
    .bf = 420.0,
    .ise = 1e-14,
    .ne = 1.5,
    .vaf = 100.0,
    .ikf = 0.1,
    .br = 4.0,
    .rb = 50.0,
    .re = 0.5,
    .rc = 1.0,
    .cje = 5e-12,
    .vje = 0.75,
    .mje = 0.33,
    .cjc = 3e-12,
    .vjc = 0.75,
    .mjc = 0.33,
    .tf = 0.4e-9,
    .xti = 3.0,
    .xtb = 1.5,
    .eg = 1.11,
    .tnom = 25.0,
};
inline constexpr ca72::devices::Bjt TIS92 {
    .is = 1e-14,
    .bf = 200.0,
    .ise = 2e-14,
    .ne = 1.5,
    .vaf = 80.0,
    .ikf = 0.3,
    .br = 4.0,
    .rb = 10.0,
    .re = 0.3,
    .rc = 0.5,
    .cje = 15e-12,
    .vje = 0.75,
    .mje = 0.33,
    .cjc = 8e-12,
    .vjc = 0.75,
    .mjc = 0.33,
    .tf = 0.5e-9,
    .xti = 3.0,
    .xtb = 1.5,
    .eg = 1.11,
    .tnom = 25.0,
};
inline constexpr ca72::devices::Bjt TIS93 = []
{
    ca72::devices::Bjt b = TIS92;
    b.tf = 0.6e-9;
    return b;
}();

struct ExpoPoint
{
    double i0 = 0.0;
    double v_ctl = 0.0;
    double v_e26 = 0.0;
    double v_tail = 0.0;

    bool operator== (const ExpoPoint&) const = default;
};

struct FilterExpo
{
    double r39;
    double r46;
    double r49;
    double r70;
    double r74;
    double r45;
    double r60;
    double vp;
    double vn;
    ca72::devices::Bjt q26;
    ca72::devices::Bjt q28;
    ca72::devices::Bjt ladder;
    std::array<double, 5> chain;
    double r67;
    double r54;

    bool operator== (const FilterExpo&) const = default;

    static FilterExpo default_()
    {
        return FilterExpo {
            .r39 = 0.5,
            .r46 = 68e3,
            .r49 = 250.0,
            .r70 = 1.8e3,
            .r74 = 47e3,
            .r45 = 330.0,
            .r60 = 680.0,
            .vp = 10.0,
            .vn = -10.0,
            .q26 = TIS93,
            .q28 = TIS92,
            .ladder = TIS97,
            .chain = { 220.0, 150.0, 150.0, 150.0, 150.0 },
            .r67 = 200.0,
            .r54 = 470.0,
        };
    }

    double current (std::span<const ca72::expo::Input> inputs, double celsius) const;
    double tail_node (double i0, double celsius) const;
    // Rust's private FilterExpo::q28 (renamed: it clashes with the field q28).
    std::tuple<double, double, double> q28_ (double v_b, double celsius) const;
    std::pair<ExpoPoint, double> at_node (double v_ctl, double celsius) const;
    ExpoPoint solve (std::span<const ca72::expo::Input> inputs, double celsius) const;
    std::pair<double, double> node (std::span<const ca72::expo::Input> inputs) const;
};

inline constexpr double EXPO_V_MIN = -0.8;
inline constexpr double EXPO_V_STEP = 0.001;
inline constexpr size_t EXPO_POINTS = 1601;

struct ExpoTable
{
    FilterExpo expo;
    double celsius;
    std::vector<double> ln_i0;
    std::vector<double> ib26;

    static std::shared_ptr<const ExpoTable> shared (FilterExpo expo, double celsius);
    static ExpoTable new_ (FilterExpo expo, double celsius);
    double current (double i_in, double g_node) const;
    std::pair<double, double> current_and_node (double i_in, double g_node) const;
};

struct LadderBias
{
    std::array<double, 4> e {};
    double b_in = 0.0;
    double v_in = 0.0;
    std::array<double, 4> v_stage {};
    double vt_in = 0.0;
    std::array<double, 4> vt_stage {};
    std::array<double, 4> c_stage {};

    bool operator== (const LadderBias&) const = default;

    LadderBias lerp (const LadderBias& o, double t) const;
};

inline constexpr size_t STATES = 9;
inline constexpr size_t W = 4;
inline constexpr size_t Q10 = 5;
inline constexpr size_t NT = 6;
inline constexpr size_t VB = 7;
inline constexpr size_t Q27 = 8;

using Jacobian = std::array<std::array<double, STATES>, STATES>;
using JacobianOut = std::pair<Jacobian*, std::array<double, STATES>*>;

struct Drive
{
    double i_n = 0.0;
    double g_n = 0.0;
    LadderBias bias;
    double p = 0.0;
    double gain = 0.0;
    bool plain = false;

    bool operator== (const Drive&) const = default;
};

struct VcfCircuit
{
    double c;
    ca72::devices::Bjt ladder;
    double ladder_vbc;
    double c27;
    double r54;
    double r67;
    double r_chain;
    double c24;
    double c5;
    double r31;
    double r3;
    double r7;
    double r6;
    double c9;
    double c10;
    double r14;
    double r73;
    double r73_pos;
    double r76;

    bool operator== (const VcfCircuit&) const = default;

    static VcfCircuit default_()
    {
        return VcfCircuit {
            .c = 0.068e-6,
            .ladder = TIS97,
            .ladder_vbc = -0.9,
            .c27 = 10e-6,
            .r54 = 470.0,
            .r67 = 200.0,
            .r_chain = 820.0,
            .c24 = 220e-6,
            .c5 = 0.22e-6,
            .r31 = 47e3,
            .r3 = 1000.0,
            .r7 = 180.0,
            .r6 = 47.0,
            .c9 = 100e-6,
            .c10 = 10e-6,
            .r14 = 50e3,
            .r73 = 1000.0,
            .r73_pos = 0.5,
            .r76 = 330.0,
        };
    }

    LadderBias bias (double i0, double celsius) const;

    // Option<&mut Jacobian>: nullptr for None.
    std::pair<std::array<double, STATES>, double> eval (const std::array<double, STATES>& y, const Drive& d, Jacobian* jac) const;
    std::pair<std::array<double, STATES>, double> eval_from (const std::array<double, STATES>& y,
                                                             const Drive& d,
                                                             Jacobian* jac,
                                                             std::array<ca72::devices::PairWarm, 5>& zs) const;
    std::pair<std::array<double, STATES>, double> eval_full (const std::array<double, STATES>& y,
                                                             const Drive& d,
                                                             std::optional<JacobianOut> jac,
                                                             std::array<ca72::devices::PairWarm, 5>& zs) const;
    std::pair<std::array<double, STATES>, double> eval_sparse (const std::array<double, STATES>& y,
                                                               const Drive& d,
                                                               std::optional<JacobianOut> jac,
                                                               std::array<ca72::devices::PairWarm, 5>& zs,
                                                               bool zero) const;
};

inline constexpr double PREWARP_HZ = 16e3;
inline constexpr double MODE_RT = 23.6e3;
inline constexpr double MODE_HZ = 3.0;

struct Vcf
{
    VcfCircuit circuit;
    FilterExpo expo;
    double celsius = 25.0;
    double prewarp_hz = PREWARP_HZ;
    size_t oversample = 0;
    double h = 0.0;
    std::array<double, STATES> y {};
    std::optional<std::array<double, STATES>> f_prev;
    std::tuple<double, double, LadderBias> bias;
    ca72::resample::Interpolator up_n;
    ca72::resample::Interpolator up_g;
    ca72::resample::Decimator down;
    size_t other = 0;
    std::tuple<ca72::resample::Interpolator, ca72::resample::Interpolator, ca72::resample::Decimator> other_rs;
    std::shared_ptr<const std::vector<LadderBias>> bias_table;
    std::array<uint64_t, 2> bias_key {};
    bool potato = false;
    bool recip = false;
    double tol = 1e-10;
    size_t last_iterations = 0;
    std::array<ca72::devices::PairWarm, 5> zs;
    double gain = 1.0;
    std::optional<std::pair<double, double>> warp;
    Jacobian jac {};
    std::array<double, STATES> gout {};
    std::tuple<double, size_t, std::array<double, 16>> g_held;
    bool high_pass = false;
    std::pair<double, double> mode { 0.0, 0.0 };

    static Vcf new_ (double sample_rate, size_t oversample);
    void prepare (size_t oversample);
    void set_drive (double gain);
    void set_quality (ca72::voice::Quality q);
    std::optional<LadderBias> table_bias (double i0) const;
    void set_oversample (size_t oversample);
    void reset();
    double latency() const;
    const std::array<double, STATES>& state() const { return y; }
    double tick (double i_bus, double g_bus, double i0);
    double filter_mode (double i_n, double v);
    Drive drive (double i_n, double g_n, double i0);
    double step (double i_n, double g_n, double i0, size_t& iters);
};

std::pair<double, double> table_hermite (std::span<const double> t, double min, double step, double x);
} // namespace ca72::vcf
