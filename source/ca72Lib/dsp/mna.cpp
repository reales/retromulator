#include "mna.h"

#include <bit>
#include <map>

namespace ca72::mna
{
std::tuple<double, double, double> Jfet::intrinsic (double vgs, double vds) const
{
    const double vgst = vgs - vto;
    if (vgst <= 0.0)
        return { 0.0, 0.0, 0.0 };
    const double l = 1.0 + lambda * vds;
    if (vds < vgst)
    {
        const double core = vds * (2.0 * vgst - vds);
        return {
            beta * core * l,
            beta * 2.0 * vds * l,
            beta * (2.0 * vgst - 2.0 * vds) * l + beta * core * lambda,
        };
    }
    return {
        beta * vgst * vgst * l,
        beta * 2.0 * vgst * l,
        beta * vgst * vgst * lambda,
    };
}

std::tuple<double, double, double> Jfet::forward (double vgs, double vds, double r_s, double r_d) const
{
    const auto [i0, g0, d0] = intrinsic (vgs, vds);
    if (r_s + r_d == 0.0)
        return { i0, g0, d0 };
    double i = std::fmin (i0, vds / (r_s + r_d));
    for (int it = 0; it < 50; ++it)
    {
        const auto [fv, fg, fd] = intrinsic (vgs - r_s * i, std::fmax (vds - (r_s + r_d) * i, 0.0));
        const double g = i - fv;
        const double dg = 1.0 + r_s * fg + (r_s + r_d) * fd;
        const double step = g / dg;
        i -= step;
        if (std::fabs (step) <= 1e-15 + 1e-12 * std::fabs (i))
            break;
    }
    const auto [fv, fg, fd] = intrinsic (vgs - r_s * i, std::fmax (vds - (r_s + r_d) * i, 0.0));
    (void) fv;
    const double dg = 1.0 + r_s * fg + (r_s + r_d) * fd;
    return { i, fg / dg, fd / dg };
}

std::pair<double, std::array<double, 3>> Jfet::channel (double vg, double vd, double vs) const
{
    if (vd >= vs)
    {
        const auto [i, g, d] = forward (vg - vs, vd - vs, rs, rd);
        return { i, { g, d, -g - d } };
    }
    const auto [i, g, d] = forward (vg - vd, vs - vd, rd, rs);
    return { -i, { -g, g + d, -d } };
}

DepletionConsts Depletion::consts() const
{
    const double c1 = -m;
    const double c2 = -m * (-m - 1.0) / 2.0;
    const double c3 = c2 * (-m - 2.0) / 3.0;
    const double c4 = c3 * (-m - 3.0) / 4.0;
    const double c5 = c4 * (-m - 4.0) / 5.0;
    const double c6 = c5 * (-m - 5.0) / 6.0;
    return DepletionConsts { { c1, c2, c3, c4, c5, c6 }, beyond_consts() };
}

std::array<double, 3> Depletion::beyond_consts() const
{
    const double f1 = vj * (1.0 - ca72::ulp::pow (1.0 - fc, 1.0 - m)) / (1.0 - m);
    const double f2 = ca72::ulp::pow (1.0 - fc, 1.0 + m);
    const double f3 = 1.0 - fc * (1.0 + m);
    return { f1, f2, f3 };
}

std::pair<double, double> Depletion::charge (double v) const
{
    if (v < fc * vj)
    {
        const double a = 1.0 - v / vj;
        const double s = ca72::ulp::exp (-m * ca72::ulp::log (a));
        return { cj0 * vj * (1.0 - a * s) / (1.0 - m), cj0 * s };
    }
    return beyond (v, beyond_consts());
}

Part Part::Resistor (Node a, Node b, double r)
{
    Part p;
    p.tag = Tag::Resistor;
    p.resistor_a = a;
    p.resistor_b = b;
    p.resistor_r = r;
    return p;
}

Part Part::Capacitor (Node a, Node b, double c)
{
    Part p;
    p.tag = Tag::Capacitor;
    p.capacitor_a = a;
    p.capacitor_b = b;
    p.capacitor_c = c;
    return p;
}

Part Part::Diode (Node a, Node k, double is, double nvt, ca72::mna::Depletion cap, double tt)
{
    Part p;
    p.tag = Tag::Diode;
    p.diode_a = a;
    p.diode_k = k;
    p.diode_is = is;
    p.diode_nvt = nvt;
    p.diode_cap = cap;
    p.diode_tt = tt;
    return p;
}

Part Part::Junction (Node a, Node k, ca72::mna::Depletion cap)
{
    Part p;
    p.tag = Tag::Junction;
    p.junction_a = a;
    p.junction_k = k;
    p.junction_cap = cap;
    return p;
}

Part Part::Diffusion (Node a, Node k, double is, double vt, double tt)
{
    Part p;
    p.tag = Tag::Diffusion;
    p.diffusion_a = a;
    p.diffusion_k = k;
    p.diffusion_is = is;
    p.diffusion_vt = vt;
    p.diffusion_tt = tt;
    return p;
}

Part Part::Bjt (Node c, Node b, Node e, ca72::devices::BjtAt m, double vaf, bool pnp)
{
    Part p;
    p.tag = Tag::Bjt;
    p.bjt_c = c;
    p.bjt_b = b;
    p.bjt_e = e;
    p.bjt_m = m;
    p.bjt_vaf = vaf;
    p.bjt_pnp = pnp;
    return p;
}

Part Part::Jfet (Node d, Node g, Node s, ca72::mna::Jfet m)
{
    Part p;
    p.tag = Tag::Jfet;
    p.jfet_d = d;
    p.jfet_g = g;
    p.jfet_s = s;
    p.jfet_m = m;
    return p;
}

LuProgram::LuProgram (const LuProgram& o)
{
    *this = o;
}

LuProgram& LuProgram::operator= (const LuProgram& o)
{
    if (this == &o)
        return *this;
    auto with_room = [&] (std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
        dst.clear();
        dst.reserve (std::max (o.n * o.n, src.size()));
        dst.insert (dst.end(), src.begin(), src.end());
    };
    ready = o.ready;
    pattern = o.pattern;
    n = o.n;
    piv = o.piv;
    with_room (cand, o.cand);
    with_room (cols, o.cols);
    with_room (rows, o.rows);
    cand_off = o.cand_off;
    cols_off = o.cols_off;
    rows_off = o.rows_off;
    snap_rows = o.snap_rows;
    snap_cols = o.snap_cols;
    flat_ok = o.flat_ok;
    flat_tgt.clear();
    flat_tgt.reserve (std::max (o.flat_tgt.capacity(), o.n * o.n));
    flat_tgt.insert (flat_tgt.end(), o.flat_tgt.begin(), o.flat_tgt.end());
    flat_tgt_off = o.flat_tgt_off;
    flat_ops.clear();
    flat_ops.reserve (std::max (o.flat_ops.capacity(), FLAT_OPS));
    flat_ops.insert (flat_ops.end(), o.flat_ops.begin(), o.flat_ops.end());
    flat_back.clear();
    flat_back.reserve (std::max (o.flat_back.capacity(), o.n * o.n));
    flat_back.insert (flat_back.end(), o.flat_back.begin(), o.flat_back.end());
    flat_back_off = o.flat_back_off;
    return *this;
}

LuProgram LuProgram::new_ (size_t n)
{
    LuProgram lu;
    lu.ready = false;
    lu.pattern = 0;
    lu.n = n;
    lu.cand.reserve (n * n);
    lu.cols.reserve (n * n);
    lu.rows.reserve (n * n);
    lu.snap_rows.assign (n, std::array<uint64_t, MAX_SOLVED> {});
    lu.snap_cols.assign (n, std::array<uint64_t, MAX_SOLVED> {});
    lu.flat_ok = false;
    lu.flat_tgt.reserve (n * n);
    lu.flat_ops.reserve (FLAT_OPS);
    lu.flat_back.reserve (n * n);
    return lu;
}

void LuProgram::flatten (size_t n_)
{
    flat_ok = false;
    flat_tgt.clear();
    flat_ops.clear();
    flat_back.clear();
    for (size_t col = 0; col < n_; ++col)
    {
        const size_t c0 = cols_off[col], c1 = cols_off[col + 1];
        const size_t r0 = rows_off[col], r1 = rows_off[col + 1];
        for (size_t ri = r0; ri < r1; ++ri)
        {
            const size_t r = rows[ri];
            const size_t start = flat_ops.size();
            for (size_t ci = c0; ci < c1; ++ci)
            {
                const size_t c = cols[ci];
                if (c <= col)
                    continue;
                if (flat_ops.size() == FLAT_OPS)
                    return;
                flat_ops.push_back ({ (uint16_t) (r * n_ + c), (uint16_t) (col * n_ + c) });
            }
            flat_tgt.push_back ({
                (uint16_t) (r * n_ + col),
                (uint16_t) r,
                (uint16_t) start,
                (uint16_t) flat_ops.size(),
            });
        }
        flat_tgt_off[col + 1] = (uint16_t) flat_tgt.size();
        for (size_t ci = c0; ci < c1; ++ci)
        {
            const size_t c = cols[ci];
            if (c <= col)
                continue;
            flat_back.push_back ({ (uint16_t) (col * n_ + c), (uint16_t) c });
        }
        flat_back_off[col + 1] = (uint16_t) flat_back.size();
    }
    flat_ok = true;
}

double pnjlim (double vnew, double vold, double vt, double vcrit)
{
    if (vnew > vcrit && std::fabs (vnew - vold) > 2.0 * vt)
    {
        if (vold > 0.0)
        {
            const double arg = 1.0 + (vnew - vold) / vt;
            if (arg > 0.0)
                return vold + vt * ca72::ulp::log (arg);
            return vcrit;
        }
        return vt * ca72::ulp::log (vnew / vt);
    }
    return vnew;
}

std::pair<double, double> diode_charge (double vd, double is, double nvt, const Depletion& cap, double tt)
{
    const double e = ca72::ulp::exp (std::fmin (vd / nvt, 80.0));
    std::array<double, 2> r = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
    const DepletionConsts k = cap.consts();
    return diode_charge_e (vd, is, nvt, cap, k, tt, e, r);
}

std::pair<double, double> diode_charge_e (double vd,
                                          double is,
                                          double nvt,
                                          const Depletion& cap,
                                          const DepletionConsts& k,
                                          double tt,
                                          double e,
                                          std::array<double, 2>& r)
{
    const auto [qj, cj] = cap.charge_near (vd, r, k);
    if (tt == 0.0)
        return { qj, cj };
    return { qj + tt * is * (e - 1.0), cj + tt * is * e / nvt };
}

std::pair<double, double> diffusion_charge (double v, double is, double vt, double tt)
{
    const double v1 = std::fmin (v, 1.0);
    const double e = ca72::ulp::exp (v1 / vt);
    const double q1 = tt * is * (e - 1.0);
    const double c1 = tt * is * e / vt;
    return { q1 + c1 * (v - v1), c1 };
}

double vcrit (double vt, double is)
{
    return vt * ca72::ulp::log (vt / (1.4142135623730951 * is));
}

double fetlim (double vnew, double vold, double vto)
{
    const double vtsthi = std::fabs (2.0 * (vold - vto)) + 2.0;
    const double vtstlo = vtsthi / 2.0 + 2.0;
    const double vtox = vto + 3.5;
    const double delv = vnew - vold;
    if (vold >= vto)
    {
        if (vold >= vtox)
        {
            if (delv <= 0.0)
            {
                if (vnew >= vtox)
                {
                    if (-delv > vtstlo)
                        vnew = vold - vtstlo;
                }
                else
                    vnew = std::fmax (vnew, vto + 2.0);
            }
            else if (delv >= vtsthi)
                vnew = vold + vtsthi;
        }
        else if (delv <= 0.0)
            vnew = std::fmax (vnew, vto - 0.5);
        else
            vnew = std::fmin (vnew, vto + 4.0);
    }
    else if (delv <= 0.0)
    {
        if (-delv > vtsthi)
            vnew = vold - vtsthi;
    }
    else
    {
        const double vtemp = vto + 0.5;
        if (vnew <= vtemp)
        {
            if (delv > vtstlo)
                vnew = vold + vtstlo;
        }
        else
            vnew = vtemp;
    }
    return vnew;
}

Circuit Circuit::new_ (size_t nodes, size_t held)
{
    assert (held >= 1 && held <= nodes);
    const size_t n = nodes - held;
    Circuit c;
    c.nodes = nodes;
    c.held = held;
    c.v_.assign (nodes, 0.0);
    c.saved.assign (nodes, 0.0);
    c.theta = 1.0;
    c.jac.assign (n * n, 0.0);
    c.lu = LuProgram::new_ (n);
    c.f.assign (n, 0.0);
    c.dx.assign (n, 0.0);
    c.tolerance = { 1e-9, 1e-9 };
    c.predict_limit = std::nullopt;
    c.step_iterations = 100;
    c.prev.assign (nodes, 0.0);
    c.prev2.assign (nodes, 0.0);
    return c;
}

Node Circuit::node()
{
    nodes += 1;
    v_.push_back (0.0);
    saved.push_back (0.0);
    prev.push_back (0.0);
    prev2.push_back (0.0);
    slots_ready = false;
    const size_t n = nodes - held;
    assert (n <= MAX_SOLVED);
    jac.assign (n * n, 0.0);
    lu = LuProgram::new_ (n);
    f.push_back (0.0);
    dx.push_back (0.0);
    return nodes - 1;
}

void Circuit::add_bjt (Node c, Node b, Node e, const ca72::devices::Bjt& qd, double celsius, bool pnp)
{
    auto inner = [this] (Node outer, double r) -> Node {
        if (r > 0.0)
        {
            const Node n = node();
            v_[n] = v_[outer];
            add (Part::Resistor (outer, n, r));
            return n;
        }
        return outer;
    };
    const Node ci = inner (c, qd.rc);
    const Node bi = inner (b, qd.rb);
    const Node ei = inner (e, qd.re);
    const ca72::devices::BjtAt m = qd.at (celsius);
    add (Part::Bjt (ci, bi, ei, m, qd.vaf, pnp));
    const Node anode_e = pnp ? ei : bi;
    const Node cathode_e = pnp ? bi : ei;
    const Node anode_c = pnp ? ci : bi;
    const Node cathode_c = pnp ? bi : ci;
    if (qd.cje > 0.0)
    {
        const Depletion cap { qd.cje, qd.vje, qd.mje, 0.5 };
        add (Part::Junction (anode_e, cathode_e, cap));
    }
    if (qd.cjc > 0.0)
    {
        const Depletion cap { qd.cjc, qd.vjc, qd.mjc, 0.5 };
        add (Part::Junction (anode_c, cathode_c, cap));
    }
    if (qd.tf > 0.0)
        add (Part::Diffusion (anode_e, cathode_e, m.is, m.vt, qd.tf));
}

void Circuit::add_jfet (Node d, Node gn, Node s, const Jfet& j)
{
    auto inner = [this] (Node outer, double r) -> Node {
        if (r > 0.0)
        {
            const Node n = node();
            v_[n] = v_[outer];
            add (Part::Resistor (outer, n, r));
            return n;
        }
        return outer;
    };
    const Node di = inner (d, j.rd);
    const Node si = inner (s, j.rs);
    Jfet jm = j;
    jm.rd = 0.0;
    jm.rs = 0.0;
    add (Part::Jfet (di, gn, si, jm));
    const double nvt = j.n * ca72::devices::vt (25.0);
    auto cap = [&] (double cj0) { return Depletion { cj0, j.pb, 0.5, j.fc }; };
    add (Part::Diode (gn, si, j.is, nvt, cap (j.cgs), 0.0));
    add (Part::Diode (gn, di, j.is, nvt, cap (j.cgd), 0.0));
}

size_t Circuit::add (Part p)
{
    using T = Part::Tag;
    const uint32_t idx = (uint32_t) parts.size();
    if (p.tag == T::Diode || p.tag == T::Bjt || p.tag == T::Jfet)
        limiting.push_back (idx);
    if (p.tag == T::Capacitor || p.tag == T::Junction || p.tag == T::Diffusion || p.tag == T::Diode)
        charged.push_back (idx);
    parts.push_back (p);
    g.push_back (p.tag == T::Resistor ? 1.0 / p.resistor_r : 0.0);
    qc.push_back ({ 0.0, 0.0, 0.0 });
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        near.push_back ({ { { nan, nan }, { nan, nan }, { nan, nan } } });
    }
    if (p.tag == T::Junction)
        consts.push_back (p.junction_cap.consts());
    else if (p.tag == T::Diode)
        consts.push_back (p.diode_cap.consts());
    else
        consts.push_back (DepletionConsts::default_());
    slots.push_back ({ Slot::default_(), Slot::default_(), Slot::default_() });
    slots_ready = false;
    q.push_back (0.0);
    i_prev.push_back (0.0);
    lim.push_back ({ 0.0, 0.0 });
    if (p.tag == T::Diode)
        vcrit.push_back (ca72::mna::vcrit (p.diode_nvt, p.diode_is));
    else if (p.tag == T::Bjt)
        vcrit.push_back (ca72::mna::vcrit (p.bjt_m.vt, p.bjt_m.is));
    else
        vcrit.push_back (0.0);
    return parts.size() - 1;
}

