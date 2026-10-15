#pragma once

#include "common.h"
#include "devices.h"
#include "vcf.h"

namespace ca72::vca
{
inline constexpr ca72::devices::Bjt Q2N4058 {
    .is = 5e-15,
    .bf = 200.0,
    .ise = 1e-14,
    .ne = 1.5,
    .vaf = 60.0,
    .ikf = 0.1,
    .br = 4.0,
    .rb = 20.0,
    .re = 0.5,
    .rc = 1.0,
    .cje = 6e-12,
    .vje = 0.75,
    .mje = 0.33,
    .cjc = 5e-12,
    .vjc = 0.75,
    .mjc = 0.33,
    .tf = 0.6e-9,
    .xti = 3.0,
    .xtb = 1.5,
    .eg = 1.11,
    .tnom = 25.0,
};

struct VcaCircuit
{
    double r2;
    double c6;
    double r34;
    double r19;
    double r14;
    double r14_pos;
    double r18;
    double r12;
    double r12_pos;
    double r35;
    double r59;
    double r37;
    double r43;
    double r33;
    double r51;
    double r42;
    double r44;
    double r_j3;
    double ext;
    double r40;
    double c8;
    double r9;
    double r8;
    double r30;
    double r29;
    double c2;
    double r77;
    double load;
    double r10;
    double c4;
    double r11;
    double r13;
    double r22;
    double r36;
    double c15;
    double vp;
    double vn;
    ca72::devices::Bjt pair;
    ca72::devices::Bjt out;

    bool operator== (const VcaCircuit&) const = default;

    static VcaCircuit default_()
    {
        return VcaCircuit {
            .r2 = 160e3,
            .c6 = 0.33e-6,
            .r34 = 820.0,
            .r19 = 100.0,
            .r14 = 25.0,
            .r14_pos = 0.5,
            .r18 = 330.0,
            .r12 = 100.0,
            .r12_pos = 0.5,
            .r35 = 1e3,
            .r59 = 68e3,
            .r37 = 6.8e3,
            .r43 = 180e3,
            .r33 = 470.0,
            .r51 = 33e3,
            .r42 = 3.3e3,
            .r44 = 150e3,
            .r_j3 = 33e3,
            .ext = 10.0,
            .r40 = 10e3,
            .c8 = 0.1e-6,
            .r9 = 62.0,
            .r8 = 4.7,
            .r30 = 680.0,
            .r29 = 680.0,
            .c2 = 10e-6,
            .r77 = 1e3,
            .load = 10e3,
            .r10 = 120.0,
            .c4 = 220e-6,
            .r11 = 150.0,
            .r13 = 120.0,
            .r22 = 120.0,
            .r36 = 510.0,
            .c15 = 220e-6,
            .vp = 10.0,
            .vn = -10.0,
            .pair = ca72::vcf::TIS97,
            .out = Q2N4058,
        };
    }

    VcaCircuit reference_trims() const;
    VcaCircuit calibrated() const;
};

inline constexpr size_t MAX_PASSES = 30;
inline constexpr double REFRESH_V = 2e-3;
inline constexpr double REFRESH_EXT_V = 2e-3;
inline constexpr size_t FAST_STEPS = 60;
inline constexpr double CONTOUR_FULL = 5.0;
inline constexpr double R14_REFERENCE = 0.564;
inline constexpr double R12_REFERENCE = 0.496;

struct PairBias
{
    double tail = 0.0;
    double alpha = 0.0;
    double ib0 = 0.0;
    double b = 0.0;
    double vt = 0.0;
    double vt0 = 0.0;
    double q1 = 0.0;

    bool operator== (const PairBias&) const = default;

    static PairBias default_() { return PairBias {}; }
    PairBias lerp (const PairBias& o, double k) const;
};

PairBias pair_bias (const ca72::devices::Bjt& q, double celsius, double tail, double vbc);

struct VcaBias
{
    double i_a = 0.0;
    double i_b = 0.0;
    double i_c = 0.0;
    double nm = 0.0;
    double nn = 0.0;
    double draw_nm = 0.0;
    double draw_nn = 0.0;
    double draw_bb = 0.0;
    double ib1 = 0.0;

