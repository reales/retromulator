/**
 * OpenWurli DSP — Per-note parameter tables
 * Ported from Rust openwurli-dsp 0.9.0 tables.rs (GPL v3)
 *
 * Derived from Euler-Bernoulli beam theory with tip mass.
 * Range: MIDI 33 (A1) to MIDI 96 (C7), 64 reeds.
 */
#pragma once

#include "owPickup.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

static constexpr int NUM_MODES = 7;
static constexpr uint8_t MIDI_LO = 33;
static constexpr uint8_t MIDI_HI = 96;

/// The amp's clip ceiling in volts; ±1.0 at the amp output is ±22 V.
static constexpr double POWER_AMP_HEADROOM_V = 22.0;
/// Volts at the power-amp output that map to digital full scale.
static constexpr double FULL_SCALE_VOLTS = POWER_AMP_HEADROOM_V;
static constexpr double POST_SPEAKER_GAIN = POWER_AMP_HEADROOM_V / FULL_SCALE_VOLTS;
/// Plugin-side output alignment after the whole chain (+14 dB), a level change only.
static constexpr double OUTPUT_ALIGNMENT_DB = 14.0;

/// Base mode amplitudes calibrated against OBM recordings
static constexpr double BASE_MODE_AMPLITUDES[NUM_MODES] =
	{1.0, 0.005, 0.0035, 0.0018, 0.0011, 0.0007, 0.0005};