void Circuit::set (Node n, double v)
{
    v_[n] = v;
    if (n >= held)
        prev_h = 0.0;
}

void Circuit::set_theta (double theta_)
{
    assert (theta_ >= 0.5 && theta_ <= 1.0);
    theta = theta_;
}

std::pair<double, double> Circuit::charge_current (size_t idx, double qv, double h) const
{
    if (theta == 1.0)
        return { (qv - this->q[idx]) / h, h };
    const double th = theta * h;
    return { (qv - this->q[idx]) / th - (1.0 - theta) / theta * i_prev[idx], th };
}

void Circuit::set_resistance (size_t part, double r)
{
    if (parts[part].tag == Part::Tag::Resistor)
    {
        parts[part].resistor_r = r;
        g[part] = 1.0 / r;
    }
}

std::optional<size_t> Circuit::free (Node n) const
{
    if (n >= held)
        return n - held;
    return std::nullopt;
}

std::array<std::optional<StampNodes>, 3> Circuit::stamp_nodes (const Part& p)
{
    using T = Part::Tag;
    auto two = [] (Node a, Node b) -> std::array<std::optional<StampNodes>, 3> {
        return { StampNodes { a, b, { a, b, GND }, 2 }, std::nullopt, std::nullopt };
    };
    switch (p.tag)
    {
        case T::Resistor: return two (p.resistor_a, p.resistor_b);
        case T::Capacitor: return two (p.capacitor_a, p.capacitor_b);
        case T::Junction: return two (p.junction_a, p.junction_k);
        case T::Diffusion: return two (p.diffusion_a, p.diffusion_k);
        case T::Diode: return two (p.diode_a, p.diode_k);
        case T::Bjt:
        {
            const std::array<Node, 3> on = { p.bjt_b, p.bjt_e, p.bjt_c };
            return {
                StampNodes { p.bjt_c, GND, on, 3 },
                StampNodes { p.bjt_b, GND, on, 3 },
                StampNodes { p.bjt_e, GND, on, 3 },
            };
        }
        case T::Jfet:
            return { StampNodes { p.jfet_d, p.jfet_s, { p.jfet_g, p.jfet_d, p.jfet_s }, 3 }, std::nullopt, std::nullopt };
    }
    assert (false);
    return { std::nullopt, std::nullopt, std::nullopt };
}