    bool operator== (const VcaBias&) const = default;

    static VcaBias default_() { return VcaBias {}; }
    VcaBias lerp (const VcaBias& o, double k) const;
};

struct Path
{
    double o17 = 0.0;
    double v_c14 = 0.0;
    double ib16 = 0.0;
    std::array<double, 3> stages {};
    double out = 0.0;

    bool operator== (const Path&) const = default;

    static Path default_() { return Path {}; }
};

struct PairDrive
{
    double alpha = 0.0;
    double ib0 = 0.0;
    double b = 0.0;
    double vt = 0.0;
    double vt0 = 0.0;

    bool operator== (const PairDrive&) const = default;

    static PairDrive default_() { return PairDrive {}; }
};

struct VcaDrive
{
    double i_a = 0.0;
    double i_b = 0.0;
    double i_c = 0.0;
    double nm = 0.0;
    double bb = 0.0;
    std::array<PairDrive, 3> pairs {};

    static constexpr size_t LEN = 20;

    bool operator== (const VcaDrive&) const = default;

    static VcaDrive default_() { return VcaDrive {}; }
    std::array<double, LEN> to_array() const;
    static VcaDrive from_array (const std::array<double, LEN>& x);
};

inline constexpr size_t DRAWN = VcaDrive::LEN + 4;
inline constexpr size_t ENTRY = 3 * DRAWN + 2;

struct DriveTable
{
    std::vector<std::array<double, ENTRY>> empty;
    std::vector<std::array<double, ENTRY>> plugged;

    static std::shared_ptr<const DriveTable> for_circuit (VcaCircuit circuit, double celsius);
    static DriveTable make (VcaCircuit circuit, double celsius);
    std::array<double, ENTRY> lookup (double cont, std::optional<double> ext) const;
};

struct Vca
{
    VcaCircuit circuit;
    double celsius = 25.0;
    double h = 0.0;
    double q6 = 0.0;
    double q8 = 0.0;
    double q2 = 0.0;
    double s4 = 0.0;
    double bb = 0.0;
    double a440 = 0.0;
    // Rust's field `bias` (renamed: it clashes with the method bias()).
    VcaBias bias_;
    std::array<double, 2> x18 {};
    std::array<double, 2> x21 {};
    std::array<double, 2> x1 {};
    std::array<double, 3> xp {};
    std::array<double, 3> vbc {};
    std::array<PairBias, 3> pairs {};
    double ib16_rest = 0.0;
    double bb_rest = 0.0;
    size_t control_every = 1;
    size_t count = 0;
    std::pair<VcaBias, std::array<PairBias, 3>> from;
    std::pair<VcaBias, std::array<PairBias, 3>> to;
    double solved_cont = 0.0;
    std::pair<double, double> solved_ext { 0.0, 0.0 };
    std::pair<double, double> e1_drop { 0.0, 0.0 };
    std::pair<double, double> e2_drop { 0.0, 0.0 };
    mutable std::array<ca72::devices::PairWarm, 3> warm;
    bool plain_signal = false;
    mutable std::optional<double> out_warm;
    std::optional<std::shared_ptr<const DriveTable>> potato;
    bool potato_on = false;

    static Vca new_ (VcaCircuit circuit, double rate, double cont);
    void settle (double cont);
    std::pair<double, double> chain_equilibrium() const;
    void prepare_potato();
    void solve_bias (double cont);
    void fast_bias (double cont);
    Path path (const VcaDrive& d, double v_in, double q6, double q8, double q2, std::optional<double> w) const;
    double rest_output (double cont);
    double static_output (double w, double cont);
    double tick (double v_in, double cont);
    VcaDrive drive() const;
    VcaDrive control (double cont);
    double signal (double v_in, const VcaDrive& d);
    void step_chain();
    std::pair<double, std::array<double, 3>> probe (double v_in) const;
    std::array<double, 4> chain() const;
    VcaBias bias() const { return bias_; }
};
} // namespace ca72::vca
