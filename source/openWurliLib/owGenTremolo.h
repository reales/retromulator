/**
 * OpenWurli DSP — Melange-generated Twin-T tremolo oscillator circuit solver
 * Auto-translated from Rust gen_tremolo.rs (melange-solver output)
 * Circuit: "Wurlitzer 200A Tremolo Oscillator"
 * GPL v3
 */
#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace openWurli {
namespace genTremolo {

// =============================================================================
// CONSTANTS: Compile-time circuit topology (Nodal solver)
// =============================================================================

static constexpr size_t N = 7;
static constexpr size_t N_NODES = 6;
static constexpr size_t N_AUG = 7;
static constexpr size_t M = 4;
static constexpr size_t MAX_ITER = 200;
static constexpr double TOL = 1.00000000000000006e-9;
static constexpr double SAMPLE_RATE = 48000.0;
static constexpr size_t OVERSAMPLING_FACTOR = 1;
static constexpr size_t INPUT_NODE = 5;
static constexpr size_t NUM_OUTPUTS = 1;

static constexpr std::array<size_t, NUM_OUTPUTS> OUTPUT_NODES = { 0 };
static constexpr std::array<double, NUM_OUTPUTS> OUTPUT_SCALES = { 1.00000000000000000e0 };
static constexpr double INPUT_RESISTANCE = 1.00000000000000000e7;

// G matrix: conductance matrix (sample-rate independent)
static const std::array<std::array<double, N>, N> G = {{
    {{
        2.14236546682102636e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -2.12765957446808509e-4,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        3.70370380370370393e-5,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        2.94117747058823528e-6,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        2.94117747058823528e-6,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000001000000001e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -2.12765957446808509e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        2.12865958446808507e-4,
        1.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// C matrix: capacitance matrix (sample-rate independent)
static const std::array<std::array<double, N>, N> C = {{
    {{
        1.20000999999999990e-7,
        -1.19999999999999989e-7,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.19999999999999989e-7,
        2.39999999999999979e-7,
        -1.19999999999999989e-7,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -1.19999999999999989e-7,
        1.19999999999999989e-7,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.19999999999999989e-7,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// Default A matrix: A = G + (2/T)*C (trapezoidal, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> A_DEFAULT = {{
    {{
        1.17343325466821007e-2,
        -1.15199999999999990e-2,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -2.12765957446808509e-4,
        0.00000000000000000e0,
    }},
    {{
        -1.15199999999999990e-2,
        2.30770370380370335e-2,
        -1.15199999999999990e-2,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -1.15199999999999990e-2,
        1.15229411774705869e-2,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        1.15229411774705869e-2,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000001000000001e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -2.12765957446808509e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        2.12865958446808507e-4,
        1.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// Default A_neg matrix: A_neg = (2/T)*C - G (trapezoidal history, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> A_NEG_DEFAULT = {{
    {{
        1.13058594533178968e-2,
        -1.15199999999999990e-2,
        0.00000000000000000e0,
        1.47058823529411773e-6,
        0.00000000000000000e0,
        2.12765957446808509e-4,
        0.00000000000000000e0,
    }},
    {{
        -1.15199999999999990e-2,
        2.30029629619629625e-2,
        -1.15199999999999990e-2,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -1.15199999999999990e-2,
        1.15170588225294111e-2,
        1.47058823529411773e-6,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        1.47058823529411773e-6,
        0.00000000000000000e0,
        1.47058823529411773e-6,
        1.15170588225294111e-2,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        -1.00000001000000001e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        2.12765957446808509e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        -2.12865958446808507e-4,
        -1.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// Default A_be matrix: A_be = G + (1/T)*C (backward Euler, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> A_BE_DEFAULT = {{
    {{
        5.97428454668210221e-3,
        -5.75999999999999950e-3,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -2.12765957446808509e-4,
        0.00000000000000000e0,
    }},
    {{
        -5.75999999999999950e-3,
        1.15570370380370362e-2,
        -5.75999999999999950e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -5.75999999999999950e-3,
        5.76294117747058743e-3,
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.47058823529411773e-6,
        0.00000000000000000e0,
        -1.47058823529411773e-6,
        5.76294117747058743e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000001000000001e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -2.12765957446808509e-4,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        2.12865958446808507e-4,
        1.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// Default A_neg_be matrix: (1/T)*C (backward Euler history, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> A_NEG_BE_DEFAULT = {{
    {{
        5.76004799999999938e-3,
        -5.75999999999999950e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -5.75999999999999950e-3,
        1.15199999999999990e-2,
        -5.75999999999999950e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -5.75999999999999950e-3,
        5.75999999999999950e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        5.75999999999999950e-3,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// N_v matrix: extracts controlling voltages from node voltages (M x N)
static constexpr std::array<std::array<double, N>, M> N_V = {{
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
        -1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// N_i matrix: maps nonlinear currents to node injections (N x M)
static constexpr std::array<std::array<double, M>, N> N_I = {{
    {{
        -1.00000000000000000e0,
        0.00000000000000000e0,
        -1.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        -1.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        1.00000000000000000e0,
        1.00000000000000000e0,
        0.00000000000000000e0,
        -1.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
}};

// S matrix: A^{-1} (precomputed inverse, trapezoidal, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> S_DEFAULT = {{
    {{
        3.93435898371326812e3,
        3.92075305545618494e3,
        3.91975242928666876e3,
        1.00236247540945600e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.37097656109206167e-1,
    }},
    {{
        3.92075305545618539e3,
        3.99369954001274164e3,
        3.99268029555644671e3,
        1.00993329806599119e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.34202777756635316e-1,
    }},
    {{
        3.91975242928666876e3,
        3.99268029555644671e3,
        4.07844471147694094e3,
        1.02075107714399671e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.33989878571631649e-1,
    }},
    {{
        1.00236247540945578e0,
        1.00993329806599097e0,
        1.02075107714399671e0,
        8.67836570338633635e1,
        0.00000000000000000e0,
        0.00000000000000000e0,
        2.13268611789245910e-4,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        9.99999990000000071e3,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
    }},
    {{
        8.37097656109206056e-1,
        8.34202777756635205e-1,
        8.33989878571631649e-1,
        2.13268611789245910e-4,
        0.00000000000000000e0,
        1.00000000000000000e0,
        -3.47600741682540368e-5,
    }},
}};

// K matrix: N_v * S * N_i (nonlinear kernel, trapezoidal, at SAMPLE_RATE)
static const std::array<std::array<double, M>, M> K_DEFAULT = {{
    {{
        -1.39197523292866699e4,
        -1.40784446114769416e4,
        -3.91975242928666876e3,
        9.99999990000000071e3,
    }},
    {{
        1.46065544265993594e1,
        -1.58692282190272181e2,
        1.46065544265993594e1,
        0.00000000000000000e0,
    }},
    {{
        9.99999990000000071e3,
        9.99999990000000071e3,
        0.00000000000000000e0,
        -9.99999990000000071e3,
    }},
    {{
        1.39343588837132684e4,
        1.39197523292866699e4,
        3.93435898371326812e3,
        -9.99999990000000071e3,
    }},
}};

// S_NI matrix: S * N_i (precomputed for final voltage recovery, N x M)
static const std::array<std::array<double, M>, N> S_NI_DEFAULT = {{
    {{
        -3.93435898371326812e3,
        -3.91975242928666876e3,
        -3.93435898371326812e3,
        0.00000000000000000e0,
    }},
    {{
        -3.92075305545618539e3,
        -3.99268029555644671e3,
        -3.92075305545618539e3,
        0.00000000000000000e0,
    }},
    {{
        -3.91975242928666876e3,
        -4.07844471147694094e3,
        -3.91975242928666876e3,
        0.00000000000000000e0,
    }},
    {{
        -1.00236247540945578e0,
        -1.02075107714399671e0,
        -1.00236247540945578e0,
        0.00000000000000000e0,
    }},
    {{
        9.99999990000000071e3,
        9.99999990000000071e3,
        0.00000000000000000e0,
        -9.99999990000000071e3,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -8.37097656109206056e-1,
        -8.33989878571631649e-1,
        -8.37097656109206056e-1,
        0.00000000000000000e0,
    }},
}};

// S_be matrix: A_be^{-1} (backward Euler, at SAMPLE_RATE)
static const std::array<std::array<double, N>, N> S_BE_DEFAULT = {{
    {{
        3.93725233914840283e3,
        3.91011508022033013e3,
        3.90812002284700020e3,
        2.00198335220834345e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.37713263648596551e-1,
    }},
    {{
        3.91011508022033013e3,
        4.05557935955550010e3,
        4.05351007066186457e3,
        2.03215911745951017e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.31939378770283122e-1,
    }},
    {{
        3.90812002284699929e3,
        4.05351007066186412e3,
        4.22496435574593397e3,
        2.07540174981669434e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        8.31514898478085018e-1,
    }},
    {{
        2.00198335220834300e0,
        2.03215911745951017e0,
        2.07540174981669434e0,
        1.73523547327525279e2,
        0.00000000000000000e0,
        0.00000000000000000e0,
        4.25953904725179396e-4,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        9.99999990000000071e3,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        1.00000000000000000e0,
    }},
    {{
        8.37713263648596329e-1,
        8.31939378770282900e-1,
        8.31514898478084796e-1,
        4.25953904725179342e-4,
        0.00000000000000000e0,
        1.00000000000000000e0,
        -3.46290938407241690e-5,
    }},
}};

// K_be matrix: N_v * S_be * N_i (backward Euler kernel)
static const std::array<std::array<double, M>, M> K_BE_DEFAULT = {{
    {{
        -1.39081199228469995e4,
        -1.42249642557459338e4,
        -3.90812002284699929e3,
        9.99999990000000071e3,
    }},
    {{
        2.91323163014035345e1,
        -3.16844332898933772e2,
        2.91323163014035345e1,
        0.00000000000000000e0,
    }},
    {{
        9.99999990000000071e3,
        9.99999990000000071e3,
        0.00000000000000000e0,
        -9.99999990000000071e3,
    }},
    {{
        1.39372522391484035e4,
        1.39081199228470014e4,
        3.93725233914840283e3,
        -9.99999990000000071e3,
    }},
}};

// S_NI_be matrix: S_be * N_i (backward Euler, for final voltage recovery)
static const std::array<std::array<double, M>, N> S_NI_BE_DEFAULT = {{
    {{
        -3.93725233914840283e3,
        -3.90812002284700020e3,
        -3.93725233914840283e3,
        0.00000000000000000e0,
    }},
    {{
        -3.91011508022033013e3,
        -4.05351007066186457e3,
        -3.91011508022033013e3,
        0.00000000000000000e0,
    }},
    {{
        -3.90812002284699929e3,
        -4.22496435574593397e3,
        -3.90812002284699929e3,
        0.00000000000000000e0,
    }},
    {{
        -2.00198335220834300e0,
        -2.07540174981669434e0,
        -2.00198335220834300e0,
        0.00000000000000000e0,
    }},
    {{
        9.99999990000000071e3,
        9.99999990000000071e3,
        0.00000000000000000e0,
        -9.99999990000000071e3,
    }},
    {{
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
        0.00000000000000000e0,
    }},
    {{
        -8.37713263648596329e-1,
        -8.31514898478084796e-1,
        -8.37713263648596329e-1,
        0.00000000000000000e0,
    }},
}};

// RHS constant contribution from DC sources (trapezoidal: node rows x2, VS rows x1)
static constexpr std::array<double, N> RHS_CONST = {
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    1.50000000000000000e1,
};

// RHS constant contribution from DC sources (backward Euler: all rows x1)
static constexpr std::array<double, N> RHS_CONST_BE = {
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    0.00000000000000000e0,
    1.50000000000000000e1,
};

static constexpr bool DC_OP_CONVERGED = true;

// =============================================================================
// DEVICE MODELS
// =============================================================================

static constexpr double DEVICE_0_IS     = 1.40000000000000003e-14;
static constexpr double DEVICE_0_VT     = 2.58519910000000012e-2;
static constexpr double DEVICE_0_BETA_F = 2.00000000000000000e2;
static constexpr double DEVICE_0_BETA_R = 3.00000000000000000e0;
static constexpr double DEVICE_0_NF     = 1.00000000000000000e0;
static constexpr double DEVICE_0_NR     = 1.00000000000000000e0;
static constexpr double DEVICE_0_ISE    = 0.00000000000000000e0;
static constexpr double DEVICE_0_NE     = 1.50000000000000000e0;
static constexpr double DEVICE_0_ISC    = 0.00000000000000000e0;
static constexpr double DEVICE_0_NC     = 2.00000000000000000e0;
static constexpr double DEVICE_0_SIGN   = 1.0;
static constexpr bool   DEVICE_0_USE_GP = false;
static const     double DEVICE_0_VAF    = std::numeric_limits<double>::infinity();
static const     double DEVICE_0_VAR    = std::numeric_limits<double>::infinity();
static const     double DEVICE_0_IKF    = std::numeric_limits<double>::infinity();
static const     double DEVICE_0_IKR    = std::numeric_limits<double>::infinity();
static constexpr double DEVICE_0_VCRIT  = 7.21213101001093038e-1;

static constexpr double DEVICE_1_IS     = 1.40000000000000003e-14;
static constexpr double DEVICE_1_VT     = 2.58519910000000012e-2;
static constexpr double DEVICE_1_BETA_F = 2.00000000000000000e2;
static constexpr double DEVICE_1_BETA_R = 3.00000000000000000e0;
static constexpr double DEVICE_1_NF     = 1.00000000000000000e0;
static constexpr double DEVICE_1_NR     = 1.00000000000000000e0;
static constexpr double DEVICE_1_ISE    = 0.00000000000000000e0;
static constexpr double DEVICE_1_NE     = 1.50000000000000000e0;
static constexpr double DEVICE_1_ISC    = 0.00000000000000000e0;
static constexpr double DEVICE_1_NC     = 2.00000000000000000e0;
static constexpr double DEVICE_1_SIGN   = 1.0;
static constexpr bool   DEVICE_1_USE_GP = false;
static const     double DEVICE_1_VAF    = std::numeric_limits<double>::infinity();
static const     double DEVICE_1_VAR    = std::numeric_limits<double>::infinity();
static const     double DEVICE_1_IKF    = std::numeric_limits<double>::infinity();
static const     double DEVICE_1_IKR    = std::numeric_limits<double>::infinity();
static constexpr double DEVICE_1_VCRIT  = 7.21213101001093038e-1;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

inline double fast_exp(double x)
{
    x = std::clamp(x, -40.0, 40.0);
    static constexpr double LN2_INV = 1.4426950408889634;
    static constexpr double LN2_HI  = 0.6931471803691238;
    static constexpr double LN2_LO  = 1.9082149292705877e-10;
    static constexpr double SHIFT   = 6755399441055744.0; // 2^52 + 2^51
    double z = x * LN2_INV + SHIFT;
    uint64_t z_bits;
    std::memcpy(&z_bits, &z, sizeof(z_bits));
    uint64_t shift_bits;
    std::memcpy(&shift_bits, &SHIFT, sizeof(shift_bits));
    int64_t n_i64 = static_cast<int64_t>(z_bits) - static_cast<int64_t>(shift_bits);
    double n_dbl = static_cast<double>(n_i64);
    double f = (x - n_dbl * LN2_HI) - n_dbl * LN2_LO;
    double p = 1.0
        + f * (1.0
            + f * (0.5
                + f * (0.16666666666666607
                    + f * (0.04166666666665876 + f * 0.008333333333492337))));
    uint64_t pow2n_bits = static_cast<uint64_t>(1023 + n_i64) << 52;
    double pow2n;
    std::memcpy(&pow2n, &pow2n_bits, sizeof(pow2n));
    return p * pow2n;
}

inline double fast_ln(double x)
{
    uint64_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    int64_t e = static_cast<int64_t>((bits >> 52) & 0x7FF) - 1023;
    uint64_t m_bits = (bits & 0x000FFFFFFFFFFFFF) | 0x3FF0000000000000;
    double m;
    std::memcpy(&m, &m_bits, sizeof(m));
    double u = (m - 1.0) / (m + 1.0);
    double u2 = u * u;
    double ln_m = 2.0 * u * (1.0 + u2 * (0.3333333333333333 + u2 * (0.2 + u2 * 0.14285714285714285)));
    return ln_m + static_cast<double>(e) * 0.6931471805599453;
}

inline double pnjlim(double vnew, double vold, double vt, double vcrit)
{
    if (vnew > vcrit && std::abs(vnew - vold) > vt + vt) {
        if (vold >= 0.0) {
            double arg = 1.0 + (vnew - vold) / vt;
            if (arg > 0.0) {
                return vold + vt * std::log(arg);
            } else {
                return vcrit;
            }
        } else {
            return vt * std::log(vnew / vt);
        }
    }
    return vnew;
}

inline double fetlim(double vnew, double vold, double vto)
{
    double delv = vnew - vold;
    double vtox = vto + 3.5;
    double vtsthi = std::abs(2.0 * (vold - vto)) + 2.0;
    double vtstlo = vtsthi / 2.0 + 2.0;

    if (vold >= vto) {
        if (vold >= vtox) {
            if (delv <= 0.0) {
                if (vnew >= vtox) {
                    if (-delv > vtstlo) {
                        return vold - vtstlo;
                    }
                } else {
                    return std::max(vnew, vto + 2.0);
                }
            } else {
                if (delv >= vtsthi) {
                    return vold + vtsthi;
                }
            }
        } else {
            if (delv <= 0.0) {
                return std::max(vnew, vto - 0.5);
            } else {
                return std::min(vnew, vto + 4.0);
            }
        }
    } else {
        if (delv <= 0.0) {
            if (-delv > vtstlo) {
                return vold - vtstlo;
            }
        } else if (vnew <= vto + 0.5) {
            if (delv > vtstlo) {
                return vold + vtstlo;
            }
        } else {
            return vto + 0.5;
        }
    }
    return vnew;
}

inline double safe_exp(double x)
{
    return fast_exp(x);
}

// =============================================================================
// BJT DEVICE FUNCTIONS
// =============================================================================

inline double bjt_qb(
    double vbe, double vbc,
    double is, double vt, double nf, double nr, double sign,
    bool use_gp, double vaf, double var, double ikf, double ikr)
{
    if (!use_gp) return 1.0;
    double vbe_eff = sign * vbe;
    double vbc_eff = sign * vbc;
    double q1_denom = 1.0 - vbe_eff / var - vbc_eff / vaf;
    if (std::abs(q1_denom) < 1e-30) return 1.0;
    double q1 = 1.0 / q1_denom;
    double nf_vt = nf * vt;
    double nr_vt = nr * vt;
    double exp_be = safe_exp(vbe_eff / nf_vt);
    double exp_bc = safe_exp(vbc_eff / nr_vt);
    double q2 = is * exp_be / ikf + is * exp_bc / ikr;
    return q1 * (1.0 + std::sqrt(std::max(1.0 + 4.0 * q2, 0.0))) / 2.0;
}

inline double bjt_ic(
    double vbe, double vbc,
    double is, double vt, double nf, double nr, double beta_r, double sign,
    bool use_gp, double vaf, double var, double ikf, double ikr)
{
    double vbe_eff = sign * vbe;
    double vbc_eff = sign * vbc;
    double nf_vt = nf * vt;
    double nr_vt = nr * vt;
    double exp_be = safe_exp(vbe_eff / nf_vt);
    double exp_bc = safe_exp(vbc_eff / nr_vt);
    double i_cc = is * (exp_be - exp_bc);
    double qb = bjt_qb(vbe, vbc, is, vt, nf, nr, sign, use_gp, vaf, var, ikf, ikr);
    return sign * (i_cc / qb - is / beta_r * (exp_bc - 1.0));
}

inline double bjt_ib(
    double vbe, double vbc,
    double is, double vt, double nf, double nr,
    double beta_f, double beta_r, double sign,
    double ise, double ne, double isc, double nc,
    bool use_gp, double vaf, double var, double ikf, double ikr)
{
    double vbe_eff = sign * vbe;
    double vbc_eff = sign * vbc;
    double nf_vt = nf * vt;
    double nr_vt = nr * vt;
    double exp_be = safe_exp(vbe_eff / nf_vt);
    double exp_bc = safe_exp(vbc_eff / nr_vt);
    double ib_fwd = is / beta_f * (exp_be - 1.0);
    double ib_rev = is / beta_r * (exp_bc - 1.0);
    double ib;
    if (use_gp) {
        double qb = bjt_qb(vbe, vbc, is, vt, nf, nr, sign, true, vaf, var, ikf, ikr);
        ib = ib_fwd / qb + ib_rev;
    } else {
        ib = ib_fwd + ib_rev;
    }
    if (ise > 0.0) {
        double ne_vt = ne * vt;
        double exp_be_leak = safe_exp(vbe_eff / ne_vt);
        ib += ise * (exp_be_leak - 1.0);
    }
    if (isc > 0.0) {
        double nc_vt = nc * vt;
        double exp_bc_leak = safe_exp(vbc_eff / nc_vt);
        ib += isc * (exp_bc_leak - 1.0);
    }
    return sign * ib;
}

inline std::array<double, 4> bjt_jacobian(
    double vbe, double vbc,
    double is, double vt, double nf, double nr,
    double beta_f, double beta_r, double sign,
    bool use_gp, double vaf, double var, double ikf, double ikr,
    double ise, double ne, double isc, double nc)
{
    double vbe_eff = sign * vbe;
    double vbc_eff = sign * vbc;
    double nf_vt = nf * vt;
    double nr_vt = nr * vt;
    double exp_be = safe_exp(vbe_eff / nf_vt);
    double exp_bc = safe_exp(vbc_eff / nr_vt);

    double dib_fwd_dvbe = (is / (beta_f * nf_vt)) * exp_be;
    double dib_rev_dvbc = (is / (beta_r * nr_vt)) * exp_bc;
    double dib_leak_dvbe = 0.0;
    double dib_leak_dvbc = 0.0;
    if (ise > 0.0) {
        double ne_vt = ne * vt;
        double exp_be_leak = safe_exp(vbe_eff / ne_vt);
        dib_leak_dvbe = (ise / ne_vt) * exp_be_leak;
    }
    if (isc > 0.0) {
        double nc_vt = nc * vt;
        double exp_bc_leak = safe_exp(vbc_eff / nc_vt);
        dib_leak_dvbc = (isc / nc_vt) * exp_bc_leak;
    }

    if (!use_gp) {
        double dic_dvbe = is / nf_vt * exp_be;
        double dic_dvbc = -(is / nr_vt) * exp_bc - (is / (beta_r * nr_vt)) * exp_bc;
        return {{
            dic_dvbe,
            dic_dvbc,
            dib_fwd_dvbe + dib_leak_dvbe,
            dib_rev_dvbc + dib_leak_dvbc,
        }};
    }

    double icc = is * (exp_be - exp_bc);
    double dicc_dvbe = is / nf_vt * exp_be;
    double dicc_dvbc = -is / nr_vt * exp_bc;

    double q1_denom = 1.0 - vbe_eff / var - vbc_eff / vaf;
    double q1, dq1_dvbe, dq1_dvbc;
    if (std::abs(q1_denom) < 1e-30) {
        q1 = 1.0; dq1_dvbe = 0.0; dq1_dvbc = 0.0;
    } else {
        q1 = 1.0 / q1_denom;
        dq1_dvbe = q1 * q1 / var;
        dq1_dvbc = q1 * q1 / vaf;
    }

    double q2 = is * exp_be / ikf + is * exp_bc / ikr;
    double dq2_dvbe = is / (nf_vt * ikf) * exp_be;
    double dq2_dvbc = is / (nr_vt * ikr) * exp_bc;

    double disc = std::max(1.0 + 4.0 * q2, 0.0);
    double d = std::sqrt(disc);
    double dd_dvbe = (d > 1e-15) ? 2.0 * dq2_dvbe / d : 0.0;
    double dd_dvbc = (d > 1e-15) ? 2.0 * dq2_dvbc / d : 0.0;

    double qb = q1 * (1.0 + d) / 2.0;
    double dqb_dvbe = dq1_dvbe * (1.0 + d) / 2.0 + q1 * dd_dvbe / 2.0;
    double dqb_dvbc = dq1_dvbc * (1.0 + d) / 2.0 + q1 * dd_dvbc / 2.0;

    double qb2 = std::max(qb * qb, 1e-30);
    double quotient_dvbe = (dicc_dvbe * qb - icc * dqb_dvbe) / qb2;
    double quotient_dvbc = (dicc_dvbc * qb - icc * dqb_dvbc) / qb2;

    double d_bc_term_dvbc = is / (beta_r * nr_vt) * exp_bc;

    double ib_fwd = is / beta_f * (exp_be - 1.0);
    double dib_fwd_gp_dvbe = (dib_fwd_dvbe * qb - ib_fwd * dqb_dvbe) / qb2;
    double dib_fwd_gp_dvbc = -ib_fwd * dqb_dvbc / qb2;

    return {{
        quotient_dvbe,
        quotient_dvbc - d_bc_term_dvbc,
        dib_fwd_gp_dvbe + dib_leak_dvbe,
        dib_fwd_gp_dvbc + dib_rev_dvbc + dib_leak_dvbc,
    }};
}

struct BjtResult {
    double ic;
    double ib;
    std::array<double, 4> jac;
};

inline BjtResult bjt_evaluate(
    double vbe, double vbc,
    double is, double vt, double nf, double nr,
    double beta_f, double beta_r, double sign,
    bool use_gp, double vaf, double var, double ikf, double ikr,
    double ise, double ne, double isc, double nc)
{
    double vbe_eff = sign * vbe;
    double vbc_eff = sign * vbc;
    double nf_vt = nf * vt;
    double nr_vt = nr * vt;

    double exp_be = safe_exp(vbe_eff / nf_vt);
    double exp_bc = safe_exp(vbc_eff / nr_vt);

    double exp_be_leak = (ise > 0.0) ? safe_exp(vbe_eff / (ne * vt)) : 0.0;
    double exp_bc_leak = (isc > 0.0) ? safe_exp(vbc_eff / (nc * vt)) : 0.0;

    double i_cc = is * (exp_be - exp_bc);

    double ib_fwd = is / beta_f * (exp_be - 1.0);
    double ib_rev = is / beta_r * (exp_bc - 1.0);
    double ib_leak_be = (ise > 0.0) ? ise * (exp_be_leak - 1.0) : 0.0;
    double ib_leak_bc = (isc > 0.0) ? isc * (exp_bc_leak - 1.0) : 0.0;

    double dib_fwd_dvbe = (is / (beta_f * nf_vt)) * exp_be;
    double dib_rev_dvbc = (is / (beta_r * nr_vt)) * exp_bc;
    double dib_leak_dvbe = (ise > 0.0) ? (ise / (ne * vt)) * exp_be_leak : 0.0;
    double dib_leak_dvbc = (isc > 0.0) ? (isc / (nc * vt)) * exp_bc_leak : 0.0;

    if (!use_gp) {
        double ic_val = sign * (i_cc - is / beta_r * (exp_bc - 1.0));
        double ib_val = sign * (ib_fwd + ib_rev + ib_leak_be + ib_leak_bc);
        double dic_dvbe = is / nf_vt * exp_be;
        double dic_dvbc = -(is / nr_vt) * exp_bc - (is / (beta_r * nr_vt)) * exp_bc;
        return {
            ic_val, ib_val,
            {{ dic_dvbe, dic_dvbc, dib_fwd_dvbe + dib_leak_dvbe, dib_rev_dvbc + dib_leak_dvbc }}
        };
    }

    // Gummel-Poon
    double q1_denom = 1.0 - vbe_eff / var - vbc_eff / vaf;
    double q1, dq1_dvbe, dq1_dvbc;
    if (std::abs(q1_denom) < 1e-30) {
        q1 = 1.0; dq1_dvbe = 0.0; dq1_dvbc = 0.0;
    } else {
        q1 = 1.0 / q1_denom;
        dq1_dvbe = q1 * q1 / var;
        dq1_dvbc = q1 * q1 / vaf;
    }

    double q2 = is * exp_be / ikf + is * exp_bc / ikr;
    double dq2_dvbe = is / (nf_vt * ikf) * exp_be;
    double dq2_dvbc = is / (nr_vt * ikr) * exp_bc;

    double disc = std::max(1.0 + 4.0 * q2, 0.0);
    double d = std::sqrt(disc);
    double dd_dvbe = (d > 1e-15) ? 2.0 * dq2_dvbe / d : 0.0;
    double dd_dvbc = (d > 1e-15) ? 2.0 * dq2_dvbc / d : 0.0;

    double qb = q1 * (1.0 + d) / 2.0;
    double dqb_dvbe = dq1_dvbe * (1.0 + d) / 2.0 + q1 * dd_dvbe / 2.0;
    double dqb_dvbc = dq1_dvbc * (1.0 + d) / 2.0 + q1 * dd_dvbc / 2.0;

    double ic_val = sign * (i_cc / qb - is / beta_r * (exp_bc - 1.0));
    double ib_val = sign * (ib_fwd / qb + ib_rev + ib_leak_be + ib_leak_bc);

    double dicc_dvbe = is / nf_vt * exp_be;
    double dicc_dvbc = -is / nr_vt * exp_bc;
    double qb2 = std::max(qb * qb, 1e-30);
    double quotient_dvbe = (dicc_dvbe * qb - i_cc * dqb_dvbe) / qb2;
    double quotient_dvbc = (dicc_dvbc * qb - i_cc * dqb_dvbc) / qb2;
    double d_bc_term_dvbc = is / (beta_r * nr_vt) * exp_bc;

    double dib_fwd_gp_dvbe = (dib_fwd_dvbe * qb - ib_fwd * dqb_dvbe) / qb2;
    double dib_fwd_gp_dvbc = -ib_fwd * dqb_dvbc / qb2;

    return {
        ic_val, ib_val,
        {{
            quotient_dvbe,
            quotient_dvbc - d_bc_term_dvbc,
            dib_fwd_gp_dvbe + dib_leak_dvbe,
            dib_fwd_gp_dvbc + dib_rev_dvbc + dib_leak_dvbc,
        }}
    };
}

inline BjtResult bjt_with_parasitics(
    double vbe_ext, double vbc_ext,
    double is, double vt, double nf, double nr,
    double beta_f, double beta_r, double sign,
    bool use_gp, double vaf, double var, double ikf, double ikr,
    double ise, double ne, double isc, double nc,
    double rb, double rc, double re)
{
    static constexpr size_t INNER_MAX_ITER = 15;
    static constexpr double INNER_TOL = 1e-10;

    double vbe_int = vbe_ext;
    double vbc_int = vbc_ext;

    for (size_t iter = 0; iter < INNER_MAX_ITER; ++iter) {
        auto result = bjt_evaluate(
            vbe_int, vbc_int, is, vt, nf, nr, beta_f, beta_r, sign,
            use_gp, vaf, var, ikf, ikr, ise, ne, isc, nc);
        double ic = result.ic;
        double ib = result.ib;
        double dic_dvbe = result.jac[0];
        double dic_dvbc = result.jac[1];
        double dib_dvbe = result.jac[2];
        double dib_dvbc = result.jac[3];

        double f1 = vbe_int - vbe_ext + ib * rb + (ic + ib) * re;
        double f2 = vbc_int - vbc_ext + ib * rb - ic * rc;

        if (std::abs(f1) < INNER_TOL && std::abs(f2) < INNER_TOL)
            break;

        double j11 = 1.0 + dib_dvbe * rb + (dic_dvbe + dib_dvbe) * re;
        double j12 = dib_dvbc * rb + (dic_dvbc + dib_dvbc) * re;
        double j21 = dib_dvbe * rb - dic_dvbe * rc;
        double j22 = 1.0 + dib_dvbc * rb - dic_dvbc * rc;

        double det = j11 * j22 - j12 * j21;
        if (std::abs(det) < 1e-30)
            break;
        double inv_det = 1.0 / det;
        double dvbe = (j22 * f1 - j12 * f2) * inv_det;
        double dvbc = (j11 * f2 - j21 * f1) * inv_det;

        double max_step = 4.0 * vt;
        dvbe = std::clamp(dvbe, -max_step, max_step);
        dvbc = std::clamp(dvbc, -max_step, max_step);

        vbe_int -= dvbe;
        vbc_int -= dvbc;
    }

    auto result = bjt_evaluate(
        vbe_int, vbc_int, is, vt, nf, nr, beta_f, beta_r, sign,
        use_gp, vaf, var, ikf, ikr, ise, ne, isc, nc);
    double ic = result.ic;
    double ib = result.ib;
    double dic_dvbe = result.jac[0];
    double dic_dvbc = result.jac[1];
    double dib_dvbe = result.jac[2];
    double dib_dvbc = result.jac[3];

    double j11 = 1.0 + dib_dvbe * rb + (dic_dvbe + dib_dvbe) * re;
    double j12 = dib_dvbc * rb + (dic_dvbc + dib_dvbc) * re;
    double j21 = dib_dvbe * rb - dic_dvbe * rc;
    double j22 = 1.0 + dib_dvbc * rb - dic_dvbc * rc;

    double det = j11 * j22 - j12 * j21;
    if (std::abs(det) < 1e-30)
        return result;

    double inv_det = 1.0 / det;
    double fi11 =  j22 * inv_det;
    double fi12 = -j12 * inv_det;
    double fi21 = -j21 * inv_det;
    double fi22 =  j11 * inv_det;

    double dic_dvbe_ext = dic_dvbe * fi11 + dic_dvbc * fi21;
    double dic_dvbc_ext = dic_dvbe * fi12 + dic_dvbc * fi22;
    double dib_dvbe_ext = dib_dvbe * fi11 + dib_dvbc * fi21;
    double dib_dvbc_ext = dib_dvbe * fi12 + dib_dvbc * fi22;

    return { ic, ib, {{ dic_dvbe_ext, dic_dvbc_ext, dib_dvbe_ext, dib_dvbc_ext }} };
}

// =============================================================================
// STATE STRUCTURE (Nodal solver)
// =============================================================================

// DC operating point: steady-state node voltages
static constexpr std::array<double, N> DC_OP = {
    4.26487382569817974e0,
    0.00000000000000000e0,
    1.24643449212847934e0,
    2.75565322199124418e0,
    6.66528752563515625e-1,
    1.50000000000000000e1,
    -2.28706941378761840e-3,
};

// DC operating point: nonlinear device currents at bias point
static constexpr std::array<double, M> DC_NL_I = {
    7.72892333516560844e-5,
    3.86446161998280494e-7,
    2.20456071910682351e-3,
    1.10228035907741172e-5,
};

struct CircuitState {
    std::array<double, N> v_prev;
    std::array<double, M> i_nl_prev;
    std::array<double, M> i_nl_prev_prev;
    std::array<double, N> dc_operating_point;
    double input_prev;
    uint32_t last_nr_iterations;
    double diag_peak_output;
    uint64_t diag_clamp_count;
    uint64_t diag_nr_max_iter_count;
    uint64_t diag_be_fallback_count;
    uint64_t diag_nan_reset_count;

    std::array<std::array<double, N>, N> a;
    std::array<std::array<double, N>, N> a_neg;
    std::array<std::array<double, N>, N> a_be;
    std::array<std::array<double, N>, N> a_neg_be;
    std::array<std::array<double, N>, N> s;
    std::array<std::array<double, M>, M> k;
    std::array<std::array<double, M>, N> s_ni;
    std::array<std::array<double, N>, N> s_be;
    std::array<std::array<double, M>, M> k_be;
    std::array<std::array<double, M>, N> s_ni_be;

    double device_0_is;
    double device_0_vt;
    double device_0_bf;
    double device_0_br;
    double device_1_is;
    double device_1_vt;
    double device_1_bf;
    double device_1_br;

    CircuitState()
        : v_prev(DC_OP)
        , i_nl_prev(DC_NL_I)
        , i_nl_prev_prev(DC_NL_I)
        , dc_operating_point(DC_OP)
        , input_prev(0.0)
        , last_nr_iterations(0)
        , diag_peak_output(0.0)
        , diag_clamp_count(0)
        , diag_nr_max_iter_count(0)
        , diag_be_fallback_count(0)
        , diag_nan_reset_count(0)
        , a(A_DEFAULT)
        , a_neg(A_NEG_DEFAULT)
        , a_be(A_BE_DEFAULT)
        , a_neg_be(A_NEG_BE_DEFAULT)
        , s(S_DEFAULT)
        , k(K_DEFAULT)
        , s_ni(S_NI_DEFAULT)
        , s_be(S_BE_DEFAULT)
        , k_be(K_BE_DEFAULT)
        , s_ni_be(S_NI_BE_DEFAULT)
        , device_0_is(DEVICE_0_IS)
        , device_0_vt(DEVICE_0_VT)
        , device_0_bf(DEVICE_0_BETA_F)
        , device_0_br(DEVICE_0_BETA_R)
        , device_1_is(DEVICE_1_IS)
        , device_1_vt(DEVICE_1_VT)
        , device_1_bf(DEVICE_1_BETA_F)
        , device_1_br(DEVICE_1_BETA_R)
    {}

    void reset()
    {
        v_prev = dc_operating_point;
        i_nl_prev = DC_NL_I;
        i_nl_prev_prev = DC_NL_I;
        input_prev = 0.0;
        last_nr_iterations = 0;
        diag_peak_output = 0.0;
        diag_clamp_count = 0;
        diag_nr_max_iter_count = 0;
        diag_be_fallback_count = 0;
        diag_nan_reset_count = 0;
        s = S_DEFAULT;
        k = K_DEFAULT;
        s_ni = S_NI_DEFAULT;
        s_be = S_BE_DEFAULT;
        k_be = K_BE_DEFAULT;
        s_ni_be = S_NI_BE_DEFAULT;
        device_0_is = DEVICE_0_IS;
        device_0_vt = DEVICE_0_VT;
        device_0_bf = DEVICE_0_BETA_F;
        device_0_br = DEVICE_0_BETA_R;
        device_1_is = DEVICE_1_IS;
        device_1_vt = DEVICE_1_VT;
        device_1_bf = DEVICE_1_BETA_F;
        device_1_br = DEVICE_1_BETA_R;
    }

    void setDcOperatingPoint(const std::array<double, N>& v_dc)
    {
        dc_operating_point = v_dc;
        v_prev = v_dc;
    }

    void setSampleRate(double sample_rate)
    {
        if (!(sample_rate > 0.0 && std::isfinite(sample_rate)))
            return;

        if (std::abs(sample_rate - SAMPLE_RATE) < 0.5) {
            a = A_DEFAULT;
            a_neg = A_NEG_DEFAULT;
            a_be = A_BE_DEFAULT;
            a_neg_be = A_NEG_BE_DEFAULT;
            s = S_DEFAULT;
            s_be = S_BE_DEFAULT;
            k = K_DEFAULT;
            s_ni = S_NI_DEFAULT;
            k_be = K_BE_DEFAULT;
            s_ni_be = S_NI_BE_DEFAULT;
            return;
        }

        double internal_rate = sample_rate * 1.0;
        rebuildMatrices(internal_rate);
    }

    void rebuildMatrices(double internal_rate);
};

// =============================================================================
// MATRIX INVERSION (for rebuild_matrices)
// =============================================================================

inline bool invertN(const std::array<std::array<double, N>, N>& a_in,
                    std::array<std::array<double, N>, N>& result)
{
    std::array<std::array<double, 2 * N>, N> aug;
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < 2 * N; ++j)
            aug[i][j] = 0.0;
        for (size_t j = 0; j < N; ++j)
            aug[i][j] = a_in[i][j];
        aug[i][N + i] = 1.0;
    }

    for (size_t col = 0; col < N; ++col) {
        size_t max_row = col;
        double max_val = std::abs(aug[col][col]);
        for (size_t row = col + 1; row < N; ++row) {
            double v = std::abs(aug[row][col]);
            if (v > max_val) {
                max_val = v;
                max_row = row;
            }
        }
        if (max_val < 1e-30)
            return false;
        if (max_row != col)
            std::swap(aug[col], aug[max_row]);
        double pivot = aug[col][col];
        for (size_t row = col + 1; row < N; ++row) {
            double factor = aug[row][col] / pivot;
            for (size_t j = col; j < 2 * N; ++j)
                aug[row][j] -= factor * aug[col][j];
        }
    }

    for (size_t col_idx = N; col_idx > 0; --col_idx) {
        size_t col = col_idx - 1;
        double pivot = aug[col][col];
        if (std::abs(pivot) < 1e-30)
            return false;
        for (size_t j = 0; j < 2 * N; ++j)
            aug[col][j] /= pivot;
        for (size_t row = 0; row < col; ++row) {
            double factor = aug[row][col];
            for (size_t j = 0; j < 2 * N; ++j)
                aug[row][j] -= factor * aug[col][j];
        }
    }

    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < N; ++j)
            result[i][j] = aug[i][N + j];
    return true;
}

// =============================================================================
// rebuild_matrices implementation
// =============================================================================

inline void CircuitState::rebuildMatrices(double internal_rate)
{
    double alpha_val = 2.0 * internal_rate;
    double alpha_be = internal_rate;

    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j) {
            a[i][j] = G[i][j] + alpha_val * C[i][j];
            a_neg[i][j] = alpha_val * C[i][j] - G[i][j];
            a_be[i][j] = G[i][j] + alpha_be * C[i][j];
            a_neg_be[i][j] = alpha_be * C[i][j];
        }
    }
    for (size_t i = 6; i < 7; ++i) {
        for (size_t j = 0; j < N; ++j) {
            a_neg[i][j] = 0.0;
            a_neg_be[i][j] = 0.0;
        }
    }

    // Recompute S = A^{-1} (trapezoidal)
    std::array<std::array<double, N>, N> inv;
    if (invertN(a, inv)) {
        s = inv;
        // K = N_v * S * N_i
        for (size_t i = 0; i < M; ++i) {
            for (size_t j = 0; j < M; ++j) {
                double sum = 0.0;
                for (size_t aa = 0; aa < N; ++aa) {
                    double s_ni_aj = 0.0;
                    for (size_t b = 0; b < N; ++b)
                        s_ni_aj += s[aa][b] * N_I[b][j];
                    sum += N_V[i][aa] * s_ni_aj;
                }
                k[i][j] = sum;
            }
        }
        // S_NI = S * N_i
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < M; ++j) {
                double sum = 0.0;
                for (size_t aa = 0; aa < N; ++aa)
                    sum += s[i][aa] * N_I[aa][j];
                s_ni[i][j] = sum;
            }
        }
    }
    // Recompute S_be = A_be^{-1} (backward Euler)
    if (invertN(a_be, inv)) {
        s_be = inv;
        for (size_t i = 0; i < M; ++i) {
            for (size_t j = 0; j < M; ++j) {
                double sum = 0.0;
                for (size_t aa = 0; aa < N; ++aa) {
                    double s_ni_aj = 0.0;
                    for (size_t b = 0; b < N; ++b)
                        s_ni_aj += s_be[aa][b] * N_I[b][j];
                    sum += N_V[i][aa] * s_ni_aj;
                }
                k_be[i][j] = sum;
            }
        }
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = 0; j < M; ++j) {
                double sum = 0.0;
                for (size_t aa = 0; aa < N; ++aa)
                    sum += s_be[i][aa] * N_I[aa][j];
                s_ni_be[i][j] = sum;
            }
        }
    }
}

// =============================================================================
// LU SOLVE (Equilibrated Gaussian elimination with iterative refinement)
// =============================================================================

inline bool luSolve(std::array<std::array<double, N>, N>& a_mat,
                    std::array<double, N>& b)
{
    auto a_orig = a_mat;
    auto b_orig = b;

    // Step 1: Diagonal equilibration
    std::array<double, N> d;
    d.fill(1.0);
    for (size_t i = 0; i < N; ++i) {
        double diag = std::abs(a_mat[i][i]);
        if (diag > 1e-30)
            d[i] = 1.0 / std::sqrt(diag);
    }
    for (size_t i = 0; i < N; ++i) {
        for (size_t j = 0; j < N; ++j)
            a_mat[i][j] *= d[i] * d[j];
        b[i] *= d[i];
    }

    // Step 2: LU factorize with partial pivoting
    std::array<size_t, N> perm;
    for (size_t i = 0; i < N; ++i)
        perm[i] = i;

    for (size_t col = 0; col < N; ++col) {
        size_t max_row = col;
        double max_val = std::abs(a_mat[col][col]);
        for (size_t row = col + 1; row < N; ++row) {
            if (std::abs(a_mat[row][col]) > max_val) {
                max_val = std::abs(a_mat[row][col]);
                max_row = row;
            }
        }
        if (max_val < 1e-30)
            return false;
        if (max_row != col) {
            std::swap(a_mat[col], a_mat[max_row]);
            std::swap(perm[col], perm[max_row]);
        }
        double pivot = a_mat[col][col];
        for (size_t row = col + 1; row < N; ++row) {
            double factor = a_mat[row][col] / pivot;
            a_mat[row][col] = factor;
            for (size_t j = col + 1; j < N; ++j)
                a_mat[row][j] -= factor * a_mat[col][j];
        }
    }

    // Step 3: Solve LU * x_eq = D * P * b
    std::array<double, N> x;
    for (size_t i = 0; i < N; ++i)
        x[i] = d[perm[i]] * b_orig[perm[i]];

    // Forward substitution (L)
    for (size_t i = 1; i < N; ++i) {
        double sum = 0.0;
        for (size_t j = 0; j < i; ++j)
            sum += a_mat[i][j] * x[j];
        x[i] -= sum;
    }
    // Backward substitution (U)
    for (size_t i_idx = N; i_idx > 0; --i_idx) {
        size_t i = i_idx - 1;
        double sum = 0.0;
        for (size_t j = i + 1; j < N; ++j)
            sum += a_mat[i][j] * x[j];
        if (std::abs(a_mat[i][i]) < 1e-30)
            return false;
        x[i] = (x[i] - sum) / a_mat[i][i];
    }

    // Step 4: Iterative refinement
    std::array<double, N> r;
    for (size_t i = 0; i < N; ++i) {
        size_t pi = perm[i];
        double ax_i = 0.0;
        for (size_t j = 0; j < N; ++j)
            ax_i += d[pi] * a_orig[pi][j] * d[j] * x[j];
        r[i] = d[pi] * b_orig[pi] - ax_i;
    }
    // Solve LU * dx = r
    for (size_t i = 1; i < N; ++i) {
        double sum = 0.0;
        for (size_t j = 0; j < i; ++j)
            sum += a_mat[i][j] * r[j];
        r[i] -= sum;
    }
    for (size_t i_idx = N; i_idx > 0; --i_idx) {
        size_t i = i_idx - 1;
        double sum = 0.0;
        for (size_t j = i + 1; j < N; ++j)
            sum += a_mat[i][j] * r[j];
        r[i] = (r[i] - sum) / a_mat[i][i];
    }

    // Step 5: Apply correction and undo equilibration
    for (size_t i = 0; i < N; ++i)
        b[i] = d[i] * (x[i] + r[i]);

    return true;
}

// =============================================================================
// PROCESS SAMPLE (Full-nodal NR with LU solve)
// =============================================================================

inline std::array<double, NUM_OUTPUTS> processSample(double input, CircuitState& state)
{
    if (std::isfinite(input))
        input = std::clamp(input, -100.0, 100.0);
    else
        input = 0.0;

    // Step 1: Build RHS (sparse A_neg * v_prev + sparse N_i * i_nl_prev)
    std::array<double, N> rhs = RHS_CONST;
    rhs[0] += state.a_neg[0][0] * state.v_prev[0];
    rhs[0] += state.a_neg[0][1] * state.v_prev[1];
    rhs[0] += state.a_neg[0][3] * state.v_prev[3];
    rhs[0] += state.a_neg[0][5] * state.v_prev[5];
    rhs[1] += state.a_neg[1][0] * state.v_prev[0];
    rhs[1] += state.a_neg[1][1] * state.v_prev[1];
    rhs[1] += state.a_neg[1][2] * state.v_prev[2];
    rhs[2] += state.a_neg[2][1] * state.v_prev[1];
    rhs[2] += state.a_neg[2][2] * state.v_prev[2];
    rhs[2] += state.a_neg[2][3] * state.v_prev[3];
    rhs[3] += state.a_neg[3][0] * state.v_prev[0];
    rhs[3] += state.a_neg[3][2] * state.v_prev[2];
    rhs[3] += state.a_neg[3][3] * state.v_prev[3];
    rhs[4] += state.a_neg[4][4] * state.v_prev[4];
    rhs[5] += state.a_neg[5][0] * state.v_prev[0];
    rhs[5] += state.a_neg[5][5] * state.v_prev[5];
    rhs[5] += state.a_neg[5][6] * state.v_prev[6];
    rhs[0] += N_I[0][0] * state.i_nl_prev[0];
    rhs[0] += N_I[0][2] * state.i_nl_prev[2];
    rhs[2] += N_I[2][1] * state.i_nl_prev[1];
    rhs[4] += N_I[4][0] * state.i_nl_prev[0];
    rhs[4] += N_I[4][1] * state.i_nl_prev[1];
    rhs[4] += N_I[4][3] * state.i_nl_prev[3];

    double input_conductance = 1.0 / INPUT_RESISTANCE;
    // Input source (trapezoidal: (V_in + V_in_prev) * G_in)
    rhs[INPUT_NODE] += (input + state.input_prev) * input_conductance;
    state.input_prev = input;

    // Step 2: Newton-Raphson in full augmented voltage space
    std::array<double, N> v = state.v_prev;
    bool converged = false;
    std::array<double, M> i_nl = {};

    for (size_t iter = 0; iter < MAX_ITER; ++iter) {
        // 2a. Extract nonlinear voltages: v_nl = N_v * v (sparse)
        std::array<double, M> v_nl = {};
        v_nl[0] = N_V[0][2] * v[2] + N_V[0][4] * v[4];
        v_nl[1] = N_V[1][0] * v[0] + N_V[1][2] * v[2];
        v_nl[2] = N_V[2][4] * v[4];
        v_nl[3] = N_V[3][0] * v[0] + N_V[3][4] * v[4];

        // 2b. Evaluate device currents and Jacobian (block-diagonal)
        std::array<double, M * M> j_dev = {};
        {
            // BJT 0
            double vbe = v_nl[0];
            double vbc = v_nl[1];
            auto result = bjt_evaluate(
                vbe, vbc,
                state.device_0_is, state.device_0_vt,
                DEVICE_0_NF, DEVICE_0_NR,
                state.device_0_bf, state.device_0_br,
                DEVICE_0_SIGN, DEVICE_0_USE_GP,
                DEVICE_0_VAF, DEVICE_0_VAR,
                DEVICE_0_IKF, DEVICE_0_IKR,
                DEVICE_0_ISE, DEVICE_0_NE,
                DEVICE_0_ISC, DEVICE_0_NC);
            i_nl[0] = result.ic;
            i_nl[1] = result.ib;
            j_dev[0 * M + 0] = result.jac[0];
            j_dev[0 * M + 1] = result.jac[1];
            j_dev[1 * M + 0] = result.jac[2];
            j_dev[1 * M + 1] = result.jac[3];
        }
        {
            // BJT 1
            double vbe = v_nl[2];
            double vbc = v_nl[3];
            auto result = bjt_evaluate(
                vbe, vbc,
                state.device_1_is, state.device_1_vt,
                DEVICE_1_NF, DEVICE_1_NR,
                state.device_1_bf, state.device_1_br,
                DEVICE_1_SIGN, DEVICE_1_USE_GP,
                DEVICE_1_VAF, DEVICE_1_VAR,
                DEVICE_1_IKF, DEVICE_1_IKR,
                DEVICE_1_ISE, DEVICE_1_NE,
                DEVICE_1_ISC, DEVICE_1_NC);
            i_nl[2] = result.ic;
            i_nl[3] = result.ib;
            j_dev[2 * M + 2] = result.jac[0];
            j_dev[2 * M + 3] = result.jac[1];
            j_dev[3 * M + 2] = result.jac[2];
            j_dev[3 * M + 3] = result.jac[3];
        }

        // 2c. Build Jacobian: G_aug = A - N_i * J_dev * N_v (sparse)
        auto g_aug = state.a;
        g_aug[0][2] -= N_I[0][0] * j_dev[0 * M + 0] * N_V[0][2];
        g_aug[0][4] -= N_I[0][0] * j_dev[0 * M + 0] * N_V[0][4];
        g_aug[4][2] -= N_I[4][0] * j_dev[0 * M + 0] * N_V[0][2];
        g_aug[4][4] -= N_I[4][0] * j_dev[0 * M + 0] * N_V[0][4];
        g_aug[0][0] -= N_I[0][0] * j_dev[0 * M + 1] * N_V[1][0];
        g_aug[0][2] -= N_I[0][0] * j_dev[0 * M + 1] * N_V[1][2];
        g_aug[4][0] -= N_I[4][0] * j_dev[0 * M + 1] * N_V[1][0];
        g_aug[4][2] -= N_I[4][0] * j_dev[0 * M + 1] * N_V[1][2];
        g_aug[2][2] -= N_I[2][1] * j_dev[1 * M + 0] * N_V[0][2];
        g_aug[2][4] -= N_I[2][1] * j_dev[1 * M + 0] * N_V[0][4];
        g_aug[4][2] -= N_I[4][1] * j_dev[1 * M + 0] * N_V[0][2];
        g_aug[4][4] -= N_I[4][1] * j_dev[1 * M + 0] * N_V[0][4];
        g_aug[2][0] -= N_I[2][1] * j_dev[1 * M + 1] * N_V[1][0];
        g_aug[2][2] -= N_I[2][1] * j_dev[1 * M + 1] * N_V[1][2];
        g_aug[4][0] -= N_I[4][1] * j_dev[1 * M + 1] * N_V[1][0];
        g_aug[4][2] -= N_I[4][1] * j_dev[1 * M + 1] * N_V[1][2];
        g_aug[0][4] -= N_I[0][2] * j_dev[2 * M + 2] * N_V[2][4];
        g_aug[0][0] -= N_I[0][2] * j_dev[2 * M + 3] * N_V[3][0];
        g_aug[0][4] -= N_I[0][2] * j_dev[2 * M + 3] * N_V[3][4];
        g_aug[4][4] -= N_I[4][3] * j_dev[3 * M + 2] * N_V[2][4];
        g_aug[4][0] -= N_I[4][3] * j_dev[3 * M + 3] * N_V[3][0];
        g_aug[4][4] -= N_I[4][3] * j_dev[3 * M + 3] * N_V[3][4];

        // 2d. Build companion RHS: rhs + N_i * (i_nl - J_dev * v_nl) (sparse)
        auto rhs_work = rhs;
        {
            double i_comp = i_nl[0] - (j_dev[0 * M + 0] * v_nl[0] + j_dev[0 * M + 1] * v_nl[1]);
            rhs_work[0] += N_I[0][0] * i_comp;
            rhs_work[4] += N_I[4][0] * i_comp;
        }
        {
            double i_comp = i_nl[1] - (j_dev[1 * M + 0] * v_nl[0] + j_dev[1 * M + 1] * v_nl[1]);
            rhs_work[2] += N_I[2][1] * i_comp;
            rhs_work[4] += N_I[4][1] * i_comp;
        }
        {
            double i_comp = i_nl[2] - (j_dev[2 * M + 2] * v_nl[2] + j_dev[2 * M + 3] * v_nl[3]);
            rhs_work[0] += N_I[0][2] * i_comp;
        }
        {
            double i_comp = i_nl[3] - (j_dev[3 * M + 2] * v_nl[2] + j_dev[3 * M + 3] * v_nl[3]);
            rhs_work[4] += N_I[4][3] * i_comp;
        }

        // 2e. LU solve: v_new = G_aug^{-1} * rhs_work
        auto v_new = rhs_work;
        if (!luSolve(g_aug, v_new)) {
            state.diag_nr_max_iter_count += 1;
            break;
        }

        // 2f. SPICE voltage limiting + node damping
        double alpha = 1.0;

        // Layer 1: SPICE device voltage limiting
        {
            // Device 0 dim 0
            double v_nl_proposed = 0.0;
            for (size_t j = 0; j < N; ++j)
                v_nl_proposed += N_V[0][j] * v_new[j];
            double v_nl_current = v_nl[0];
            double dv = v_nl_proposed - v_nl_current;
            if (std::abs(dv) > 1e-15) {
                double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_0_vt, DEVICE_0_VCRIT);
                double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                if (ratio < alpha) alpha = ratio;
            }
        }
        {
            // Device 0 dim 1
            double v_nl_proposed = 0.0;
            for (size_t j = 0; j < N; ++j)
                v_nl_proposed += N_V[1][j] * v_new[j];
            double v_nl_current = v_nl[1];
            double dv = v_nl_proposed - v_nl_current;
            if (std::abs(dv) > 1e-15) {
                double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_0_vt, DEVICE_0_VCRIT);
                double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                if (ratio < alpha) alpha = ratio;
            }
        }
        {
            // Device 1 dim 0
            double v_nl_proposed = 0.0;
            for (size_t j = 0; j < N; ++j)
                v_nl_proposed += N_V[2][j] * v_new[j];
            double v_nl_current = v_nl[2];
            double dv = v_nl_proposed - v_nl_current;
            if (std::abs(dv) > 1e-15) {
                double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_1_vt, DEVICE_1_VCRIT);
                double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                if (ratio < alpha) alpha = ratio;
            }
        }
        {
            // Device 1 dim 1
            double v_nl_proposed = 0.0;
            for (size_t j = 0; j < N; ++j)
                v_nl_proposed += N_V[3][j] * v_new[j];
            double v_nl_current = v_nl[3];
            double dv = v_nl_proposed - v_nl_current;
            if (std::abs(dv) > 1e-15) {
                double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_1_vt, DEVICE_1_VCRIT);
                double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                if (ratio < alpha) alpha = ratio;
            }
        }

        // Layer 2: Global node voltage damping
        {
            double max_node_dv = 0.0;
            for (size_t i = 0; i < 6; ++i) {
                double dv_val = alpha * (v_new[i] - v[i]);
                max_node_dv = std::max(max_node_dv, std::abs(dv_val));
            }
            double max_v = 0.0;
            for (size_t i = 0; i < 6; ++i)
                max_v = std::max(max_v, std::abs(v[i]));
            double damp_thresh = std::max(10.0, max_v * 0.05);
            if (max_node_dv > damp_thresh)
                alpha *= std::max(damp_thresh / max_node_dv, 0.01);
        }

        // Compute damped step, check convergence, then apply
        bool max_step_exceeded = false;
        for (size_t i = 0; i < N; ++i) {
            double step = alpha * (v_new[i] - v[i]);
            double threshold = 1e-6 * std::max(std::abs(v[i]), std::abs(v[i] + step)) + TOL;
            if (std::abs(step) >= threshold)
                max_step_exceeded = true;
            v[i] += step;
        }
        bool converged_check = !max_step_exceeded;

        if (converged_check) {
            converged = true;
            state.last_nr_iterations = static_cast<uint32_t>(iter);
            // Final device evaluation at converged point
            std::array<double, M> v_nl_final = {};
            for (size_t i = 0; i < M; ++i) {
                double sum = 0.0;
                for (size_t j = 0; j < N; ++j)
                    sum += N_V[i][j] * v[j];
                v_nl_final[i] = sum;
            }
            {
                double vbe = v_nl_final[0];
                double vbc = v_nl_final[1];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_0_is, state.device_0_vt,
                    DEVICE_0_NF, DEVICE_0_NR,
                    state.device_0_bf, state.device_0_br,
                    DEVICE_0_SIGN, DEVICE_0_USE_GP,
                    DEVICE_0_VAF, DEVICE_0_VAR,
                    DEVICE_0_IKF, DEVICE_0_IKR,
                    DEVICE_0_ISE, DEVICE_0_NE,
                    DEVICE_0_ISC, DEVICE_0_NC);
                i_nl[0] = result.ic;
                i_nl[1] = result.ib;
            }
            {
                double vbe = v_nl_final[2];
                double vbc = v_nl_final[3];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_1_is, state.device_1_vt,
                    DEVICE_1_NF, DEVICE_1_NR,
                    state.device_1_bf, state.device_1_br,
                    DEVICE_1_SIGN, DEVICE_1_USE_GP,
                    DEVICE_1_VAF, DEVICE_1_VAR,
                    DEVICE_1_IKF, DEVICE_1_IKR,
                    DEVICE_1_ISE, DEVICE_1_NE,
                    DEVICE_1_ISC, DEVICE_1_NC);
                i_nl[2] = result.ic;
                i_nl[3] = result.ib;
            }
            break;
        }
    }

    // Backward Euler fallback: if trapezoidal NR didn't converge, retry with BE
    if (!converged) {
        state.diag_nr_max_iter_count += 1;

        // Rebuild RHS with backward Euler matrices
        v = state.v_prev;
        std::array<double, N> rhs_be = {};
        for (size_t i = 0; i < N; ++i) {
            double sum = RHS_CONST_BE[i];
            for (size_t j = 0; j < N; ++j)
                sum += state.a_neg_be[i][j] * state.v_prev[j];
            for (size_t j = 0; j < M; ++j)
                sum += N_I[i][j] * state.i_nl_prev[j];
            rhs_be[i] = sum;
        }
        // BE input: just input[n+1] * G_in (no trapezoidal average)
        rhs_be[INPUT_NODE] += input * input_conductance;

        for (size_t iter = 0; iter < MAX_ITER; ++iter) {
            std::array<double, M> v_nl = {};
            for (size_t i = 0; i < M; ++i) {
                double sum = 0.0;
                for (size_t j = 0; j < N; ++j)
                    sum += N_V[i][j] * v[j];
                v_nl[i] = sum;
            }

            // Evaluate devices
            std::array<double, M * M> j_dev = {};
            {
                // BJT 0
                double vbe = v_nl[0];
                double vbc = v_nl[1];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_0_is, state.device_0_vt,
                    DEVICE_0_NF, DEVICE_0_NR,
                    state.device_0_bf, state.device_0_br,
                    DEVICE_0_SIGN, DEVICE_0_USE_GP,
                    DEVICE_0_VAF, DEVICE_0_VAR,
                    DEVICE_0_IKF, DEVICE_0_IKR,
                    DEVICE_0_ISE, DEVICE_0_NE,
                    DEVICE_0_ISC, DEVICE_0_NC);
                i_nl[0] = result.ic;
                i_nl[1] = result.ib;
                j_dev[0 * M + 0] = result.jac[0];
                j_dev[0 * M + 1] = result.jac[1];
                j_dev[1 * M + 0] = result.jac[2];
                j_dev[1 * M + 1] = result.jac[3];
            }
            {
                // BJT 1
                double vbe = v_nl[2];
                double vbc = v_nl[3];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_1_is, state.device_1_vt,
                    DEVICE_1_NF, DEVICE_1_NR,
                    state.device_1_bf, state.device_1_br,
                    DEVICE_1_SIGN, DEVICE_1_USE_GP,
                    DEVICE_1_VAF, DEVICE_1_VAR,
                    DEVICE_1_IKF, DEVICE_1_IKR,
                    DEVICE_1_ISE, DEVICE_1_NE,
                    DEVICE_1_ISC, DEVICE_1_NC);
                i_nl[2] = result.ic;
                i_nl[3] = result.ib;
                j_dev[2 * M + 2] = result.jac[0];
                j_dev[2 * M + 3] = result.jac[1];
                j_dev[3 * M + 2] = result.jac[2];
                j_dev[3 * M + 3] = result.jac[3];
            }

            auto g_aug = state.a_be;
            g_aug[0][2] -= N_I[0][0] * j_dev[0 * M + 0] * N_V[0][2];
            g_aug[0][4] -= N_I[0][0] * j_dev[0 * M + 0] * N_V[0][4];
            g_aug[4][2] -= N_I[4][0] * j_dev[0 * M + 0] * N_V[0][2];
            g_aug[4][4] -= N_I[4][0] * j_dev[0 * M + 0] * N_V[0][4];
            g_aug[0][0] -= N_I[0][0] * j_dev[0 * M + 1] * N_V[1][0];
            g_aug[0][2] -= N_I[0][0] * j_dev[0 * M + 1] * N_V[1][2];
            g_aug[4][0] -= N_I[4][0] * j_dev[0 * M + 1] * N_V[1][0];
            g_aug[4][2] -= N_I[4][0] * j_dev[0 * M + 1] * N_V[1][2];
            g_aug[2][2] -= N_I[2][1] * j_dev[1 * M + 0] * N_V[0][2];
            g_aug[2][4] -= N_I[2][1] * j_dev[1 * M + 0] * N_V[0][4];
            g_aug[4][2] -= N_I[4][1] * j_dev[1 * M + 0] * N_V[0][2];
            g_aug[4][4] -= N_I[4][1] * j_dev[1 * M + 0] * N_V[0][4];
            g_aug[2][0] -= N_I[2][1] * j_dev[1 * M + 1] * N_V[1][0];
            g_aug[2][2] -= N_I[2][1] * j_dev[1 * M + 1] * N_V[1][2];
            g_aug[4][0] -= N_I[4][1] * j_dev[1 * M + 1] * N_V[1][0];
            g_aug[4][2] -= N_I[4][1] * j_dev[1 * M + 1] * N_V[1][2];
            g_aug[0][4] -= N_I[0][2] * j_dev[2 * M + 2] * N_V[2][4];
            g_aug[0][0] -= N_I[0][2] * j_dev[2 * M + 3] * N_V[3][0];
            g_aug[0][4] -= N_I[0][2] * j_dev[2 * M + 3] * N_V[3][4];
            g_aug[4][4] -= N_I[4][3] * j_dev[3 * M + 2] * N_V[2][4];
            g_aug[4][0] -= N_I[4][3] * j_dev[3 * M + 3] * N_V[3][0];
            g_aug[4][4] -= N_I[4][3] * j_dev[3 * M + 3] * N_V[3][4];

            auto rhs_work = rhs_be;
            {
                double i_comp = i_nl[0] - (j_dev[0 * M + 0] * v_nl[0] + j_dev[0 * M + 1] * v_nl[1]);
                rhs_work[0] += N_I[0][0] * i_comp;
                rhs_work[4] += N_I[4][0] * i_comp;
            }
            {
                double i_comp = i_nl[1] - (j_dev[1 * M + 0] * v_nl[0] + j_dev[1 * M + 1] * v_nl[1]);
                rhs_work[2] += N_I[2][1] * i_comp;
                rhs_work[4] += N_I[4][1] * i_comp;
            }
            {
                double i_comp = i_nl[2] - (j_dev[2 * M + 2] * v_nl[2] + j_dev[2 * M + 3] * v_nl[3]);
                rhs_work[0] += N_I[0][2] * i_comp;
            }
            {
                double i_comp = i_nl[3] - (j_dev[3 * M + 2] * v_nl[2] + j_dev[3 * M + 3] * v_nl[3]);
                rhs_work[4] += N_I[4][3] * i_comp;
            }

            auto v_new = rhs_work;
            if (!luSolve(g_aug, v_new))
                break;

            double alpha = 1.0;
            {
                // Device 0 dim 0
                double v_nl_proposed = 0.0;
                for (size_t j = 0; j < N; ++j)
                    v_nl_proposed += N_V[0][j] * v_new[j];
                double v_nl_current = v_nl[0];
                double dv = v_nl_proposed - v_nl_current;
                if (std::abs(dv) > 1e-15) {
                    double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_0_vt, DEVICE_0_VCRIT);
                    double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                    if (ratio < alpha) alpha = ratio;
                }
            }
            {
                // Device 0 dim 1
                double v_nl_proposed = 0.0;
                for (size_t j = 0; j < N; ++j)
                    v_nl_proposed += N_V[1][j] * v_new[j];
                double v_nl_current = v_nl[1];
                double dv = v_nl_proposed - v_nl_current;
                if (std::abs(dv) > 1e-15) {
                    double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_0_vt, DEVICE_0_VCRIT);
                    double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                    if (ratio < alpha) alpha = ratio;
                }
            }
            {
                // Device 1 dim 0
                double v_nl_proposed = 0.0;
                for (size_t j = 0; j < N; ++j)
                    v_nl_proposed += N_V[2][j] * v_new[j];
                double v_nl_current = v_nl[2];
                double dv = v_nl_proposed - v_nl_current;
                if (std::abs(dv) > 1e-15) {
                    double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_1_vt, DEVICE_1_VCRIT);
                    double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                    if (ratio < alpha) alpha = ratio;
                }
            }
            {
                // Device 1 dim 1
                double v_nl_proposed = 0.0;
                for (size_t j = 0; j < N; ++j)
                    v_nl_proposed += N_V[3][j] * v_new[j];
                double v_nl_current = v_nl[3];
                double dv = v_nl_proposed - v_nl_current;
                if (std::abs(dv) > 1e-15) {
                    double v_lim = pnjlim(v_nl_proposed, v_nl_current, state.device_1_vt, DEVICE_1_VCRIT);
                    double ratio = std::clamp((v_lim - v_nl_current) / dv, 0.01, 1.0);
                    if (ratio < alpha) alpha = ratio;
                }
            }
            {
                double max_node_dv = 0.0;
                for (size_t i = 0; i < 6; ++i) {
                    double dv_val = alpha * (v_new[i] - v[i]);
                    max_node_dv = std::max(max_node_dv, std::abs(dv_val));
                }
                if (max_node_dv > 10.0)
                    alpha *= std::max(10.0 / max_node_dv, 0.01);
            }

            bool be_step_exceeded = false;
            for (size_t i = 0; i < N; ++i) {
                double step = alpha * (v_new[i] - v[i]);
                double threshold = 1e-6 * std::max(std::abs(v[i]), std::abs(v[i] + step)) + TOL;
                if (std::abs(step) >= threshold)
                    be_step_exceeded = true;
                v[i] += step;
            }
            bool be_converged = !be_step_exceeded;

            if (be_converged) {
                converged = true;
                state.diag_be_fallback_count += 1;
                std::array<double, M> v_nl_final = {};
                for (size_t i = 0; i < M; ++i) {
                    double sum = 0.0;
                    for (size_t j = 0; j < N; ++j)
                        sum += N_V[i][j] * v[j];
                    v_nl_final[i] = sum;
                }
                {
                    double vbe = v_nl_final[0];
                    double vbc = v_nl_final[1];
                    auto result = bjt_evaluate(
                        vbe, vbc,
                        state.device_0_is, state.device_0_vt,
                        DEVICE_0_NF, DEVICE_0_NR,
                        state.device_0_bf, state.device_0_br,
                        DEVICE_0_SIGN, DEVICE_0_USE_GP,
                        DEVICE_0_VAF, DEVICE_0_VAR,
                        DEVICE_0_IKF, DEVICE_0_IKR,
                        DEVICE_0_ISE, DEVICE_0_NE,
                        DEVICE_0_ISC, DEVICE_0_NC);
                    i_nl[0] = result.ic;
                    i_nl[1] = result.ib;
                }
                {
                    double vbe = v_nl_final[2];
                    double vbc = v_nl_final[3];
                    auto result = bjt_evaluate(
                        vbe, vbc,
                        state.device_1_is, state.device_1_vt,
                        DEVICE_1_NF, DEVICE_1_NR,
                        state.device_1_bf, state.device_1_br,
                        DEVICE_1_SIGN, DEVICE_1_USE_GP,
                        DEVICE_1_VAF, DEVICE_1_VAR,
                        DEVICE_1_IKF, DEVICE_1_IKR,
                        DEVICE_1_ISE, DEVICE_1_NE,
                        DEVICE_1_ISC, DEVICE_1_NC);
                    i_nl[2] = result.ic;
                    i_nl[3] = result.ib;
                }
                break;
            }
        }

