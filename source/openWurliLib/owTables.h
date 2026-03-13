/**
 * OpenWurli DSP — Per-note parameter tables
 * Ported from Rust openwurli-dsp (GPL v3)
 *
 * Derived from Euler-Bernoulli beam theory with tip mass.
 * Range: MIDI 33 (A1) to MIDI 96 (C7) — 64 reeds.
 */
#pragma once

#include <cmath>
#include <algorithm>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

static constexpr int NUM_MODES = 7;
static constexpr uint8_t MIDI_LO = 33;
static constexpr uint8_t MIDI_HI = 96;

/// Post-speaker gain: +13 dB — calibrated with outputScale TARGET_DB=-35 dBFS
static constexpr double POST_SPEAKER_GAIN = 4.467; // 10^(13/20)

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

/// Eigenvalues for cantilever beam with tip mass
inline std::array<double, NUM_MODES> eigenvalues(double mu)
{
	struct EigRow { double mu; double betas[NUM_MODES]; };
	static constexpr EigRow table[] = {
		{0.00, {1.8751, 4.6941, 7.8548, 10.9955, 14.1372, 17.2788, 20.4204}},
		{0.01, {1.8584, 4.6849, 7.8504, 10.9930, 14.1356, 17.2776, 20.4195}},
		{0.05, {1.7918, 4.6480, 7.8326, 10.9827, 14.1289, 17.2735, 20.4166}},
		{0.10, {1.7227, 4.6063, 7.8102, 10.9696, 14.1201, 17.2669, 20.4116}},
		{0.25, {1.5574, 4.4924, 7.7467, 10.9305, 14.0945, 17.2475, 20.3969}},
		{0.50, {1.4206, 4.3604, 7.6647, 10.8760, 14.0573, 17.2191, 20.3741}},
		{1.00, {1.2479, 4.0311, 7.1341, 10.2566, 13.3878, 16.5222, 19.6583}},
	};
	constexpr int N = 7;

	if (mu <= table[0].mu)
	{
		std::array<double, NUM_MODES> out;
		for (int i = 0; i < NUM_MODES; i++) out[i] = table[0].betas[i];
		return out;
	}
	if (mu >= table[N-1].mu)
	{
		std::array<double, NUM_MODES> out;
		for (int i = 0; i < NUM_MODES; i++) out[i] = table[N-1].betas[i];
		return out;
	}

	for (int r = 0; r < N - 1; r++)
	{
		if (mu <= table[r+1].mu)
		{
			const double t = (mu - table[r].mu) / (table[r+1].mu - table[r].mu);
			std::array<double, NUM_MODES> out;
			for (int i = 0; i < NUM_MODES; i++)
				out[i] = table[r].betas[i] + t * (table[r+1].betas[i] - table[r].betas[i]);
			return out;
		}
	}

	std::array<double, NUM_MODES> out;
	for (int i = 0; i < NUM_MODES; i++) out[i] = table[N-1].betas[i];
	return out;
}

/// Mode frequency ratios f_n/f_1
inline std::array<double, NUM_MODES> modeRatios(uint8_t midi)
{
	const double mu = tipMassRatio(midi);
	const auto betas = eigenvalues(mu);
	std::array<double, NUM_MODES> ratios;
	const double beta1sq = betas[0] * betas[0];
	for (int i = 0; i < NUM_MODES; i++)
		ratios[i] = (betas[i] * betas[i]) / beta1sq;
	return ratios;
}

