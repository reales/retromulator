#pragma once

#include "common.h"
#include "devices.h"

namespace ca72::mna
{
using Node = size_t;
inline constexpr Node GND = 0;

inline constexpr double GMIN = 1e-12;

inline constexpr size_t MAX_SOLVED = 64;

struct Jfet
{
    double vto = 0.0;
    double beta = 0.0;
    double lambda = 0.0;
    double rd = 0.0;
    double rs = 0.0;
    double cgs = 0.0;
    double cgd = 0.0;
    double pb = 0.0;
    double fc = 0.0;
    double is = 0.0;
    double n = 0.0;

    bool operator== (const Jfet&) const = default;

    std::tuple<double, double, double> intrinsic (double vgs, double vds) const;
    std::tuple<double, double, double> forward (double vgs, double vds, double r_s, double r_d) const;
    std::pair<double, std::array<double, 3>> channel (double vg, double vd, double vs) const;
};

inline constexpr Jfet J2N4303 = {
    .vto = -3.5,
    .beta = 5.1e-4,
    .lambda = 0.005,
    .rd = 10.0,
    .rs = 10.0,
    .cgs = 5e-12,
    .cgd = 3e-12,
    .pb = 0.6,
    .fc = 0.5,
    .is = 1e-14,
    .n = 1.0,
};

struct DepletionConsts
{
    std::array<double, 6> series {};
    std::array<double, 3> beyond {};

    static DepletionConsts default_() { return DepletionConsts {}; }

    bool operator== (const DepletionConsts&) const = default;
};

struct Depletion
{
    double cj0 = 0.0;
    double vj = 0.0;
    double m = 0.0;
    double fc = 0.0;

    bool operator== (const Depletion&) const = default;

    std::pair<double, double> charge_near (double v, std::array<double, 2>& r, const DepletionConsts& k) const
    {
        if (v < fc * vj)
        {
            const double a = 1.0 - v / vj;
            const double e = (a - r[0]) / r[0];
            double s;
            if (std::fabs (e) < 1.0 / 256.0)
            {
                const auto& c = k.series;
                s = r[1] * (1.0 + e * (c[0] + e * (c[1] + e * (c[2] + e * (c[3] + e * (c[4] + e * c[5]))))));
            }
            else
            {
                s = ca72::ulp::exp (-m * ca72::ulp::log (a));
                r = { a, s };
            }
            return { cj0 * vj * (1.0 - a * s) / (1.0 - m), cj0 * s };
        }
        return beyond (v, k.beyond);
    }

    DepletionConsts consts() const;
    std::array<double, 3> beyond_consts() const;

    std::pair<double, double> beyond (double v, const std::array<double, 3>& f) const
    {
        const double f1 = f[0], f2 = f[1], f3 = f[2];
        const double x = v - fc * vj;
        const double q = cj0 * (f1 + (f3 * x + m / (2.0 * vj) * (v * v - (fc * vj) * (fc * vj))) / f2);
        return { q, cj0 / f2 * (f3 + m * v / vj) };
    }

    std::pair<double, double> charge (double v) const;
};

struct Part
{
    enum class Tag
    {
        Resistor,
        Capacitor,
        Diode,
        Junction,
        Diffusion,
        Bjt,
        Jfet,
    } tag = Tag::Resistor;

    Node resistor_a = 0;
    Node resistor_b = 0;
    double resistor_r = 0.0;

    Node capacitor_a = 0;
    Node capacitor_b = 0;
    double capacitor_c = 0.0;

    Node diode_a = 0;
    Node diode_k = 0;
    double diode_is = 0.0;
    double diode_nvt = 0.0;
    ca72::mna::Depletion diode_cap {};
    double diode_tt = 0.0;

    Node junction_a = 0;
    Node junction_k = 0;
    ca72::mna::Depletion junction_cap {};

    Node diffusion_a = 0;
    Node diffusion_k = 0;
    double diffusion_is = 0.0;
    double diffusion_vt = 0.0;
    double diffusion_tt = 0.0;

    Node bjt_c = 0;
    Node bjt_b = 0;
    Node bjt_e = 0;
    ca72::devices::BjtAt bjt_m {};
    double bjt_vaf = 0.0;
    bool bjt_pnp = false;

    Node jfet_d = 0;
    Node jfet_g = 0;
    Node jfet_s = 0;
    ca72::mna::Jfet jfet_m {};

    static Part Resistor (Node a, Node b, double r);
    static Part Capacitor (Node a, Node b, double c);
    static Part Diode (Node a, Node k, double is, double nvt, ca72::mna::Depletion cap, double tt);
    static Part Junction (Node a, Node k, ca72::mna::Depletion cap);
    static Part Diffusion (Node a, Node k, double is, double vt, double tt);
    static Part Bjt (Node c, Node b, Node e, ca72::devices::BjtAt m, double vaf, bool pnp);
    static Part Jfet (Node d, Node g, Node s, ca72::mna::Jfet m);
};

struct NoConvergence
{
    size_t iterations = 0;
    double residual = 0.0;

    bool operator== (const NoConvergence&) const = default;
};

using StampNodes = std::tuple<Node, Node, std::array<Node, 3>, size_t>;

struct Slot
{
    uint32_t fa = 0;
    uint32_t fb = 0;
    std::array<uint32_t, 3> ja {};
    std::array<uint32_t, 3> jb {};