        // If still not converged, ensure i_nl is consistent with v
        if (!converged) {
            std::array<double, M> v_nl_final = {};
            for (size_t i = 0; i < M; ++i) {
                double sum = 0.0;
                for (size_t j = 0; j < N; ++j)
                    sum += N_V[i][j] * v[j];
                v_nl_final[i] = sum;
            }
            {
                double vbe = v_nl_final[0];
                double vbc = v_nl_final[1];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_0_is, state.device_0_vt,
                    DEVICE_0_NF, DEVICE_0_NR,
                    state.device_0_bf, state.device_0_br,
                    DEVICE_0_SIGN, DEVICE_0_USE_GP,
                    DEVICE_0_VAF, DEVICE_0_VAR,
                    DEVICE_0_IKF, DEVICE_0_IKR,
                    DEVICE_0_ISE, DEVICE_0_NE,
                    DEVICE_0_ISC, DEVICE_0_NC);
                i_nl[0] = result.ic;
                i_nl[1] = result.ib;
            }
            {
                double vbe = v_nl_final[2];
                double vbc = v_nl_final[3];
                auto result = bjt_evaluate(
                    vbe, vbc,
                    state.device_1_is, state.device_1_vt,
                    DEVICE_1_NF, DEVICE_1_NR,
                    state.device_1_bf, state.device_1_br,
                    DEVICE_1_SIGN, DEVICE_1_USE_GP,
                    DEVICE_1_VAF, DEVICE_1_VAR,
                    DEVICE_1_IKF, DEVICE_1_IKR,
                    DEVICE_1_ISE, DEVICE_1_NE,
                    DEVICE_1_ISC, DEVICE_1_NC);
                i_nl[2] = result.ic;
                i_nl[3] = result.ib;
            }
        }
    }

    // NaN check
    {
        bool all_finite = true;
        for (size_t i = 0; i < N; ++i) {
            if (!std::isfinite(v[i])) { all_finite = false; break; }
        }
        if (!all_finite) {
            state.v_prev = state.dc_operating_point;
            state.i_nl_prev.fill(0.0);
            state.i_nl_prev_prev.fill(0.0);
            state.input_prev = 0.0;
            state.diag_nan_reset_count += 1;
            std::array<double, NUM_OUTPUTS> zero_out = {};
            return zero_out;
        }
    }

    // Step 3: Update state
    state.v_prev = v;
    state.i_nl_prev_prev = state.i_nl_prev;
    state.i_nl_prev = i_nl;

    if (state.last_nr_iterations >= static_cast<uint32_t>(MAX_ITER))
        state.diag_nr_max_iter_count += 1;

    // Step 4: Extract outputs, DC blocking, and scaling
    std::array<double, NUM_OUTPUTS> output = {};
    for (size_t out_idx = 0; out_idx < NUM_OUTPUTS; ++out_idx) {
        double raw_out = v[OUTPUT_NODES[out_idx]];
        raw_out = std::isfinite(raw_out) ? raw_out : 0.0;
        double scaled = raw_out * OUTPUT_SCALES[out_idx];
        double abs_out = std::abs(scaled);
        if (abs_out > state.diag_peak_output)
            state.diag_peak_output = abs_out;
        output[out_idx] = std::isfinite(scaled) ? scaled : 0.0;
    }
    return output;
}

} // namespace genTremolo
} // namespace openWurli
