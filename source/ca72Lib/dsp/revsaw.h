#pragma once

#include "common.h"
#include "contour.h"
#include "devices.h"
#include "mna.h"
#include "voice_types.h"

namespace ca72::revsaw
{
struct RevSawCircuit
{
    double r164;
    double c10;
    double r165;
    double r175;
    double r171;
    double r182;
    double r172;
    double r176;
    ca72::devices::Bjt q37;
    double r_saw;

    static RevSawCircuit default_();
    bool operator== (const RevSawCircuit& o) const;
};

inline constexpr ca72::mna::Node P10 = 1;
inline constexpr ca72::mna::Node N10 = 2;
inline constexpr ca72::mna::Node SRC = 3;
inline constexpr size_t HELD = 4;
inline constexpr ca72::mna::Node SAW = 4;
inline constexpr ca72::mna::Node NA = 5;
inline constexpr ca72::mna::Node B37 = 6;
inline constexpr ca72::mna::Node C37 = 7;
inline constexpr ca72::mna::Node OUT = 8;
inline constexpr size_t NODES = 9;

struct RevSawOut
{
    double saw = 0.0;
    double rev = 0.0;
    double r_rev = 0.0;

    static RevSawOut default_() { return RevSawOut {}; }
    bool operator== (const RevSawOut&) const = default;
};

struct Plain
{
    double v_na;
    std::array<double, 2> x;
    std::array<double, 2> q;
    std::array<std::array<double, 2>, 2> near;
};

inline constexpr double PLAIN_TOL = 1e-9;

struct RevSaw
{
    RevSawCircuit circuit;
    ca72::mna::Circuit net;
    double dt;
    double r_out_;
    ca72::devices::BjtAt q37;
    std::array<std::pair<ca72::mna::Depletion, ca72::mna::DepletionConsts>, 2> dep;
    std::optional<Plain> plain;
    size_t failed;

    static ca72::Result<RevSaw, ca72::mna::NoConvergence> new_ (RevSawCircuit circuit, double rate);

    void set_quality (ca72::voice::Quality q);
    double r_out() const { return r_out_; }
    RevSawOut tick (double saw_open);
    std::pair<double, double> charge (size_t j, double v, std::array<double, 2>& near) const;
    RevSawOut tick_plain (Plain p, double saw_open);
};
} // namespace ca72::revsaw