void Circuit::build_slots()
{
    const size_t n = nodes - held;
    auto row = [this] (Node x) -> uint32_t {
        if (x >= held)
            return (uint32_t) (x - held);
        return NONE;
    };
    std::array<PatternPair, 2> pat {};
    for (auto& [rows_in, cols_in] : pat)
    {
        for (size_t k = 0; k < n; ++k)
        {
            rows_in[k] = uint64_t { 1 } << k;
            cols_in[k] = uint64_t { 1 } << k;
        }
    }
    for (size_t idx = 0; idx < parts.size(); ++idx)
    {
        const Part p = parts[idx];
        const bool in_dc = ! (p.tag == Part::Tag::Capacitor || p.tag == Part::Tag::Junction || p.tag == Part::Tag::Diffusion);
        const auto stamps = stamp_nodes (p);
        for (size_t s = 0; s < 3; ++s)
        {
            if (! stamps[s].has_value())
                continue;
            const auto& [a, b, on, count] = *stamps[s];
            Slot slot;
            slot.fa = row (a);
            slot.fb = row (b);
            slot.ja = { NONE, NONE, NONE };
            slot.jb = { NONE, NONE, NONE };
            for (size_t j = 0; j < count; ++j)
            {
                const uint32_t cm = row (on[j]);
                if (cm == NONE)
                    continue;
                const std::array<std::pair<uint32_t, uint32_t*>, 2> targets = { {
                    { slot.fa, &slot.ja[j] },
                    { slot.fb, &slot.jb[j] },
                } };
                for (const auto& [r, entry] : targets)
                {
                    if (r == NONE)
                        continue;
                    *entry = r * (uint32_t) n + cm;
                    for (size_t dc = 0; dc < 2; ++dc)
                    {
                        if (dc == 1 || in_dc)
                        {
                            pat[dc].first[cm] |= uint64_t { 1 } << r;
                            pat[dc].second[r] |= uint64_t { 1 } << cm;
                        }
                    }
                }
            }
            slots[idx][s] = slot;
        }
    }
    pattern = pat;
    slots_ready = true;
}