inline double midiToFreq(uint8_t midi)
{
	return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

/// Estimated tip mass ratio mu for a given MIDI note (linear interpolation)
inline double tipMassRatio(uint8_t midi)
{
	const double m = static_cast<double>(midi);
	struct Anchor { double x, y; };
	static constexpr Anchor anchors[] = {
		{33.0, 0.10}, {52.0, 0.00}, {62.0, 0.00}, {74.0, 0.02}, {96.0, 0.01}
	};
	constexpr int N = 5;

	if (m <= anchors[0].x) return anchors[0].y;
	if (m >= anchors[N-1].x) return anchors[N-1].y;

	for (int i = 0; i < N - 1; i++)
	{
		if (m <= anchors[i+1].x)
		{
			const double t = (m - anchors[i].x) / (anchors[i+1].x - anchors[i].x);
			return anchors[i].y + t * (anchors[i+1].y - anchors[i].y);
		}
	}
	return 0.0;
}

/// Eigenvalues for cantilever beam with tip mass ratio mu (clamped to 0..0.5)
inline std::array<double, NUM_MODES> eigenvalues(double mu)
{
	struct EigRow { double mu; double betas[NUM_MODES]; };
	static constexpr EigRow table[] = {
		{0.00, {1.8751, 4.6941, 7.8548, 10.9955, 14.1372, 17.2788, 20.4204}},
		{0.01, {1.8584, 4.6849, 7.8504, 10.9930, 14.1356, 17.2776, 20.4195}},
		{0.05, {1.7920, 4.6477, 7.8316, 10.9830, 14.1288, 17.2726, 20.4158}},
		{0.10, {1.7227, 4.6024, 7.8077, 10.9700, 14.1198, 17.2660, 20.4110}},
		{0.15, {1.6625, 4.5618, 7.7859, 10.9580, 14.1114, 17.2598, 20.4065}},
		{0.20, {1.6097, 4.5254, 7.7659, 10.9470, 14.1036, 17.2540, 20.4023}},
		{0.30, {1.5201, 4.4620, 7.7310, 10.9280, 14.0894, 17.2434, 20.3946}},
		{0.50, {1.3853, 4.3601, 7.6745, 10.8970, 14.0650, 17.2252, 20.3814}},
	};
	constexpr int N = 8;

	const double muC = std::clamp(mu, 0.0, 0.50);
	int lo = 0;
	for (int r = 0; r < N; r++)
		if (table[r].mu <= muC) lo = r;
	const int hi = std::min(lo + 1, N - 1);
	const double t = (table[hi].mu > table[lo].mu)
		? (muC - table[lo].mu) / (table[hi].mu - table[lo].mu) : 0.0;

	std::array<double, NUM_MODES> out;
	for (int i = 0; i < NUM_MODES; i++)
		out[i] = table[lo].betas[i] + t * (table[hi].betas[i] - table[lo].betas[i]);
	return out;
}

/// Mode frequency ratios f_n/f_1 = (beta_n/beta_1)^2
inline std::array<double, NUM_MODES> modeRatios(double mu)
{
	const auto betas = eigenvalues(mu);
	std::array<double, NUM_MODES> ratios;
	const double beta1sq = betas[0] * betas[0];
	for (int i = 0; i < NUM_MODES; i++)
		ratios[i] = (betas[i] * betas[i]) / beta1sq;
	return ratios;
}

/// Reed length in mm (200A series)
inline double reedLengthMm(uint8_t midi)
{
	const double n = std::clamp(static_cast<double>(midi) - 32.0, 1.0, 64.0);
	const double inches = (n <= 20.0)
		? 3.0 - n / 20.0
		: 2.0 - (n - 20.0) / 44.0;
	return inches * 25.4;
}

// ── Provisional per-note calibration (swing at the pickup, air drag) ────────

struct NoteKnot { uint8_t midi; double value; };

/// PROVISIONAL ff reed swing at the pickup, mm (central pick of the hammer-impact prior).
static constexpr NoteKnot PROVISIONAL_FF_SWING_MM[24] = {
	{33, 2.46}, {36, 2.44}, {39, 2.40}, {42, 2.33}, {45, 2.23}, {48, 2.25}, {51, 2.05}, {52, 1.98},
	{53, 1.16}, {54, 1.16}, {57, 1.14}, {60, 1.11}, {63, 1.06}, {66, 0.99}, {69, 0.90}, {72, 0.77},
	{75, 0.67}, {78, 0.57}, {81, 0.48}, {84, 0.42}, {87, 0.35}, {90, 0.30}, {93, 0.25}, {96, 0.21},
};

/// PROVISIONAL quadratic air drag on mode 1: extra decay in dB/s per mm of swing.
static constexpr NoteKnot PROVISIONAL_AIR_DRAG_DB_PER_MM[24] = {
	{33, 0.259}, {36, 0.356}, {39, 0.485}, {42, 0.651}, {45, 0.863}, {48, 1.125}, {51, 1.417}, {52, 1.519},
	{53, 0.664}, {54, 0.756}, {57, 1.114}, {60, 1.633}, {63, 2.381}, {66, 3.446}, {69, 4.863}, {72, 5.921},
	{75, 7.116}, {78, 8.562}, {81, 10.32}, {84, 12.45}, {87, 15.05}, {90, 18.24}, {93, 22.16}, {96, 27.02},
};

static constexpr uint8_t BASS_TOP_MIDI = 52;
static constexpr double SWING_MIN_MM = 0.02;

/// Linear-in-log interpolation within one side of the reed-20|21 break; clamps at the side ends.
inline double logInterpBySide(uint8_t midi, const NoteKnot* knots, int count)
{
	const bool bass = midi <= BASS_TOP_MIDI;
	const NoteKnot* prev = nullptr;
	for (int i = 0; i < count; i++)
	{
		const NoteKnot& k = knots[i];
		if ((k.midi <= BASS_TOP_MIDI) != bass)
			continue;
		if (k.midi >= midi)
		{
			if (prev && k.midi > midi)
			{
				const double t = static_cast<double>(midi - prev->midi) / static_cast<double>(k.midi - prev->midi);
				return std::exp(std::log(prev->value) * (1.0 - t) + std::log(k.value) * t);
			}
			return k.value;
		}
		prev = &k;
	}
	return prev ? prev->value : knots[0].value;
}

/// ff reed swing at the pickup for this note, mm.
inline double pickupSwingMm(uint8_t midi)
{
	return std::max(logInterpBySide(midi, PROVISIONAL_FF_SWING_MM, 24), SWING_MIN_MM);
}

/// Mode-1 air-drag coefficient, dB/s per mm of finger-region swing.
inline double airDragDbPerMm(uint8_t midi)
{
	return logInterpBySide(midi, PROVISIONAL_AIR_DRAG_DB_PER_MM, 24);
}

/// Cantilever beam mode shape phi_n(xi) with tip mass
inline double modeShape(double beta, double xi)
{
	const double sigma = (std::cosh(beta) + std::cos(beta)) / (std::sinh(beta) + std::sin(beta));
	const double bx = beta * xi;
	return std::cosh(bx) - std::cos(bx) - sigma * (std::sinh(bx) - std::sin(bx));
}

static constexpr double PLATE_ACTIVE_LENGTH_MM = 6.0;

/// Spatial coupling coefficients (kappa_n / kappa_1): the pickup integrates reed
/// displacement over its active region, attenuating higher bending modes.
inline std::array<double, NUM_MODES> spatialCouplingCoefficients(double mu, double reedLenMm)
{
	const auto betas = eigenvalues(mu);
	const double ellOverL = std::clamp(PLATE_ACTIVE_LENGTH_MM / reedLenMm, 0.0, 1.0);

	std::array<double, NUM_MODES> kappaRaw;

	constexpr int N_SIMPSON = 32;
	const double xiStart = 1.0 - ellOverL;

	for (int mode = 0; mode < NUM_MODES; mode++)
	{
		const double beta = betas[mode];
		const double tipVal = modeShape(beta, 1.0);

		if (std::abs(tipVal) < 1e-30 || ellOverL < 1e-12)
		{
			kappaRaw[mode] = 1.0;
			continue;
		}

		const double h = ellOverL / static_cast<double>(N_SIMPSON);
		double sum = modeShape(beta, xiStart) + modeShape(beta, 1.0);

		for (int j = 1; j < N_SIMPSON; j++)
		{
			const double xi = xiStart + static_cast<double>(j) * h;
			const double coeff = (j % 2 == 1) ? 4.0 : 2.0;
			sum += coeff * modeShape(beta, xi);
		}

		const double integral = sum * h / 3.0;
		const double k = std::abs(integral / (ellOverL * tipVal));
		kappaRaw[mode] = std::clamp(k, 0.0, 1.0);
	}

	const double k1 = kappaRaw[0];
	if (k1 > 1e-30)
	{
		for (auto& k : kappaRaw)
			k = std::clamp(k / k1, 0.0, 1.0);
	}
	else
	{
		kappaRaw.fill(1.0);
	}
	return kappaRaw;
}

/// Fundamental decay rate in dB/s: 0.005 * f^1.22, floored at 3.0 dB/s
inline double fundamentalDecayRate(uint8_t midi)
{
	const double f = midiToFreq(midi);
	return std::max(0.005 * std::pow(f, 1.22), 3.0);
}

/// Mode decay rates (dB/s), super-linear damping (ratio^2)
inline std::array<double, NUM_MODES> modeDecayRates(uint8_t midi, const std::array<double, NUM_MODES>& ratios)
{
	const double base = fundamentalDecayRate(midi);
	std::array<double, NUM_MODES> rates;
	for (int i = 0; i < NUM_MODES; i++)
		rates[i] = base * ratios[i] * ratios[i];
	return rates;
}

/// Multi-harmonic RMS proxy for a static pickup source law: H1..H8 of the source term,
/// each through the pickup's one-pole high-pass, from a 64-point DFT over one cycle.
template<typename Source>
inline double pickupRmsProxyNumeric(Source source, double f0, double fc)
{
	constexpr int N = 64;
	double s[N];
	for (int i = 0; i < N; i++)
		s[i] = source(std::sin(2.0 * M_PI * static_cast<double>(i) / N));
	double sumSq = 0.0;
	for (int n = 1; n <= 8; n++)
	{
		double re = 0.0, im = 0.0;
		for (int i = 0; i < N; i++)
		{
			const double p = 2.0 * M_PI * static_cast<double>(n * i) / N;
			re += s[i] * std::cos(p);
			im -= s[i] * std::sin(p);
		}
		const double cn = 2.0 * std::sqrt(re * re + im * im) / N;
		const double nf = static_cast<double>(n) * f0;
		const double hpfN = nf / std::sqrt(nf * nf + fc * fc);
		sumSq += (cn * hpfN) * (cn * hpfN);
	}
	return std::sqrt(sumSq);
}

/// RMS proxy for a note's actual pickup: its section's law, band side and swing (mm).
inline double pickupRmsProxyLaw(uint8_t midi, double swingMm, double f0, double fc)
{
	const PickupLawTable& law = pickupLawFor(midi);
	const double a = pickupBandSign(midi) * swingMm;
	const double rest = pickupLawEval(law, -PICKUP_REST_OFFSET_MM);
	return pickupRmsProxyNumeric(
		[&](double st) { return pickupLawEval(law, a * st - PICKUP_REST_OFFSET_MM) - rest; },
		f0, fc);
}

/// Register trim dB, re-calibrated 2026-09-23 on the shipping chain
inline double registerTrimDb(uint8_t midi)
{
	struct Anchor { double x, y; };
	static constexpr Anchor anchors[] = {
		{36.0, -2.6}, {40.0, -0.2}, {44.0, -1.6}, {48.0,  1.3},
		{52.0,  0.5}, {56.0, -2.0}, {60.0,  0.0}, {64.0,  0.7},
		{68.0,  0.8}, {72.0, -0.6}, {76.0,  1.4}, {80.0,  2.1},
		{84.0,  3.6}
	};
	constexpr int N = 13;
	const double m = static_cast<double>(midi);

	if (m <= anchors[0].x) return anchors[0].y;
	if (m >= anchors[N-1].x) return anchors[N-1].y;

	for (int i = 0; i < N - 1; i++)
	{
		if (m <= anchors[i+1].x)
		{
			const double t = (m - anchors[i].x) / (anchors[i+1].x - anchors[i].x);
			return anchors[i].y + t * (anchors[i+1].y - anchors[i].y);
		}
	}
	return 0.0;
}

/// Velocity S-curve, neoprene foam pad compression (k=1.5)
inline double velocityScurve(double velocity)
{
	constexpr double k = 1.5;
	const double s  = 1.0 / (1.0 + std::exp(-k * (velocity - 0.5)));
	const double s0 = 1.0 / (1.0 + std::exp( k * 0.5));
	const double s1 = 1.0 / (1.0 + std::exp(-k * 0.5));
	return (s - s0) / (s1 - s0);
}

/// Register-dependent velocity exponent: bell centred at D4, bass edge compressed to 0.55
inline double velocityExponent(uint8_t midi)
{
	const double m = static_cast<double>(midi);
	constexpr double center = 62.0;
	constexpr double sigma = 15.0;
	constexpr double maxExp = 1.7;
	constexpr double trebleMin = 1.3;
	constexpr double bassMin = 0.55;
	const double t = std::exp(-0.5 * std::pow((m - center) / sigma, 2.0));
	const double minExp = (m < center) ? bassMin : trebleMin;
	return minExp + t * (maxExp - minExp);
}

/// Post-pickup output scale: velocity-aware proxy through the note's own law + voicing + trim
inline double outputScale(uint8_t midi, double velocity)
{
	constexpr double TARGET_DB = -29.55;
	constexpr double VOICING_SLOPE = -0.04;
	const double hpfFc = PICKUP_FC;

	const double swing = pickupSwingMm(midi);
	const double f0 = midiToFreq(midi);

	const double scurveV = velocityScurve(velocity);
	const double velScale = std::pow(scurveV, velocityExponent(midi));
	const double velScaleC4 = std::pow(scurveV, velocityExponent(60));
	const double effectiveSwing = std::max(swing * velScale, 1e-6);
	const double effectiveSwingRef = std::max(pickupSwingMm(60) * velScaleC4, 1e-6);

	const double rms = pickupRmsProxyLaw(midi, effectiveSwing, f0, hpfFc);
	const double rmsRef = pickupRmsProxyLaw(60, effectiveSwingRef, midiToFreq(60), hpfFc);

	const double flatDb = -20.0 * std::log10(rms / rmsRef);
	const double voicingDb = VOICING_SLOPE * std::max(static_cast<double>(midi) - 60.0, 0.0);
	const double trim = registerTrimDb(midi);

	const double velBlend = std::pow(velocity, 1.3);
	const double effectiveTrim = trim * velBlend;

	return std::pow(10.0, (TARGET_DB + flatDb + voicingDb + effectiveTrim) / 20.0);
}

// ── Volume network: the drawn pot between preamp and power amp ─────────────

/// R-11 "REED BAR VOLUME" 25K trimmer, held at its inferred factory default.
static constexpr double R11_REED_BAR_VOLUME_DEFAULT = 17200.0;
static constexpr double R11_REED_BAR_VOLUME_MAX = 25000.0;
static constexpr double VOLUME_POT_R = 10000.0;
static constexpr double PREAMP_OUTPUT_R9 = 6800.0;
static constexpr double POWER_AMP_R_IN = 15000.0;
static constexpr double POWER_AMP_C9 = 1.0e-9;

/// Audio-taper law: wiper-to-ground fraction at rotation pos (15 % at centre, two-slope)
inline double audioTaper(double pos)
{
	const double p = std::clamp(pos, 0.0, 1.0);
	return (p <= 0.5) ? 0.15 * (p / 0.5) : 0.15 + 0.85 * ((p - 0.5) / 0.5);
}

/// Gain from the preamp's open-circuit output (R-9 terminal) to the power-amp input.
inline double volumePotGain(double vol, double r11)
{
	r11 = std::clamp(r11, 0.0, R11_REED_BAR_VOLUME_MAX);
	const double rLower = VOLUME_POT_R * audioTaper(vol);
	const double rUpper = VOLUME_POT_R - rLower;
	const double rLowEff = (rLower <= 0.0) ? 0.0 : rLower * POWER_AMP_R_IN / (rLower + POWER_AMP_R_IN);
	return rLowEff / (PREAMP_OUTPUT_R9 + r11 + rUpper + rLowEff);
}

/// Corner of the C-9 pole at the amp input for pot position vol.
inline double volumePotPoleHz(double vol, double r11)
{
	r11 = std::clamp(r11, 0.0, R11_REED_BAR_VOLUME_MAX);
	const double rLower = VOLUME_POT_R * audioTaper(vol);
	const double rUpper = VOLUME_POT_R - rLower;
	const double rSrc = PREAMP_OUTPUT_R9 + r11 + rUpper;
	auto par = [](double a, double b) { return a * b / (a + b); };
	const double rTh = (rLower <= 0.0) ? 0.0 : par(par(rSrc, rLower), POWER_AMP_R_IN);
	if (rTh <= 0.0)
		return INFINITY;
	return 1.0 / (2.0 * M_PI * rTh * POWER_AMP_C9);
}

/// Aggregate note parameters
struct NoteParams
{
	double fundamentalHz;
	std::array<double, NUM_MODES> modeRatiosArr;
	std::array<double, NUM_MODES> modeAmplitudes;
	std::array<double, NUM_MODES> modeDecayRatesArr;
};

inline NoteParams noteParams(uint8_t midi)
{
	const uint8_t m = std::clamp(midi, MIDI_LO, MIDI_HI);
	const double mu = tipMassRatio(m);
	NoteParams p;
	p.fundamentalHz = midiToFreq(m);
	p.modeRatiosArr = modeRatios(mu);
	p.modeDecayRatesArr = modeDecayRates(m, p.modeRatiosArr);

	for (int i = 0; i < NUM_MODES; i++)
		p.modeAmplitudes[i] = BASE_MODE_AMPLITUDES[i];

	const auto coupling = spatialCouplingCoefficients(mu, reedLengthMm(m));
	for (int i = 0; i < NUM_MODES; i++)
		p.modeAmplitudes[i] *= coupling[i];

	return p;
}

} // namespace openWurli