    static Slot default_() { return Slot {}; }
};

inline constexpr uint32_t NONE = std::numeric_limits<uint32_t>::max();

inline constexpr size_t FLAT_OPS = 4096;

struct LuProgram
{
    bool ready = false;
    size_t pattern = 0;
    size_t n = 0;
    std::array<uint8_t, MAX_SOLVED> piv {};
    std::vector<uint8_t> cand;
    std::vector<uint8_t> cols;
    std::vector<uint8_t> rows;
    std::array<uint16_t, MAX_SOLVED + 1> cand_off {};
    std::array<uint16_t, MAX_SOLVED + 1> cols_off {};
    std::array<uint16_t, MAX_SOLVED + 1> rows_off {};
    std::vector<std::array<uint64_t, MAX_SOLVED>> snap_rows;
    std::vector<std::array<uint64_t, MAX_SOLVED>> snap_cols;
    bool flat_ok = false;
    std::vector<std::array<uint16_t, 4>> flat_tgt;
    std::array<uint16_t, MAX_SOLVED + 1> flat_tgt_off {};
    std::vector<std::array<uint16_t, 2>> flat_ops;
    std::vector<std::array<uint16_t, 2>> flat_back;
    std::array<uint16_t, MAX_SOLVED + 1> flat_back_off {};

    LuProgram() = default;
    LuProgram (const LuProgram& o);
    LuProgram& operator= (const LuProgram& o);
    LuProgram (LuProgram&&) = default;
    LuProgram& operator= (LuProgram&&) = default;

    static LuProgram new_ (size_t n);
    void flatten (size_t n);
};

double pnjlim (double vnew, double vold, double vt, double vcrit);
double vcrit (double vt, double is);
std::pair<double, double> diffusion_charge (double v, double is, double vt, double tt);
std::pair<double, double> diode_charge (double vd, double is, double nvt, const Depletion& cap, double tt);
std::pair<double, double> diode_charge_e (double vd,
                                          double is,
                                          double nvt,
                                          const Depletion& cap,
                                          const DepletionConsts& k,
                                          double tt,
                                          double e,
                                          std::array<double, 2>& r);
double fetlim (double vnew, double vold, double vto);

using PatternPair = std::pair<std::array<uint64_t, MAX_SOLVED>, std::array<uint64_t, MAX_SOLVED>>;

struct Circuit
{
    std::vector<Part> parts;
    std::vector<double> g;
    std::vector<std::array<std::array<double, 2>, 3>> near;
    std::vector<uint32_t> limiting;
    std::vector<uint32_t> charged;
    std::vector<DepletionConsts> consts;
    size_t nodes = 0;
    size_t held = 0;
    std::vector<double> v_;
    std::vector<double> saved;
    std::vector<double> q;
    std::vector<double> i_prev;
    double theta = 1.0;
    std::vector<std::array<double, 2>> lim;
    std::vector<double> vcrit;
    std::vector<double> jac;
    LuProgram lu;
    std::vector<double> f;
    std::vector<double> dx;
    std::pair<double, double> tolerance { 1e-9, 1e-9 };
    std::optional<double> predict_limit;
    size_t step_iterations = 100;
    size_t iterations = 0;
    uint64_t solves = 0;
    uint64_t total_iterations = 0;
    uint64_t lu_recorded = 0;
    uint64_t lu_recorded_from = 0;
    std::array<uint64_t, 8> iteration_counts {};
    uint64_t nanos = 0;
    uint64_t nanos_load = 0;
    uint64_t nanos_lu = 0;
    std::vector<double> prev;
    std::vector<double> prev2;
    double prev_h = 0.0;
    double prev2_h = 0.0;
    std::vector<std::array<double, 3>> qc;
    bool qc_ok = false;
    std::vector<std::array<Slot, 3>> slots;
    std::array<PatternPair, 2> pattern {};
    bool slots_ready = false;

    static Circuit new_ (size_t nodes, size_t held);

    Node node();
    void add_bjt (Node c, Node b, Node e, const ca72::devices::Bjt& q, double celsius, bool pnp);
    void add_jfet (Node d, Node g, Node s, const Jfet& j);
    size_t add (Part p);

    double v (Node n) const { return v_[n]; }

    void set (Node n, double v);
    void set_theta (double theta);
    std::pair<double, double> charge_current (size_t idx, double q, double h) const;
    void set_resistance (size_t part, double r);
    std::optional<size_t> free (Node n) const;
    static std::array<std::optional<StampNodes>, 3> stamp_nodes (const Part& p);
    void build_slots();
    void stamp_at (size_t idx, size_t s, double i, std::span<const double> g);
    std::pair<double, double> charge (const Part& p) const;
    bool load (std::optional<double> h);
    void linear_solve (size_t pattern);
    size_t replay_flat (size_t n);
    void back_flat (size_t n);
    ca72::Result<size_t, NoConvergence> newton (std::optional<double> h, size_t max_iter);
    ca72::Result<size_t, NoConvergence> dc();
    ca72::Result<size_t, NoConvergence> step (double h);
    void accept (std::optional<double> h);
    std::pair<std::vector<double>, std::vector<double>> residual_and_jacobian (std::optional<double> h);
    std::tuple<std::vector<double>, std::vector<double>, std::vector<double>> small_signal (Node input);
    std::optional<size_t> solved_index (Node n) const;
    std::pair<size_t, size_t> size() const;
    std::pair<size_t, size_t> extent() const;
    size_t linear_nodes() const;
    std::vector<size_t> components() const;
    std::vector<std::pair<const char*, size_t>> census() const;
};
} // namespace ca72::mna