void Circuit::stamp_at (size_t idx, size_t s, double i, std::span<const double> gs)
{
    const Slot slot = slots[idx][s];
    const size_t len = std::min<size_t> (3, gs.size());
    if (slot.fa != NONE)
    {
        f[slot.fa] += i;
        for (size_t k = 0; k < len; ++k)
            if (slot.ja[k] != NONE)
                jac[slot.ja[k]] += gs[k];
    }
    if (slot.fb != NONE)
    {
        f[slot.fb] -= i;
        for (size_t k = 0; k < len; ++k)
            if (slot.jb[k] != NONE)
                jac[slot.jb[k]] -= gs[k];
    }
}

std::pair<double, double> Circuit::charge (const Part& p) const
{
    switch (p.tag)
    {
        case Part::Tag::Capacitor:
            return { p.capacitor_c * (v_[p.capacitor_a] - v_[p.capacitor_b]), p.capacitor_c };
        case Part::Tag::Junction:
            return p.junction_cap.charge (v_[p.junction_a] - v_[p.junction_k]);
        case Part::Tag::Diode:
            return diode_charge (v_[p.diode_a] - v_[p.diode_k], p.diode_is, p.diode_nvt, p.diode_cap, p.diode_tt);
        case Part::Tag::Diffusion:
            return diffusion_charge (v_[p.diffusion_a] - v_[p.diffusion_k], p.diffusion_is, p.diffusion_vt, p.diffusion_tt);
        default:
            return { 0.0, 0.0 };
    }
}

