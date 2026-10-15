#include "vca.h"

#include <bit>
#include <mutex>

#include "ulp.h"
#include "unconverged.h"

namespace ca72::vca
{
using ca72::devices::Bjt;
using ca72::devices::PairWarm;
using ca72::devices::TailPair;

VcaCircuit VcaCircuit::reference_trims() const
{
    VcaCircuit c = *this;
    c.r14_pos = R14_REFERENCE;
    c.r12_pos = R12_REFERENCE;
    return c;
}

VcaCircuit VcaCircuit::calibrated() const
{
    VcaCircuit self = *this;
    auto leak = [] (VcaCircuit c, double cont)
    {
        double a = Vca::new_ (c, 48e3, cont).rest_output (cont);
        VcaCircuit c2 = c;
        c2.ext = c.ext - 1.0;
        double b = Vca::new_ (c2, 48e3, cont).rest_output (cont);
        return a - b;
    };
    auto null = [&] (VcaCircuit& c, double cont, void (*set) (VcaCircuit&, double))
    {
        double lo = 0.0;
        double hi = 1.0;
        set (c, lo);
        double f_lo = leak (c, cont);
        for (int k = 0; k < 40; ++k)
        {
            double mid = 0.5 * (lo + hi);
            set (c, mid);
            if ((leak (c, cont) > 0.0) == (f_lo > 0.0))
                lo = mid;
            else
                hi = mid;
        }
        set (c, 0.5 * (lo + hi));
    };
    null (self, 0.0, [] (VcaCircuit& c, double x) { c.r12_pos = x; });
    null (self, CONTOUR_FULL, [] (VcaCircuit& c, double x) { c.r14_pos = x; });
    return self;
}

PairBias PairBias::lerp (const PairBias& o, double k) const
{
    auto l = [k] (double a, double b) { return a + (b - a) * k; };
    return PairBias {
        .tail = l (tail, o.tail),
        .alpha = l (alpha, o.alpha),
        .ib0 = l (ib0, o.ib0),
        .b = l (b, o.b),
        .vt = l (vt, o.vt),
        .vt0 = l (vt0, o.vt0),
        .q1 = l (q1, o.q1),
    };
}

PairBias pair_bias (const Bjt& q, double celsius, double tail, double vbc)
{
    auto m = q.at (celsius);
    double half = 0.5 * std::fmax (tail, 1e-15);
    double ib0 = m.base_current (half, vbc, q.vaf);
    double slope = m.base_slope (half, vbc, q.vaf);
    return PairBias {
        .tail = tail,
        .alpha = half / (half + ib0),
        .ib0 = ib0,
        .b = slope * half,
        .vt = m.vt / m.gm_factor (half, vbc, q.vaf),
        .vt0 = m.vt,
        .q1 = 1.0 / (1.0 - vbc / q.vaf),
    };
}

VcaBias VcaBias::lerp (const VcaBias& o, double k) const
{
    auto l = [k] (double a, double b) { return a + (b - a) * k; };
    return VcaBias {
        .i_a = l (i_a, o.i_a),
        .i_b = l (i_b, o.i_b),
        .i_c = l (i_c, o.i_c),
        .nm = l (nm, o.nm),
        .nn = l (nn, o.nn),
        .draw_nm = l (draw_nm, o.draw_nm),
        .draw_nn = l (draw_nn, o.draw_nn),
        .draw_bb = l (draw_bb, o.draw_bb),
        .ib1 = l (ib1, o.ib1),
    };
}

std::array<double, VcaDrive::LEN> VcaDrive::to_array() const
{
    std::array<double, LEN> x {};
    x[0] = i_a;
    x[1] = i_b;
    x[2] = i_c;
    x[3] = nm;
    x[4] = bb;
    for (size_t k = 0; k < pairs.size(); ++k)
    {
        const auto& p = pairs[k];
        x[5 + 5 * k] = p.alpha;
        x[6 + 5 * k] = p.ib0;
        x[7 + 5 * k] = p.b;
        x[8 + 5 * k] = p.vt;
        x[9 + 5 * k] = p.vt0;
    }
    return x;
}

VcaDrive VcaDrive::from_array (const std::array<double, LEN>& x)
{
    VcaDrive d;
    d.i_a = x[0];
    d.i_b = x[1];
    d.i_c = x[2];
    d.nm = x[3];
    d.bb = x[4];
    for (size_t k = 0; k < 3; ++k)
        d.pairs[k] = PairDrive {
            .alpha = x[5 + 5 * k],
            .ib0 = x[6 + 5 * k],
            .b = x[7 + 5 * k],
            .vt = x[8 + 5 * k],
            .vt0 = x[9 + 5 * k],
        };
    return d;
}

using Axis = std::tuple<double, double, size_t>;
static constexpr Axis CONT_EMPTY { -1.0, 9.0, 201 };
static constexpr Axis CONT_PLUGGED { -1.0, 9.0, 101 };
static constexpr Axis EXT_PLUGGED { -1.0, 11.0, 121 };

static std::pair<size_t, double> place (double x, Axis axis)
{
    auto [from, to, n] = axis;
    double f = ca72::clamp ((x - from) / (to - from) * (double) (n - 1), 0.0, (double) (n - 1));
    size_t i = std::min (f >= 0.0 ? (size_t) f : (size_t) 0, n - 2);
    return { i, f - (double) i };
}

static std::array<double, ENTRY> lerp_entry (const std::array<double, ENTRY>& a, const std::array<double, ENTRY>& b, double t)
{
    std::array<double, ENTRY> r;
    for (size_t k = 0; k < ENTRY; ++k)
        r[k] = a[k] + (b[k] - a[k]) * t;
    return r;
}

std::shared_ptr<const DriveTable> DriveTable::for_circuit (VcaCircuit circuit, double celsius)
{
    using Made = std::vector<std::tuple<VcaCircuit, uint64_t, std::shared_ptr<const DriveTable>>>;
    static std::mutex MADE_MUTEX;
    static Made MADE;
    std::lock_guard<std::mutex> lock (MADE_MUTEX);
    for (const auto& [c, k, t] : MADE)
        if (c == circuit && k == std::bit_cast<uint64_t> (celsius))
            return t;
    auto t = std::make_shared<const DriveTable> (DriveTable::make (circuit, celsius));
    MADE.emplace_back (circuit, std::bit_cast<uint64_t> (celsius), t);
    return t;
}

DriveTable DriveTable::make (VcaCircuit circuit, double celsius)
{
    Vca v = Vca::new_ (circuit, 48e3, std::get<0> (CONT_EMPTY));
    v.celsius = celsius;
    auto drawn = [] (Vca& v, double cont)
    {
        v.solve_bias (cont);
        std::array<double, DRAWN> e {};
        auto d = v.drive().to_array();
        std::copy (d.begin(), d.end(), e.begin());
        const VcaBias b = v.bias_;
        e[VcaDrive::LEN] = b.ib1;
        e[VcaDrive::LEN + 1] = b.draw_nm;
        e[VcaDrive::LEN + 2] = b.draw_nn;
        e[VcaDrive::LEN + 3] = b.draw_bb;
        return e;
    };
    auto settled = [&] (Vca& v, double cont)
    {
        for (int k = 0; k < 60; ++k)
        {
            v.solve_bias (cont);
            auto [s4, bb] = v.chain_equilibrium();
            double moved = std::fabs (s4 - v.s4) + std::fabs (bb - v.bb);
            v.s4 = s4;
            v.bb = bb;
            if (moved < 1e-9)
                break;
        }
        const auto base = drawn (v, cont);
        constexpr double D = 1e-4;
        auto slope = [&] (const Vca& v, double s4, double bb)
        {
            Vca w = v;
            w.s4 = v.s4 + s4;
            w.bb = v.bb + bb;
            auto e = drawn (w, cont);
            std::array<double, DRAWN> r;
            for (size_t k = 0; k < DRAWN; ++k)
                r[k] = (e[k] - base[k]) / D;
            return r;
        };
        const auto d_s4 = slope (v, D, 0.0);
        const auto d_bb = slope (v, 0.0, D);
        std::array<double, ENTRY> e {};
        std::copy (base.begin(), base.end(), e.begin());
        e[DRAWN] = v.s4;
        e[DRAWN + 1] = v.bb;
        std::copy (d_s4.begin(), d_s4.end(), e.begin() + DRAWN + 2);
        std::copy (d_bb.begin(), d_bb.end(), e.begin() + 2 * DRAWN + 2);
        return e;
    };
    auto at = [] (Axis axis, size_t i)
    {
        auto [from, to, n] = axis;
        return from + (to - from) * (double) i / (double) (n - 1);
    };
    std::vector<std::array<double, ENTRY>> empty;
    empty.reserve (std::get<2> (CONT_EMPTY));
    for (size_t i = 0; i < std::get<2> (CONT_EMPTY); ++i)
        empty.push_back (settled (v, at (CONT_EMPTY, i)));
    v.circuit.r_j3 = 0.0;
    std::vector<std::array<double, ENTRY>> plugged;
    plugged.reserve (std::get<2> (EXT_PLUGGED) * std::get<2> (CONT_PLUGGED));
    for (size_t j = 0; j < std::get<2> (EXT_PLUGGED); ++j)
    {
        v.circuit.ext = at (EXT_PLUGGED, j);
        for (size_t i = 0; i < std::get<2> (CONT_PLUGGED); ++i)
            plugged.push_back (settled (v, at (CONT_PLUGGED, i)));
    }
    return DriveTable { .empty = std::move (empty), .plugged = std::move (plugged) };
}

std::array<double, ENTRY> DriveTable::lookup (double cont, std::optional<double> ext) const
{
    if (! ext.has_value())
    {
        auto [i, t] = place (cont, CONT_EMPTY);
        return lerp_entry (empty[i], empty[i + 1], t);
    }
    const auto ic = place (cont, CONT_PLUGGED);
    const size_t i = ic.first;
    const double t = ic.second;
    auto [j, u] = place (*ext, EXT_PLUGGED);
    const size_t n = std::get<2> (CONT_PLUGGED);
    auto row = [&] (size_t jj) { return lerp_entry (plugged[jj * n + i], plugged[jj * n + i + 1], t); };
    return lerp_entry (row (j), row (j + 1), u);
}

Vca Vca::new_ (VcaCircuit circuit, double rate, double cont)
{
    Vca v;
    v.circuit = circuit;
    v.celsius = 25.0;
    v.h = 1.0 / rate;
    v.q6 = 0.0;
    v.q8 = 0.0;
    v.q2 = 0.0;
    v.a440 = 0.0;
    v.s4 = 0.0;
    v.bb = 0.0;
    v.bias_ = VcaBias::default_();
    v.x18 = { 0.6, -1.0 };
    v.x21 = { 0.6, -1.0 };
    v.x1 = { 0.6, -1.0 };
    v.xp = { 0.6, 0.6, 0.6 };
    v.vbc = { -1.0, -1.0, -4.5 };
    v.pairs = { PairBias::default_(), PairBias::default_(), PairBias::default_() };
    v.ib16_rest = 0.0;
    v.bb_rest = 0.0;
    v.control_every = std::max ((size_t) std::round (rate / 3000.0), (size_t) 1);
    v.count = 0;
    v.from = { VcaBias::default_(), v.pairs };
    v.to = { VcaBias::default_(), v.pairs };
    v.solved_cont = std::numeric_limits<double>::quiet_NaN();
    v.solved_ext = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
    v.e1_drop = { 0.6, 1e-4 };
    v.e2_drop = { 0.6, 1e-4 };
    v.warm = { PairWarm::COLD, PairWarm::COLD, PairWarm::COLD };
    v.plain_signal = false;
    v.out_warm = std::nullopt;
    v.potato = std::nullopt;
    v.potato_on = false;
    v.settle (cont);
    return v;
}

void Vca::settle (double cont)
{
    const VcaCircuit c = circuit;
    double total = c.r10 + c.r11 + c.r13 + c.r22 + c.r36;
    s4 = c.vp * c.r10 / total;
    bb = c.vp * c.r36 / total;
    for (int k = 0; k < 60; ++k)
    {
        solve_bias (cont);
        auto [s4n, bbn] = chain_equilibrium();
        s4 = s4n;
        bb = bbn;
    }
    solve_bias (cont);
    const PairBias p = pairs[0];
    ib16_rest = p.ib0;
    bb_rest = bb;
    q6 = c.r34 * p.ib0;
    for (int k = 0; k < 8; ++k)
    {
        Path pa = path (drive(), 0.0, q6, q8 + a440, q2, std::nullopt);
        q8 = pa.v_c14;
        q2 = pa.o17;
    }
    count = 0;
    from = { bias_, pairs };
    to = from;
    solved_cont = cont;
    solved_ext = { circuit.ext, circuit.r_j3 };
}

std::pair<double, double> Vca::chain_equilibrium() const
{
    const VcaCircuit c = circuit;
    const VcaBias b = bias_;
    double r_mid = c.r11 + c.r13 + c.r22;
    double k = c.r36;
    double lhs0 = c.vp - r_mid * b.ib1 + (c.r13 + c.r22) * b.draw_nm + c.r22 * b.draw_nn;
    double rhs0 = k * (b.ib1 - b.draw_nm - b.draw_nn - b.draw_bb);
    double s4n = (lhs0 - rhs0) / (1.0 + r_mid / c.r10 + k / c.r10);
    double j = s4n / c.r10 + b.ib1;
    double bbn = k * (j - b.draw_nm - b.draw_nn - b.draw_bb);
    return { s4n, bbn };
}

void Vca::prepare_potato()
{
    potato = DriveTable::for_circuit (circuit, celsius);
}

void Vca::solve_bias (double cont)
{
    const VcaCircuit c = circuit;
    const Bjt tis = c.pair;
    const Bjt pnp = c.out;
    const auto m_tis = tis.at (celsius);
    const auto m_pnp = pnp.at (celsius);
    double nl = c.vp - s4;
    double bbv = bb;
    VcaBias b = bias_;
    double r_mid = c.r11 + c.r13 + c.r22;
    double ra2 = c.r19 + c.r14 * c.r14_pos;
    double rb2 = c.r19 + c.r14 * (1.0 - c.r14_pos);
    double ra3 = c.r18 + c.r12 * c.r12_pos;
    double rb3 = c.r18 + c.r12 * (1.0 - c.r12_pos);
    std::array<double, 3> vbcs = vbc;
    double last_move = std::numeric_limits<double>::infinity();
    size_t passes = 0;
    while (last_move > 1e-12 && passes < MAX_PASSES)
    {
        passes += 1;
        const double before0 = b.i_a, before1 = b.i_b, before2 = b.i_c;
        const PairBias p1 = pair_bias (tis, celsius, b.i_a, vbcs[0]);
        const PairBias p2 = pair_bias (tis, celsius, b.i_b, vbcs[1]);
        const PairBias p3 = pair_bias (pnp, celsius, b.i_c, vbcs[2]);
        b.draw_nn = p1.alpha * b.i_a + 2.0 * p2.ib0;
        b.draw_nm = p2.alpha * b.i_b - 2.0 * p3.ib0;
        b.draw_bb = 2.0 * p1.ib0;
        double j = (nl - bbv + (c.r13 + c.r22) * b.draw_nm + c.r22 * b.draw_nn) / r_mid;
        b.nm = nl - c.r11 * j;
        b.nn = b.nm - c.r13 * (j - b.draw_nm);
        double r_e18 = 1.0 / (1.0 / c.r37 + 1.0 / c.r43);
        const TailPair tp1 {
            .tail = { m_tis, tis },
            .pair = { m_tis, tis },
            .pnp = false,
            .vb0 = cont,
            .rb = c.r59,
            .ve0 = r_e18 * c.vn / c.r43,
            .re = r_e18,
            .mid = bbv,
            .pair_vbc = vbcs[0],
            .r_emitter = 0.0,
            .r_collector = c.r35,
        };
        {
            auto [x, ic, ib_, e1] = ca72::devices::solve_tail_pair (tp1, { x18[0], x18[1], xp[0] }, 60);
            x18 = { x[0], x[1] };
            xp[0] = x[2];
            b.i_a = std::fmax (ic, 0.0);
            e1_drop = { bbv - e1, 0.5 * std::fmax (b.i_a, 1e-15) };
        }
        double v_th = c.ext * c.r51 / (c.r_j3 + c.r51);
        double r_th = c.r_j3 * c.r51 / (c.r_j3 + c.r51);
        double v_c16 = b.nn - ra2 * (p1.alpha * 0.5 * b.i_a + p2.ib0);
        double v_c15 = b.nn - rb2 * (p1.alpha * 0.5 * b.i_a + p2.ib0);
        double mid2 = 0.5 * (v_c16 + v_c15);
        double r_e21 = 1.0 / (1.0 / c.r42 + 1.0 / c.r44);
        TailPair tp2 = tp1;
        tp2.vb0 = v_th;
        tp2.rb = r_th;
        tp2.ve0 = r_e21 * c.vn / c.r44;
        tp2.re = r_e21;
        tp2.mid = mid2;
        tp2.pair_vbc = vbcs[1];
        tp2.r_collector = c.r33;
        {
            auto [x, ic, ib_, e2] = ca72::devices::solve_tail_pair (tp2, { x21[0], x21[1], xp[1] }, 60);
            x21 = { x[0], x[1] };
            xp[1] = x[2];
            b.i_b = std::fmax (ic, 0.0);
            e2_drop = { mid2 - e2, 0.5 * std::fmax (b.i_b, 1e-15) };
        }
        double v_c14 = b.nm - ra3 * (p2.alpha * 0.5 * b.i_b - p3.ib0);
        double v_c13 = b.nm - rb3 * (p2.alpha * 0.5 * b.i_b - p3.ib0);
        const TailPair tp3 {
            .tail = { m_pnp, pnp },
            .pair = { m_pnp, pnp },
            .pnp = true,
            .vb0 = nl,
            .rb = 0.0,
            .ve0 = c.vp,
            .re = c.r9,
            .mid = 0.5 * (v_c14 + v_c13),
            .pair_vbc = vbcs[2],
            .r_emitter = c.r8,
            .r_collector = 0.0,
        };
        double half_c;
        {
            auto [x, ic, ib, e_] = ca72::devices::solve_tail_pair (tp3, { x1[0], x1[1], xp[2] }, 60);
            half_c = 0.5 * std::fmax (b.i_c, 1e-15);
            x1 = { x[0], x[1] };
            xp[2] = x[2];
            b.i_c = std::fmax (ic, 0.0);
            b.ib1 = ib;
        }
        vbcs = { bbv - v_c16, v_c16 - v_c14, c.r29 * p3.alpha * half_c - v_c13 };
        double scale = std::fabs (b.i_a) + std::fabs (b.i_b) + std::fabs (b.i_c) + 1e-15;
        last_move = (std::fabs (b.i_a - before0) + std::fabs (b.i_b - before1) + std::fabs (b.i_c - before2)) / scale;
    }
    if (last_move > 1e-12)
        ca72::unconverged::note (ca72::unconverged::Solver::VcaBias);
    vbc = vbcs;
    bias_ = b;
    pairs = {
        pair_bias (tis, celsius, b.i_a, vbcs[0]),
        pair_bias (tis, celsius, b.i_b, vbcs[1]),
        pair_bias (pnp, celsius, b.i_c, vbcs[2]),
    };
}

void Vca::fast_bias (double cont)
{
    const VcaCircuit c = circuit;
    const Bjt tis = c.pair;
    const auto m = tis.at (celsius);
    const double nl = c.vp - s4;
    const double r_mid = c.r11 + c.r13 + c.r22;
    const double ra2 = c.r19 + c.r14 * c.r14_pos;
    const double rb2 = c.r19 + c.r14 * (1.0 - c.r14_pos);
    auto scale = [] (PairBias p, double tail)
    {
        double k = p.tail > 1e-12 ? tail / p.tail : 1.0;
        PairBias r = p;
        r.tail = tail;
        r.ib0 = p.ib0 * k;
        r.b = p.b * k;
        return r;
    };
    const double drop0 = e1_drop.first;
    const double half0 = e1_drop.second;
    const double half = 0.5 * std::fmax (bias_.i_a, 1e-15);
    const double e1 = bb - (drop0 + m.vt * ca72::ulp::log (half / half0));
    auto q18 = [&] (double ic, double ib)
    {
        double ie = ic + ib;
        double ve = (ie + c.vn / c.r43) / (1.0 / c.r37 + 1.0 / c.r43);
        return std::tuple<double, double, double> { cont - c.r59 * ib, ve, e1 - c.r35 * ic };
    };
    double i_a;
    {
        auto [x, ic, ib_] = ca72::devices::solve_junctions_limited (m, tis, false, x18, q18, FAST_STEPS);
        x18 = x;
        i_a = std::fmax (ic, 0.0);
    }
    pairs[0] = scale (pairs[0], i_a);
    const PairBias p1 = pairs[0];
    const PairBias p3 = pairs[2];
    VcaBias& b = bias_;
    b.i_a = i_a;
    b.draw_bb = 2.0 * p1.ib0;
    const double bbv = bb;
    auto nodes = [&] (VcaBias& bb_, const PairBias& p2)
    {
        bb_.draw_nn = p1.alpha * bb_.i_a + 2.0 * p2.ib0;
        bb_.draw_nm = p2.alpha * bb_.i_b - 2.0 * p3.ib0;
        double j = (nl - bbv + (c.r13 + c.r22) * bb_.draw_nm + c.r22 * bb_.draw_nn) / r_mid;
        bb_.nm = nl - c.r11 * j;
        bb_.nn = bb_.nm - c.r13 * (j - bb_.draw_nm);
    };
    nodes (b, pairs[1]);
    const double drop2 = e2_drop.first;
    const double half2 = e2_drop.second;
    const PairBias p2 = pairs[1];
    const double v_c = b.nn - 0.5 * (ra2 + rb2) * (p1.alpha * 0.5 * i_a + p2.ib0);
    const double e2 = v_c - (drop2 + m.vt * ca72::ulp::log (0.5 * std::fmax (b.i_b, 1e-15) / half2));
    const double v_th = c.ext * c.r51 / (c.r_j3 + c.r51);
    const double r_th = c.r_j3 * c.r51 / (c.r_j3 + c.r51);
    auto q21 = [&] (double ic, double ib)
    {
        double ie = ic + ib;
        double ve = (ie + c.vn / c.r44) / (1.0 / c.r42 + 1.0 / c.r44);
        return std::tuple<double, double, double> { v_th - r_th * ib, ve, e2 - c.r33 * ic };
    };
    {
        auto [x, ic, ib_] = ca72::devices::solve_junctions_limited (m, tis, false, x21, q21, FAST_STEPS);
        x21 = x;
        b.i_b = std::fmax (ic, 0.0);
    }
    pairs[1] = scale (p2, b.i_b);
    nodes (b, pairs[1]);
}

Path Vca::path (const VcaDrive& d, double v_in, double q6, double q8, double q2, std::optional<double> w) const
{
    const VcaCircuit c = circuit;
    const VcaDrive& b = d;
    const PairDrive p1 = d.pairs[0];
    const PairDrive p2 = d.pairs[1];
    const PairDrive p3 = d.pairs[2];
    const Bjt tis = c.pair;
    const Bjt pnp = c.out;
    double k_in = c.r34 / (c.r2 + c.r34);
    double r_p = c.r2 * c.r34 / (c.r2 + c.r34);
    double a1, r_src;
    if (w.has_value())
    {
        a1 = *w;
        r_src = 0.0;
    }
    else
    {
        a1 = k_in * (v_in - q6 - (d.bb - bb_rest)) - r_p * p1.ib0;
        r_src = r_p;
    }
    double b1 = r_src * p1.b + 2.0 * tis.rb * p1.b + tis.re * b.i_a;
    std::array<PairWarm, 3> wm = warm;
    double tol = plain_signal ? 1e-9 : 1e-15;
    double u1 = ca72::devices::degenerated_warm_to (a1, b1, 2.0 * p1.vt, wm[0], tol).first;
    double ic16 = p1.alpha * 0.5 * b.i_a * (1.0 + u1);
    double ic15 = p1.alpha * 0.5 * b.i_a * (1.0 - u1);
    double ib16 = p1.ib0 + p1.b * u1;
    double ra2 = c.r19 + c.r14 * c.r14_pos;
    double rb2 = c.r19 + c.r14 * (1.0 - c.r14_pos);
    double a2 = -ra2 * ic16 + rb2 * ic15 - (ra2 - rb2) * p2.ib0;
    double b2 = (ra2 + rb2) * p2.b + 2.0 * tis.rb * p2.b + tis.re * b.i_b;
    double u2 = ca72::devices::degenerated_warm_to (a2, b2, 2.0 * p2.vt, wm[1], tol).first;
    double ic14 = p2.alpha * 0.5 * b.i_b * (1.0 + u2);
    double ic13 = p2.alpha * 0.5 * b.i_b * (1.0 - u2);
    double ra3 = c.r18 + c.r12 * c.r12_pos;
    double rb3 = c.r18 + c.r12 * (1.0 - c.r12_pos);
    double ra3p = ra3 * c.r40 / (ra3 + c.r40);
    double v14_open = (c.r40 * (b.nm - ra3 * ic14) + ra3 * q8) / (c.r40 + ra3);
    double a3 = v14_open - b.nm + rb3 * ic13 + (ra3p - rb3) * p3.ib0;
    double b3 = (ra3p + rb3) * p3.b + 2.0 * pnp.rb * p3.b + (pnp.re + c.r8) * b.i_c;
    double r_out = c.r77 + c.load;
    double g_o = 1.0 / c.r29 + 1.0 / r_out;
    double v_c13 = b.nm - rb3 * (ic13 - p3.ib0);
    double v_c14_rest = v14_open + ra3p * p3.ib0;
    double half_c = 0.5 * b.i_c;
    double o17 = c.r29 * p3.alpha * half_c;
    double v_c12 = c.r30 * p3.alpha * half_c;
    double u3 = 0.0;
    if (plain_signal && out_warm.has_value() && ! w.has_value())
    {
        auto o17_at = [&] (double u) { return (p3.alpha * half_c * (1.0 + u) + q2 / r_out) / g_o; };
        auto v_c12_at = [&] (double u) { return c.r30 * p3.alpha * half_c * (1.0 - u); };
        u3 = *out_warm;
        for (int k = 0; k < 2; ++k)
        {
            double o = o17_at (u3);
            double v = v_c12_at (u3);
            double qb17 = 1.0 / (1.0 - (o - v_c13) / pnp.vaf);
            double qb12 = 1.0 / (1.0 - (v - v_c14_rest) / pnp.vaf);
            double delta = p3.vt0 * ca72::ulp::log (qb12 / qb17);
            auto [u1n, du_da] = ca72::devices::degenerated_warm_to (a3 + delta, b3, 2.0 * p3.vt, wm[2], tol);
            double d_delta = p3.vt0
                             * (qb12 / pnp.vaf * (-c.r30 * p3.alpha * half_c)
                                - qb17 / pnp.vaf * (p3.alpha * half_c / g_o));
            double step = (u1n - u3) / (1.0 - du_da * d_delta);
            u3 += step;
            if (std::fabs (step) < 1e-3)
                break;
        }
        o17 = o17_at (u3);
    }
    else
    {
        for (int k = 0; k < 4; ++k)
        {
            double qb17 = 1.0 / (1.0 - (o17 - v_c13) / pnp.vaf);
            double qb12 = 1.0 / (1.0 - (v_c12 - v_c14_rest) / pnp.vaf);
            double delta = p3.vt0 * ca72::ulp::log (qb12 / qb17);
            u3 = ca72::devices::degenerated_warm_to (a3 + delta, b3, 2.0 * p3.vt, wm[2], tol).first;
            double ic17 = p3.alpha * half_c * (1.0 + u3);
            double ic12 = p3.alpha * half_c * (1.0 - u3);
            o17 = (ic17 + q2 / r_out) / g_o;
            v_c12 = c.r30 * ic12;
        }
    }
    warm = wm;
    double ib12 = p3.ib0 - p3.b * u3;
    double ib17 = p3.ib0 + p3.b * u3;
    double v_c14 = v14_open + ra3p * ib12;
    double w1 = a1 + r_src * (p1.ib0 - ib16);
    double ib14 = p2.ib0 + p2.b * u2;
    double ib13 = p2.ib0 - p2.b * u2;
    double d1 = -ra2 * (ic16 + ib14) + rb2 * (ic15 + ib13);
    double d2 = v_c14 - (b.nm - rb3 * (ic13 - ib17));
    return Path {
        .o17 = o17,
        .v_c14 = v_c14,
        .ib16 = ib16,
        .stages = { w1, d1, d2 },
        .out = u3,
    };
}

double Vca::rest_output (double cont)
{
    settle (cont);
    return path (drive(), 0.0, q6, q8 + a440, q2, std::nullopt).o17;
}

double Vca::static_output (double w, double cont)
{
    settle (cont);
    double o17 = q2;
    const VcaDrive d = drive();
    for (int k = 0; k < 8; ++k)
        o17 = path (d, 0.0, q6, q8 + a440, o17, w).o17;
    return o17;
}

double Vca::tick (double v_in, double cont)
{
    const VcaDrive d = control (cont);
    return signal (v_in, d);
}

VcaDrive Vca::drive() const
{
    const VcaBias& b = bias_;
    VcaDrive d;
    d.i_a = b.i_a;
    d.i_b = b.i_b;
    d.i_c = b.i_c;
    d.nm = b.nm;
    d.bb = bb;
    for (size_t k = 0; k < 3; ++k)
    {
        const PairBias& p = pairs[k];
        d.pairs[k] = PairDrive { .alpha = p.alpha, .ib0 = p.ib0, .b = p.b, .vt = p.vt, .vt0 = p.vt0 };
    }
    return d;
}

VcaDrive Vca::control (double cont)
{
    if (potato_on && potato.has_value())
    {
        const auto& table = *potato;
        std::optional<double> plugged;
        if (circuit.r_j3 == 0.0)
            plugged = circuit.ext;
        const auto t = table->lookup (cont, plugged);
        double ds4 = s4 - t[DRAWN];
        double dbb = bb - t[DRAWN + 1];
        std::array<double, DRAWN> e;
        for (size_t k = 0; k < DRAWN; ++k)
            e[k] = t[k] + t[DRAWN + 2 + k] * ds4 + t[2 * DRAWN + 2 + k] * dbb;
        std::array<double, VcaDrive::LEN> x {};
        std::copy (e.begin(), e.begin() + VcaDrive::LEN, x.begin());
        VcaDrive d = VcaDrive::from_array (x);
        d.bb = bb;
        constexpr size_t k = VcaDrive::LEN;
        bias_.ib1 = e[k];
        bias_.draw_nm = e[k + 1];
        bias_.draw_nn = e[k + 2];
        bias_.draw_bb = e[k + 3];
        step_chain();
        solved_cont = std::numeric_limits<double>::quiet_NaN();
        return d;
    }
    const size_t every = std::max (control_every, (size_t) 1);
    const bool moved = std::fabs (cont - solved_cont) > REFRESH_V
                       || std::fabs (circuit.ext - solved_ext.first) > REFRESH_EXT_V
                       || circuit.r_j3 != solved_ext.second;
    const bool whole = moved || every == 1;
    if (whole || count % every == 0)
    {
        const auto before = to;
        const auto x18s = x18;
        const auto x21s = x21;
        const VcaBias fast = bias_;
        solve_bias (cont);
        to = { bias_, pairs };
        if (whole)
        {
            from = to;
        }
        else
        {
            x18 = x18s;
            x21 = x21s;
            from = before;
            bias_ = fast;
        }
        solved_cont = cont;
        solved_ext = { circuit.ext, circuit.r_j3 };
    }
    count += 1;
    if (! whole)
    {
        double k = (double) ((count - 1) % every + 1) / (double) every;
        const double i_a = bias_.i_a;
        const double i_b = bias_.i_b;
        VcaBias nb = from.first.lerp (to.first, k);
        nb.i_a = i_a;
        nb.i_b = i_b;
        bias_ = nb;
        for (size_t i = 0; i < 3; ++i)
            pairs[i] = from.second[i].lerp (to.second[i], k);
        fast_bias (cont);
    }
    const VcaDrive d = drive();
    step_chain();
    return d;
}

double Vca::signal (double v_in, const VcaDrive& d)
{
    const VcaCircuit c = circuit;
    const double h = this->h;
    const Path pa = path (d, v_in, q6, q8 + a440, q2, std::nullopt);
    out_warm = plain_signal ? std::optional<double> (pa.out) : std::nullopt;
    double a6 = 1.0 / ((c.r2 + c.r34) * c.c6);
    double drive6 = (v_in - (d.bb - bb_rest) + c.r34 * pa.ib16) * a6;
    q6 = (q6 * (1.0 - 0.5 * h * a6) + h * drive6) / (1.0 + 0.5 * h * a6);
    double ra3 = c.r18 + c.r12 * c.r12_pos;
    double g = ra3 / (c.r40 + ra3);
    double k8 = pa.v_c14 - g * q8 - a440;
    double a8 = (1.0 - g) / (c.r40 * c.c8);
    q8 = (q8 * (1.0 - 0.5 * h * a8) + h * k8 / (c.r40 * c.c8)) / (1.0 + 0.5 * h * a8);
    double r_out = c.r77 + c.load;
    double a2 = 1.0 / (r_out * c.c2);
    double out_main = c.load * (pa.o17 - q2) / r_out;
    q2 = (q2 * (1.0 - 0.5 * h * a2) + h * pa.o17 * a2) / (1.0 + 0.5 * h * a2);
    return out_main;
}

void Vca::step_chain()
{
    const VcaCircuit c = circuit;
    const VcaBias b = bias_;
    const double h = this->h;
    double r_mid = c.r11 + c.r13 + c.r22;
    double j0 = (c.vp + c.r13 * b.draw_nm + c.r22 * (b.draw_nm + b.draw_nn)) / r_mid;
    double m[2][2] = {
        { 1.0 + h / c.c4 * (1.0 / r_mid + 1.0 / c.r10), h / c.c4 / r_mid },
        { h / c.c15 / r_mid, 1.0 + h / c.c15 * (1.0 / r_mid + 1.0 / c.r36) },
    };
    double r[2] = {
        s4 + h / c.c4 * (j0 - b.ib1),
        bb + h / c.c15 * (j0 - b.draw_nm - b.draw_nn - b.draw_bb),
    };
    double det = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    s4 = (r[0] * m[1][1] - m[0][1] * r[1]) / det;
    bb = (m[0][0] * r[1] - m[1][0] * r[0]) / det;
}

std::pair<double, std::array<double, 3>> Vca::probe (double v_in) const
{
    const Path p = path (drive(), v_in, q6, q8 + a440, q2, std::nullopt);
    return { p.o17, p.stages };
}

std::array<double, 4> Vca::chain() const
{
    return { circuit.vp - s4, bias_.nm, bias_.nn, bb };
}
} // namespace ca72::vca