/// Mode decay rates (dB/s) — register dependent
inline std::array<double, NUM_MODES> modeDecayRates(uint8_t midi)
{
	const double freq = midiToFreq(midi);
	const auto ratios = modeRatios(midi);
	std::array<double, NUM_MODES> rates;

	// Base: 3 dB/s at A1 fundamental, scaling with sqrt(freq)
	const double baseRate = 3.0 * std::sqrt(freq / 55.0);

	for (int i = 0; i < NUM_MODES; i++)
	{
		// Higher modes decay faster: rate ∝ mode_ratio^0.5
		rates[i] = baseRate * std::sqrt(ratios[i]);
	}
	return rates;
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

/// Reed blank width and thickness in mm (200A series)
inline void reedBlankDims(uint8_t midi, double& widthMm, double& thicknessMm)
{
	const int reed = std::clamp(static_cast<int>(midi) - 32, 1, 64);

	double widthInch;
	if (reed <= 14)       widthInch = 0.151;
	else if (reed <= 20)  widthInch = 0.127;
	else if (reed <= 42)  widthInch = 0.121;
	else if (reed <= 50)  widthInch = 0.111;
	else                  widthInch = 0.098;

	double thicknessInch;
	if (reed <= 16)       thicknessInch = 0.026;
	else if (reed <= 26)  thicknessInch = 0.026 + (static_cast<double>(reed) - 16.0) / 10.0 * (0.034 - 0.026);
	else                  thicknessInch = 0.034;

	widthMm = widthInch * 25.4;
	thicknessMm = thicknessInch * 25.4;
}

/// Beam tip compliance: L³ / (w × t³)
inline double reedCompliance(uint8_t midi)
{
	const double l = reedLengthMm(midi);
	double w, t;
	reedBlankDims(midi, w, t);
	return (l * l * l) / (w * t * t * t);
}

/// Pickup displacement scale from beam compliance (controls bark)
static constexpr double DS_AT_C4 = 0.75;
static constexpr double DS_EXPONENT = 0.75;
static constexpr double DS_CLAMP_LO = 0.02;
static constexpr double DS_CLAMP_HI = 0.82;

inline double pickupDisplacementScale(uint8_t midi)
{
	const double c = reedCompliance(midi);
	const double cRef = reedCompliance(60);
	const double ds = DS_AT_C4 * std::pow(c / cRef, DS_EXPONENT);
	return std::clamp(ds, DS_CLAMP_LO, DS_CLAMP_HI);
}

/// Velocity S-curve — neoprene foam pad compression (k=1.5)
inline double velocityScurve(double velocity)
{
	constexpr double k = 1.5;
	const double s  = 1.0 / (1.0 + std::exp(-k * (velocity - 0.5)));
	const double s0 = 1.0 / (1.0 + std::exp( k * 0.5));
	const double s1 = 1.0 / (1.0 + std::exp(-k * 0.5));
	return (s - s0) / (s1 - s0);
}

/// Register-dependent velocity exponent (bell curve)
inline double velocityExponent(uint8_t midi)
{
	const double m = static_cast<double>(midi);
	constexpr double center = 62.0;
	constexpr double sigma = 15.0;
	constexpr double minExp = 1.3;
	constexpr double maxExp = 1.7;
	const double t = std::exp(-0.5 * std::pow((m - center) / sigma, 2.0));
	return minExp + t * (maxExp - minExp);
}

/// Multi-harmonic RMS proxy for post-pickup signal (8 harmonics through HPF)
inline double pickupRmsProxy(double ds, double f0, double fc)
{
	if (ds < 1e-10) return 0.0;
	const double r = (1.0 - std::sqrt(1.0 - ds * ds)) / ds;
	const double invSqrt = 1.0 / std::sqrt(1.0 - ds * ds);
	double sumSq = 0.0;
	double rN = r;
	for (int n = 1; n <= 8; n++)
	{
		const double cn = 2.0 * rN * invSqrt;
		const double nf = static_cast<double>(n) * f0;
		const double hpfN = nf / std::sqrt(nf * nf + fc * fc);
		sumSq += (cn * hpfN) * (cn * hpfN);
		rN *= r;
	}
	return std::sqrt(sumSq);
}

/// Register trim dB — empirical correction from Tier 3 calibration at v=127
inline double registerTrimDb(uint8_t midi)
{
	struct Anchor { double x, y; };
	static constexpr Anchor anchors[] = {
		{36.0, -1.3}, {40.0,  0.0}, {44.0, -1.3}, {48.0,  0.7},
		{52.0,  0.2}, {56.0, -1.1}, {60.0,  0.0}, {64.0,  0.9},
		{68.0,  1.2}, {72.0,  0.0}, {76.0,  1.8}, {80.0,  2.4},
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

/// Post-pickup output scale — velocity-aware multi-harmonic proxy + voicing + register trim
inline double outputScale(uint8_t midi, double velocity)
{
	constexpr double HPF_FC = 2312.0;
	constexpr double TARGET_DB = -35.0;
	constexpr double VOICING_SLOPE = -0.04;

	const double ds = pickupDisplacementScale(midi);
	const double f0 = midiToFreq(midi);

	// Velocity-aware proxy: compute at the actual displacement the pickup sees
	const double scurveV = velocityScurve(velocity);
	const double velScale = std::pow(scurveV, velocityExponent(midi));
	const double velScaleC4 = std::pow(scurveV, velocityExponent(60));
	const double effectiveDs = std::max(ds * velScale, 1e-6);
	const double effectiveDsRef = std::max(DS_AT_C4 * velScaleC4, 1e-6);

	const double rms = pickupRmsProxy(effectiveDs, f0, HPF_FC);
	const double rmsRef = pickupRmsProxy(effectiveDsRef, midiToFreq(60), HPF_FC);

	const double flatDb = -20.0 * std::log10(rms / rmsRef);
	const double voicingDb = VOICING_SLOPE * std::max(static_cast<double>(midi) - 60.0, 0.0);
	const double trim = registerTrimDb(midi);

	// Velocity-dependent trim blend (exponent 1.3)
	const double velBlend = std::pow(velocity, 1.3);
	const double effectiveTrim = trim * velBlend;

	return std::pow(10.0, (TARGET_DB + flatDb + voicingDb + effectiveTrim) / 20.0);
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
	NoteParams p;
	p.fundamentalHz = midiToFreq(std::clamp(midi, MIDI_LO, MIDI_HI));
	p.modeRatiosArr = modeRatios(midi);
	p.modeDecayRatesArr = modeDecayRates(midi);
	for (int i = 0; i < NUM_MODES; i++)
		p.modeAmplitudes[i] = BASE_MODE_AMPLITUDES[i];
	return p;
}

} // namespace openWurli