bool Circuit::load (std::optional<double> h)
{
    if (! slots_ready)
        build_slots();
    qc_ok = h.has_value();
    std::fill (jac.begin(), jac.end(), 0.0);
    std::fill (f.begin(), f.end(), 0.0);
    const size_t n = nodes - held;
    for (size_t k = 0; k < n; ++k)
    {
        const size_t nd = k + held;
        f[k] += GMIN * v_[nd];
        jac[k * n + k] += GMIN;
    }
    bool limited = false;
    for (size_t idx = 0; idx < parts.size(); ++idx)
    {
        const Part& p = parts[idx];
        switch (p.tag)
        {
            case Part::Tag::Resistor:
            {
                const double gv = g[idx];
                const double i = gv * (v_[p.resistor_a] - v_[p.resistor_b]);
                const std::array<double, 2> gs = { gv, -gv };
                stamp_at (idx, 0, i, gs);
                break;
            }
            case Part::Tag::Capacitor:
            case Part::Tag::Junction:
            case Part::Tag::Diffusion:
            {
                if (h.has_value())
                {
                    const double hv = *h;
                    Node a, b;
                    if (p.tag == Part::Tag::Capacitor)
                    {
                        a = p.capacitor_a;
                        b = p.capacitor_b;
                    }
                    else if (p.tag == Part::Tag::Junction)
                    {
                        a = p.junction_a;
                        b = p.junction_k;
                    }
                    else
                    {
                        a = p.diffusion_a;
                        b = p.diffusion_k;
                    }
                    const double v = v_[a] - v_[b];
                    const auto [qv, c] = p.tag == Part::Tag::Junction
                                             ? p.junction_cap.charge_near (v, near[idx][0], consts[idx])
                                             : charge (p);
                    qc[idx] = { qv, c, v };
                    const auto [i, th] = charge_current (idx, qv, hv);
                    const std::array<double, 2> gs = { c / th, -c / th };
                    stamp_at (idx, 0, i, gs);
                }
                break;
            }
            case Part::Tag::Diode:
            {
                const Node a = p.diode_a;
                const Node k = p.diode_k;
                const double is = p.diode_is;
                const double nvt = p.diode_nvt;
                const double tt = p.diode_tt;
                const double vd0 = v_[a] - v_[k];
                const double vd = pnjlim (vd0, lim[idx][0], nvt, vcrit[idx]);
                limited |= vd != vd0;
                lim[idx][0] = vd;
                const double e = ca72::ulp::exp (std::fmin (vd / nvt, 80.0));
                const double gv = is * e / nvt + GMIN;
                const double i = is * (e - 1.0) + GMIN * vd + gv * (vd0 - vd);
                double gs = gv;
                double ii = i;
                if (h.has_value())
                {
                    const double hv = *h;
                    const auto [qv, c] = diode_charge_e (vd, is, nvt, p.diode_cap, consts[idx], tt, e, near[idx][1]);
                    qc[idx] = { qv, c, vd };
                    const auto [iq, th] = charge_current (idx, qv, hv);
                    ii += iq + c / th * (vd0 - vd);
                    gs += c / th;
                }
                const std::array<double, 2> gss = { gs, -gs };
                stamp_at (idx, 0, ii, gss);
                break;
            }
            case Part::Tag::Bjt:
            {
                const Node c = p.bjt_c;
                const Node b = p.bjt_b;
                const Node e = p.bjt_e;
                const ca72::devices::BjtAt& m = p.bjt_m;
                const double pol = p.bjt_pnp ? -1.0 : 1.0;
                const double x1_0 = pol * (v_[b] - v_[e]);
                const double x2_0 = pol * (v_[b] - v_[c]);
                const double vc = vcrit[idx];
                const double x1 = pnjlim (x1_0, lim[idx][0], m.vt, vc);
                const double x2 = pnjlim (x2_0, lim[idx][1], m.vt, vc);
                limited |= x1 != x1_0 || x2 != x2_0;
                lim[idx] = { x1, x2 };
                const ca72::devices::Currents d = m.currents_d (x1, x2, p.bjt_vaf);
                const double dx1 = x1_0 - x1;
                const double dx2 = x2_0 - x2;
                const double ic = d.ic + d.dic[0] * dx1 + d.dic[1] * dx2 - GMIN * x2_0;
                const double ib = d.ib + d.dib[0] * dx1 + d.dib[1] * dx2 + GMIN * (x1_0 + x2_0);
                const double c1 = d.dic[0], c2 = d.dic[1] - GMIN;
                const double b1 = d.dib[0] + GMIN, b2 = d.dib[1] + GMIN;
                const std::array<double, 3> gc = { c1 + c2, -c1, -c2 };
                stamp_at (idx, 0, pol * ic, gc);
                const std::array<double, 3> gb = { b1 + b2, -b1, -b2 };
                stamp_at (idx, 1, pol * ib, gb);
                const std::array<double, 3> ge = { -(c1 + c2 + b1 + b2), c1 + b1, c2 + b2 };
                stamp_at (idx, 2, -pol * (ic + ib), ge);
                break;
            }
            case Part::Tag::Jfet:
            {
                const Node d = p.jfet_d;
                const Node gn = p.jfet_g;
                const Node s = p.jfet_s;
                const Jfet& m = p.jfet_m;
                const double vg = v_[gn];
                const double vgs0 = vg - v_[s];
                const double vgd0 = vg - v_[d];
                const double vgs = fetlim (vgs0, lim[idx][0], m.vto);
                const double vgd = fetlim (vgd0, lim[idx][1], m.vto);
                limited |= vgs != vgs0 || vgd != vgd0;
                lim[idx] = { vgs, vgd };
                const double vd_l = vg - vgd;
                const double vs_l = vg - vgs;
                const auto [i0, sl] = m.channel (vg, vd_l, vs_l);
                const double i = i0 + sl[1] * (v_[d] - vd_l) + sl[2] * (v_[s] - vs_l);
                const std::array<double, 3> gs = { sl[0], sl[1], sl[2] };
                stamp_at (idx, 0, i, gs);
                break;
            }
        }
    }
    return limited;
}

