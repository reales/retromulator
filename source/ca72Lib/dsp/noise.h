#pragma once
#include "common.h"
#include "devices.h"
#include "linear.h"
#include "mna.h"
#include "resample.h"

namespace ca72::voice
{
enum class Quality;
}

namespace ca72::noise
{
struct NoiseCircuit
{
    double r26 = 0.0;
    double vbr = 0.0;
    double rd = 0.0;

    static NoiseCircuit default_() { return NoiseCircuit { 1250.0, 7.5, 1e3 }; }
    bool operator== (const NoiseCircuit&) const = default;
};

struct NoiseLoads
{
    double white = 0.0;
    double pink = 0.0;
    double red = 0.0;

    bool operator== (const NoiseLoads&) const = default;
};

struct NoiseOut
{
    double white = 0.0;
    double pink = 0.0;
    double red = 0.0;

    static NoiseOut default_() { return {}; }
    bool operator== (const NoiseOut&) const = default;
};

inline constexpr mna::Node P10 = 1;
inline constexpr mna::Node N10 = 2;
inline constexpr mna::Node SRC = 3;
inline constexpr size_t HELD = 4;
inline constexpr mna::Node NX = 4;
inline constexpr mna::Node E15 = 5;
inline constexpr mna::Node B12 = 6;
inline constexpr mna::Node C12N = 7;
inline constexpr mna::Node E12 = 8;
inline constexpr mna::Node C4 = 9;
inline constexpr mna::Node E4 = 10;
inline constexpr mna::Node WHITE = 11;
inline constexpr mna::Node P1 = 12;
inline constexpr mna::Node N2 = 13;
inline constexpr mna::Node B3 = 14;
inline constexpr mna::Node Y3 = 15;
inline constexpr mna::Node C3N = 16;
inline constexpr mna::Node E3 = 17;
inline constexpr mna::Node PINK = 18;
inline constexpr mna::Node R1N = 19;
inline constexpr mna::Node B6 = 20;
inline constexpr mna::Node Z6 = 21;
inline constexpr mna::Node C6N = 22;
inline constexpr mna::Node E6 = 23;
inline constexpr mna::Node RED = 24;
inline constexpr mna::Node N3 = 25;
inline constexpr size_t NODES = 26;

inline constexpr size_t OVERSAMPLE = 4;

inline constexpr size_t LOAD_EVERY = 64;

ca72::Result<mna::Circuit, mna::NoConvergence> circuit (const NoiseCircuit& k, const NoiseLoads& loads);
ca72::Result<mna::Circuit, mna::NoConvergence> circuit_with (const NoiseCircuit& k, const NoiseLoads& loads, const devices::Bjt& q);

inline double open (double r) { return std::isfinite (r) ? r : 1e12; }

struct Gauss
{
    uint64_t state = 0;
    std::optional<double> spare;

    static Gauss new_ (uint64_t seed);
    uint32_t next_u32();
    double uniform();
    double next_uniform();
    double next();
};

struct Noise
{
    NoiseCircuit circuit;
    NoiseLoads loads;
    double rate;
    linear::SmallSignal model;
    linear::Discrete sys;
    std::array<size_t, 3> out;
    std::array<resample::Decimator, 3> dec;
    Gauss rng;
    std::tuple<linear::SmallSignal, linear::Discrete, std::array<size_t, 3>> other;
    bool plain;
    std::vector<std::pair<size_t, size_t>> pairs;
    double sigma;
    double density_;
    std::optional<NoiseLoads> pending;
    size_t since_loads;
    size_t os;

    static ca72::Result<Noise, mna::NoConvergence> new_ (NoiseCircuit circuit, NoiseLoads loads, double rate, uint64_t seed);

    static ca72::Result<std::tuple<linear::SmallSignal, std::array<size_t, 3>, std::vector<std::optional<size_t>>>, mna::NoConvergence>
        model_with (const NoiseCircuit& k, const NoiseLoads& loads, const devices::Bjt& q);

    void set_loads (NoiseLoads loads);
    void apply_loads (NoiseLoads loads);
    void set_density (double v_per_rt_hz);
    void set_quality (voice::Quality q);
    void reseed (uint64_t seed);
    double density() const { return density_; }
    std::tuple<const linear::SmallSignal&, const linear::Discrete&, std::array<size_t, 3>> model_of() const
    {
        return { model, sys, out };
    }
    NoiseOut tick();
    double latency() const;
};
} // namespace ca72::noise
