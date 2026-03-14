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

/// Fundamental decay rate in dB/s — frequency power law calibrated against OBM recordings
inline double fundamentalDecayRate(uint8_t midi)
{
	const double f = midiToFreq(midi);
	return std::max(0.005 * std::pow(f, 1.22), 3.0); // floor at 3.0 dB/s
}

/// Mode decay rates (dB/s) — super-linear damping (ratio², p=2.0)
/// Real steel reed damping: thermoelastic (Zener), air radiation, and clamping
/// losses scale faster than linearly with frequency. At p=2.0, mode 2 at C4
/// decays at ~326 dB/s, reaching -10 dB within 9ms — confining metallic
/// inharmonic partials to the first 2-3 cycles of the attack.
inline std::array<double, NUM_MODES> modeDecayRates(uint8_t midi)
{
	const auto ratios = modeRatios(midi);
	const double base = fundamentalDecayRate(midi);
	std::array<double, NUM_MODES> rates;
	for (int i = 0; i < NUM_MODES; i++)
		rates[i] = base * ratios[i] * ratios[i]; // ratio² (MODE_DECAY_EXPONENT = 2.0)
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

/// Cantilever beam mode shape phi_n(xi) with tip mass
/// phi_n(xi) = cosh(beta*xi) - cos(beta*xi) - sigma*(sinh(beta*xi) - sin(beta*xi))
inline double modeShape(double beta, double xi)
{
	const double sigma = (std::cosh(beta) + std::cos(beta)) / (std::sinh(beta) + std::sin(beta));
	const double bx = beta * xi;
	return std::cosh(bx) - std::cos(bx) - sigma * (std::sinh(bx) - std::sin(bx));
}

/// Active pickup plate length in mm
static constexpr double PLATE_ACTIVE_LENGTH_MM = 6.0;

/// Spatial coupling coefficients for pickup-reed interaction.
/// The pickup plate integrates reed displacement over its active region near the tip.
/// Higher bending modes' lobes partially cancel within the pickup window, producing
/// a spatial low-pass filter. Normalized to mode 1 (returns kappa_n / kappa_1).
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
	const uint8_t m = std::clamp(midi, MIDI_LO, MIDI_HI);
	NoteParams p;
	p.fundamentalHz = midiToFreq(m);
	p.modeRatiosArr = modeRatios(m);
	p.modeDecayRatesArr = modeDecayRates(m);

	// Start with base amplitudes
	for (int i = 0; i < NUM_MODES; i++)
		p.modeAmplitudes[i] = BASE_MODE_AMPLITUDES[i];

	// Apply spatial pickup coupling — finite plate length attenuates higher bending modes
	const double mu = tipMassRatio(m);
	const auto coupling = spatialCouplingCoefficients(mu, reedLengthMm(m));
	for (int i = 0; i < NUM_MODES; i++)
		p.modeAmplitudes[i] *= coupling[i];

	return p;
}

} // namespace openWurli