void Circuit::linear_solve (size_t pat)
{
    const size_t n = nodes - held;
    for (size_t k = 0; k < n; ++k)
        dx[k] = -f[k];
    auto below = [] (uint64_t mask, size_t col) -> uint64_t {
        return mask & (~uint64_t { 0 } << col) & ~(uint64_t { 1 } << col);
    };
    const size_t from = (lu.ready && lu.pattern == pat && lu.n == n && lu.flat_ok) ? replay_flat (n) : 0;
    if (from == n)
    {
        back_flat (n);
        return;
    }
    lu_recorded += 1;
    lu_recorded_from += (uint64_t) from;
    bool valid = lu.ready && lu.pattern == pat && lu.n == n;
    if (! valid)
    {
        lu.cand.clear();
        lu.cols.clear();
        lu.rows.clear();
    }
    std::array<uint64_t, MAX_SOLVED> rows_in = pattern[pat].first;
    std::array<uint64_t, MAX_SOLVED> cols_in = pattern[pat].second;
    for (size_t col = from; col < n; ++col)
    {
        size_t piv = col;
        if (valid)
        {
            for (size_t ci = lu.cand_off[col]; ci < lu.cand_off[col + 1]; ++ci)
            {
                const size_t r = lu.cand[ci];
                if (std::fabs (jac[r * n + col]) > std::fabs (jac[piv * n + col]))
                    piv = r;
            }
            if (piv != (size_t) lu.piv[col])
            {
                valid = false;
                rows_in = lu.snap_rows[col];
                cols_in = lu.snap_cols[col];
                lu.cand.resize (std::min<size_t> (lu.cand.size(), lu.cand_off[col]));
                lu.cols.resize (std::min<size_t> (lu.cols.size(), lu.cols_off[col]));
                lu.rows.resize (std::min<size_t> (lu.rows.size(), lu.rows_off[col]));
            }
        }
        if (! valid)
        {
            lu.snap_rows[col] = rows_in;
            lu.snap_cols[col] = cols_in;
            piv = col;
            uint64_t cand = below (rows_in[col], col);
            while (cand != 0)
            {
                const size_t r = (size_t) std::countr_zero (cand);
                cand &= cand - 1;
                lu.cand.push_back ((uint8_t) r);
                if (std::fabs (jac[r * n + col]) > std::fabs (jac[piv * n + col]))
                    piv = r;
            }
            lu.piv[col] = (uint8_t) piv;
            if (piv != col)
            {
                for (size_t mi = col; mi < n; ++mi)
                {
                    uint64_t& m = rows_in[mi];
                    if (((m >> col) & 1) != ((m >> piv) & 1))
                        m ^= (uint64_t { 1 } << col) | (uint64_t { 1 } << piv);
                }
                std::swap (cols_in[col], cols_in[piv]);
            }
            const uint64_t fill = cols_in[col] & (~uint64_t { 0 } << col);
            uint64_t cs = fill;
            while (cs != 0)
            {
                lu.cols.push_back ((uint8_t) std::countr_zero (cs));
                cs &= cs - 1;
            }
            uint64_t rs = below (rows_in[col], col);
            while (rs != 0)
            {
                const size_t r = (size_t) std::countr_zero (rs);
                rs &= rs - 1;
                lu.rows.push_back ((uint8_t) r);
                cols_in[r] |= fill;
                uint64_t c = fill;
                while (c != 0)
                {
                    rows_in[(size_t) std::countr_zero (c)] |= uint64_t { 1 } << r;
                    c &= c - 1;
                }
            }
            lu.cand_off[col + 1] = (uint16_t) lu.cand.size();
            lu.cols_off[col + 1] = (uint16_t) lu.cols.size();
            lu.rows_off[col + 1] = (uint16_t) lu.rows.size();
        }
        if (piv != col)
        {
            for (size_t c = col; c < n; ++c)
                std::swap (jac[col * n + c], jac[piv * n + c]);
            std::swap (dx[col], dx[piv]);
        }
        const double d = jac[col * n + col];
        const size_t c0 = lu.cols_off[col], c1 = lu.cols_off[col + 1];
        const double* pivot = jac.data() + col * n;
        for (size_t ri = lu.rows_off[col]; ri < lu.rows_off[col + 1]; ++ri)
        {
            const size_t r = lu.rows[ri];
            double* row = jac.data() + r * n;
            const double factor = row[col] / d;
            for (size_t ci = c0; ci < c1; ++ci)
            {
                const size_t c = lu.cols[ci];
                row[c] -= factor * pivot[c];
            }
            dx[r] -= factor * dx[col];
        }
    }
    if (! valid)
    {
        lu.ready = true;
        lu.pattern = pat;
        lu.n = n;
        lu.flatten (n);
    }
    for (size_t col = n; col-- > 0;)
    {
        double s = dx[col];
        for (size_t ci = lu.cols_off[col]; ci < lu.cols_off[col + 1]; ++ci)
        {
            const size_t c = lu.cols[ci];
            if (c > col)
                s -= jac[col * n + c] * dx[c];
        }
        dx[col] = s / jac[col * n + col];
    }
}

size_t Circuit::replay_flat (size_t n)
{
    for (size_t col = 0; col < n; ++col)
    {
        size_t piv = col;
        for (size_t ci = lu.cand_off[col]; ci < lu.cand_off[col + 1]; ++ci)
        {
            const size_t r = lu.cand[ci];
            if (std::fabs (jac[r * n + col]) > std::fabs (jac[piv * n + col]))
                piv = r;
        }
        if (piv != (size_t) lu.piv[col])
            return col;
        if (piv != col)
        {
            for (size_t c = col; c < n; ++c)
                std::swap (jac[col * n + c], jac[piv * n + c]);
            std::swap (dx[col], dx[piv]);
        }
        const double d = jac[col * n + col];
        const double dx_col = dx[col];
        for (size_t ti = lu.flat_tgt_off[col]; ti < lu.flat_tgt_off[col + 1]; ++ti)
        {
            const auto& t = lu.flat_tgt[ti];
            const double factor = jac[t[0]] / d;
            for (size_t oi = t[2]; oi < t[3]; ++oi)
            {
                const auto& op = lu.flat_ops[oi];
                const double pv = jac[op[1]];
                jac[op[0]] -= factor * pv;
            }
            dx[t[1]] -= factor * dx_col;
        }
    }
    return n;
}

void Circuit::back_flat (size_t n)
{
    for (size_t col = n; col-- > 0;)
    {
        double s = dx[col];
        for (size_t bi = lu.flat_back_off[col]; bi < lu.flat_back_off[col + 1]; ++bi)
        {
            const auto& b = lu.flat_back[bi];
            s -= jac[b[0]] * dx[b[1]];
        }
        dx[col] = s / jac[col * n + col];
    }
}

ca72::Result<size_t, NoConvergence> Circuit::newton (std::optional<double> h, size_t max_iter)
{
    using R = ca72::Result<size_t, NoConvergence>;
    const size_t n = nodes - held;
    for (const uint32_t idx32 : limiting)
    {
        const size_t idx = idx32;
        const Part& p = parts[idx];
        switch (p.tag)
        {
            case Part::Tag::Diode:
                lim[idx] = { std::fmin (v_[p.diode_a] - v_[p.diode_k], vcrit[idx]), 0.0 };
                break;
            case Part::Tag::Bjt:
            {
                const double pol = p.bjt_pnp ? -1.0 : 1.0;
                const double vc = vcrit[idx];
                lim[idx] = {
                    std::fmin (pol * (v_[p.bjt_b] - v_[p.bjt_e]), vc),
                    std::fmin (pol * (v_[p.bjt_b] - v_[p.bjt_c]), vc),
                };
                break;
            }
            case Part::Tag::Jfet:
                lim[idx] = { v_[p.jfet_g] - v_[p.jfet_s], v_[p.jfet_g] - v_[p.jfet_d] };
                break;
            default:
                lim[idx] = { 0.0, 0.0 };
                break;
        }
    }
    solves += 1;
    const auto tol = tolerance;
    bool loaded = false;
    for (size_t it = 1; it <= max_iter; ++it)
    {
        loaded = true;
        total_iterations += 1;
        const bool limited = load (h);
        linear_solve (h.has_value() ? 1 : 0);
        bool finite = true;
        for (const double x : dx)
            if (! std::isfinite (x))
            {
                finite = false;
                break;
            }
        if (! finite)
            break;
        for (size_t k = 0; k < n; ++k)
            v_[k + held] += dx[k];
        bool small = true;
        for (size_t k = 0; k < n; ++k)
            if (! (std::fabs (dx[k]) <= tol.first + tol.second * std::fabs (v_[k + held])))
            {
                small = false;
                break;
            }
        if (small && ! limited)
        {
            iterations = it;
            size_t bucket;
            if (it <= 4)
                bucket = std::max<size_t> (it, 1) - 1;
            else if (it <= 8)
                bucket = 4;
            else if (it <= 16)
                bucket = 5;
            else if (it <= 32)
                bucket = 6;
            else
                bucket = 7;
            iteration_counts[bucket] += 1;
            return R::Ok (it);
        }
    }
    iteration_counts[7] += 1;
    iterations = max_iter;
    double residual;
    if (loaded)
    {
        residual = 0.0;
        for (const double x : f)
            residual = std::fmax (residual, std::fabs (x));
    }
    else
        residual = std::numeric_limits<double>::infinity();
    return R::Err (NoConvergence { max_iter, residual });
}

ca72::Result<size_t, NoConvergence> Circuit::dc()
{
    prev_h = 0.0;
    std::copy (v_.begin(), v_.end(), saved.begin());
    auto r = newton (std::nullopt, 200);
    if (r.is_ok())
    {
        accept (std::nullopt);
        return r;
    }
    std::copy (saved.begin(), saved.end(), v_.begin());
    return r;
}

ca72::Result<size_t, NoConvergence> Circuit::step (double h)
{
    std::copy (v_.begin(), v_.end(), saved.begin());
    if (prev_h == h && prev2_h == h)
    {
        bool smooth = true;
        if (predict_limit.has_value())
        {
            const double lim_ = *predict_limit;
            for (size_t k = held; k < nodes; ++k)
            {
                const double d = 2.0 * v_[k] - 3.0 * prev[k] + prev2[k];
                if (! (std::fabs (d) <= lim_))
                {
                    smooth = false;
                    break;
                }
            }
        }
        if (smooth)
            for (size_t k = held; k < nodes; ++k)
                v_[k] = 3.0 * v_[k] - 3.0 * prev[k] + prev2[k];
    }
    auto r = newton (h, step_iterations);
    if (r.is_ok())
    {
        std::swap (prev, prev2);
        std::swap (prev, saved);
        prev2_h = prev_h;
        prev_h = h;
        accept (h);
        return r;
    }
    std::copy (saved.begin(), saved.end(), v_.begin());
    return r;
}

void Circuit::accept (std::optional<double> h)
{
    const bool cached = h.has_value() && qc_ok;
    for (const uint32_t idx32 : charged)
    {
        const size_t idx = idx32;
        const Part& p = parts[idx];
        double qv;
        switch (p.tag)
        {
            case Part::Tag::Capacitor:
                qv = p.capacitor_c * (v_[p.capacitor_a] - v_[p.capacitor_b]);
                break;
            case Part::Tag::Junction:
            case Part::Tag::Diffusion:
            case Part::Tag::Diode:
            {
                if (cached)
                {
                    Node a, k;
                    if (p.tag == Part::Tag::Junction)
                    {
                        a = p.junction_a;
                        k = p.junction_k;
                    }
                    else if (p.tag == Part::Tag::Diffusion)
                    {
                        a = p.diffusion_a;
                        k = p.diffusion_k;
                    }
                    else
                    {
                        a = p.diode_a;
                        k = p.diode_k;
                    }
                    const auto& qcv = qc[idx];
                    qv = qcv[0] + qcv[1] * (v_[a] - v_[k] - qcv[2]);
                }
                else
                    qv = charge (p).first;
                break;
            }
            default:
                qv = 0.0;
                break;
        }
        if (theta != 1.0)
            i_prev[idx] = h.has_value() ? charge_current (idx, qv, *h).first : 0.0;
        this->q[idx] = qv;
    }
}

std::pair<std::vector<double>, std::vector<double>> Circuit::residual_and_jacobian (std::optional<double> h)
{
    for (size_t idx = 0; idx < parts.size(); ++idx)
    {
        const Part& p = parts[idx];
        switch (p.tag)
        {
            case Part::Tag::Diode:
                lim[idx] = { v_[p.diode_a] - v_[p.diode_k], 0.0 };
                break;
            case Part::Tag::Bjt:
            {
                const double pol = p.bjt_pnp ? -1.0 : 1.0;
                lim[idx] = { pol * (v_[p.bjt_b] - v_[p.bjt_e]), pol * (v_[p.bjt_b] - v_[p.bjt_c]) };
                break;
            }
            case Part::Tag::Jfet:
                lim[idx] = { v_[p.jfet_g] - v_[p.jfet_s], v_[p.jfet_g] - v_[p.jfet_d] };
                break;
            default:
                lim[idx] = { 0.0, 0.0 };
                break;
        }
    }
    load (h);
    return { f, jac };
}

std::tuple<std::vector<double>, std::vector<double>, std::vector<double>> Circuit::small_signal (Node input)
{
    const size_t n = nodes - held;
    auto [f0, gm] = residual_and_jacobian (std::nullopt);
    const double sv = v_[input];
    const double d = 1e-6;
    v_[input] = sv + d;
    auto [f1, unused] = residual_and_jacobian (std::nullopt);
    (void) unused;
    v_[input] = sv;
    std::vector<double> g_in;
    g_in.reserve (std::min (f1.size(), f0.size()));
    for (size_t k = 0; k < std::min (f1.size(), f0.size()); ++k)
        g_in.push_back ((f1[k] - f0[k]) / d);
    std::vector<double> c (n * n, 0.0);
    for (size_t idx = 0; idx < parts.size(); ++idx)
    {
        const Part p = parts[idx];
        Node a, b;
        switch (p.tag)
        {
            case Part::Tag::Capacitor:
                a = p.capacitor_a;
                b = p.capacitor_b;
                break;
            case Part::Tag::Junction:
                a = p.junction_a;
                b = p.junction_k;
                break;
            case Part::Tag::Diode:
                a = p.diode_a;
                b = p.diode_k;
                break;
            case Part::Tag::Diffusion:
                a = p.diffusion_a;
                b = p.diffusion_k;
                break;
            default:
                continue;
        }
        const double cap = charge (p).second;
        const auto fa = free (a);
        const auto fb = free (b);
        if (fa.has_value())
            c[*fa * n + *fa] += cap;
        if (fb.has_value())
            c[*fb * n + *fb] += cap;
        if (fa.has_value() && fb.has_value())
        {
            c[*fa * n + *fb] -= cap;
            c[*fb * n + *fa] -= cap;
        }
    }
    return { gm, c, g_in };
}

std::optional<size_t> Circuit::solved_index (Node n) const
{
    return free (n);
}

std::pair<size_t, size_t> Circuit::size() const
{
    return { nodes, held };
}

std::pair<size_t, size_t> Circuit::extent() const
{
    return { nodes - held, parts.size() };
}

size_t Circuit::linear_nodes() const
{
    std::vector<bool> nonlinear (nodes, false);
    for (const Part& p : parts)
    {
        if (! (p.tag == Part::Tag::Resistor || p.tag == Part::Tag::Capacitor))
        {
            const auto st = stamp_nodes (p)[0];
            if (st.has_value())
            {
                const auto& [a, b, on, n] = *st;
                nonlinear[a] = true;
                nonlinear[b] = true;
                for (size_t i = 0; i < n; ++i)
                    nonlinear[on[i]] = true;
            }
        }
        if (p.tag == Part::Tag::Bjt)
            for (const Node x : { p.bjt_c, p.bjt_b, p.bjt_e })
                nonlinear[x] = true;
    }
    size_t count = 0;
    for (size_t x = held; x < nodes; ++x)
        if (! nonlinear[x])
            count += 1;
    return count;
}

std::vector<size_t> Circuit::components() const
{
    const size_t n = nodes;
    std::vector<size_t> parent (n);
    for (size_t i = 0; i < n; ++i)
        parent[i] = i;
    auto find = [] (std::vector<size_t>& pr, size_t x) -> size_t {
        size_t r = x;
        while (pr[r] != r)
            r = pr[r];
        pr[x] = r;
        return r;
    };
    for (const Part& p : parts)
    {
        for (const auto& stamp : stamp_nodes (p))
        {
            if (! stamp.has_value())
                continue;
            const auto& [a, b, on, k] = *stamp;
            std::vector<size_t> ns;
            if (a >= held)
                ns.push_back (a);
            if (b >= held)
                ns.push_back (b);
            for (size_t i = 0; i < k; ++i)
                if (on[i] >= held)
                    ns.push_back (on[i]);
            for (size_t w = 0; w + 1 < ns.size(); ++w)
            {
                const size_t ra = find (parent, ns[w]);
                const size_t rb = find (parent, ns[w + 1]);
                parent[ra] = rb;
            }
        }
    }
    std::map<size_t, size_t> sizes;
    for (size_t x = held; x < n; ++x)
        sizes[find (parent, x)] += 1;
    std::vector<size_t> v;
    v.reserve (sizes.size());
    for (const auto& [root, count] : sizes)
        v.push_back (count);
    std::sort (v.begin(), v.end(), [] (size_t a, size_t b) { return b < a; });
    return v;
}

std::vector<std::pair<const char*, size_t>> Circuit::census() const
{
    static constexpr std::array<const char*, 7> names = {
        "resistor", "capacitor", "diode", "junction", "diffusion", "bjt", "jfet",
    };
    std::array<size_t, 7> counts {};
    for (const Part& p : parts)
    {
        size_t k = 0;
        switch (p.tag)
        {
            case Part::Tag::Resistor: k = 0; break;
            case Part::Tag::Capacitor: k = 1; break;
            case Part::Tag::Diode: k = 2; break;
            case Part::Tag::Junction: k = 3; break;
            case Part::Tag::Diffusion: k = 4; break;
            case Part::Tag::Bjt: k = 5; break;
            case Part::Tag::Jfet: k = 6; break;
        }
        counts[k] += 1;
    }
    std::vector<std::pair<const char*, size_t>> out;
    for (size_t i = 0; i < 7; ++i)
        if (counts[i] > 0)
            out.emplace_back (names[i], counts[i]);
    return out;
}
} // namespace ca72::mna
