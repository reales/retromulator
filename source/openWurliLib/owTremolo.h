/**
 * OpenWurli DSP — Tremolo: Twin-T circuit oscillator + LED light law + CdS LDR + depth divider
 * Ported from Rust openwurli-dsp 0.9.0 tremolo.rs (GPL v3)
 *
 * The LED current comes from the oscillator deck's anode tap through R-18; the
 * 50 kΩ VIBRATO pot is a 3-terminal divider in the fb_junction→LDR shunt leg.
 */
#pragma once

#include "owGenTremolo.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace openWurli
{

class Tremolo
{
public:
	Tremolo() = default;

	void init(double depth, double sampleRate)
	{
		m_sampleRate = sampleRate;
		m_depth = std::clamp(depth, 0.0, 1.0);
		m_rLdr = R_LDR_MAX;
		m_ldrEnvelope = 0.0;
		m_ldrAttack = std::exp(-1.0 / (ATTACK_TAU * sampleRate));
		m_ldrRelease = std::exp(-1.0 / (RELEASE_TAU * sampleRate));
		m_lnRMax = std::log(R_LDR_MAX);
		m_lnMinMinusMax = std::log(R_LDR_MIN) - m_lnRMax;

		for (int k = 0; k < LED_LUT_N; ++k)
		{
			const double iMa = LED_I_FULL_MA * static_cast<double>(k) / static_cast<double>(LED_LUT_N - 1);
			m_ledLut[k] = std::clamp(ledIntensity(iMa), 0.0, 1.0);
		}

		settleOscillator();
	}

	void setDepth(double depth)
	{
		m_depth = std::clamp(depth, 0.0, 1.0);
	}

	/// Returns the shunt impedance seen by fb_junction (ohms) for this sample.
	double process()
	{
		const double ledDrive = oscillatorDrive();

		const double coeff = (ledDrive > m_ldrEnvelope) ? m_ldrAttack : m_ldrRelease;
		m_ldrEnvelope = ledDrive + coeff * (m_ldrEnvelope - ledDrive);

		const double drive = std::clamp(m_ldrEnvelope, 0.0, 1.0);
		if (drive < 1e-6)
			m_rLdr = R_LDR_MAX;
		else
			m_rLdr = std::exp(m_lnRMax + m_lnMinMinusMax * std::pow(drive, GAMMA));

		return shuntImpedance();
	}

	double currentResistance() const { return shuntImpedance(); }

	void reset()
	{
		settleOscillator();
		m_ldrEnvelope = 0.0;
		m_rLdr = R_LDR_MAX;
	}

private:
	static constexpr double ATTACK_TAU = 0.0025;
	static constexpr double RELEASE_TAU = 0.035;
	static constexpr double GAMMA = 0.9;
	static constexpr double R_LDR_MIN = 9000.0;
	static constexpr double R_LDR_MAX = 1000000.0;
	static constexpr double R_VIB_BRIDGE = 18000.0;
	static constexpr double R_VIB_POT = 50000.0;
	static constexpr double V_RAIL = 14.5;
	static constexpr double R18_LED_SERIES = 680.0;
	static constexpr double LED_I_FULL_MA = 2.279;
	static constexpr int LED_LUT_N = 256;

	// TIL209A relative intensity vs forward current: (mA, local log-log exponent up to the next)
	static constexpr double LED_LAW_I[5] = { 0.10, 1.00, 3.00, 10.00, 40.00 };
	static constexpr double LED_LAW_N[5] = { 1.36, 1.22, 1.02, 0.88, 0.88 };

	static double ledLnAt(double i)
	{
		double acc = 0.0;
		double prevI = LED_LAW_I[0];
		double prevN = LED_LAW_N[0];
		if (i <= prevI)
			return prevN * std::log(i / prevI);
		for (int k = 1; k < 5; ++k)
		{
			const double hi = std::min(i, LED_LAW_I[k]);
			acc += prevN * std::log(hi / prevI);
			if (i <= LED_LAW_I[k])
				return acc;
			prevI = LED_LAW_I[k];
			prevN = LED_LAW_N[k];
		}
		return acc + prevN * std::log(i / prevI);
	}

	static double ledIntensity(double iMa)
	{
		if (iMa <= 0.0)
			return 0.0;
		return std::exp(ledLnAt(iMa) - ledLnAt(LED_I_FULL_MA));
	}

	// Rebuild the Twin-T from its default (which carries the startup perturbation)
	// and settle it to steady amplitude. CircuitState::reset() would park it at the
	// DC point, an unstable equilibrium where it never starts oscillating.
	void settleOscillator()
	{
		m_oscState = genTremolo::CircuitState();
		if (std::abs(m_sampleRate - genTremolo::SAMPLE_RATE) > 0.5)
			m_oscState.setSampleRate(m_sampleRate);
		const auto settleCount = static_cast<size_t>(m_sampleRate * 2.0);
		for (size_t i = 0; i < settleCount; ++i)
			genTremolo::processSample(0.0, m_oscState);
	}

	double shuntImpedance() const
	{
		const double rUpper = R_VIB_POT * (1.0 - m_depth);
		const double rLower = R_VIB_POT * m_depth;
		const double top = (rUpper > 0.0) ? rUpper * R_VIB_BRIDGE / (rUpper + R_VIB_BRIDGE) : 0.0;
		const double low = (rLower > 0.0) ? rLower * m_rLdr / (rLower + m_rLdr) : 0.0;
		return top + low;
	}

	/// Normalised light (0..1) from the real LED current at the anode tap.
	double oscillatorDrive()
	{
		const auto r = genTremolo::processSample(0.0, m_oscState);
		const double iMa = ((V_RAIL - r.taps[0]) / R18_LED_SERIES) * 1000.0;
		const double x = std::clamp(iMa / LED_I_FULL_MA, 0.0, 1.0) * static_cast<double>(LED_LUT_N - 1);
		const int k = static_cast<int>(x);
		if (k + 1 < LED_LUT_N)
		{
			const double f = x - static_cast<double>(k);
			return m_ledLut[k] * (1.0 - f) + m_ledLut[k + 1] * f;
		}
		return m_ledLut[LED_LUT_N - 1];
	}

	genTremolo::CircuitState m_oscState;
	double m_sampleRate = 88200.0;
	double m_depth = 0.5;
	double m_rLdr = R_LDR_MAX;
	double m_ldrEnvelope = 0.0;
	double m_ldrAttack = 0.0;
	double m_ldrRelease = 0.0;
	double m_lnRMax = 0.0;
	double m_lnMinMinusMax = 0.0;
	std::array<double, LED_LUT_N> m_ledLut{};
};

} // namespace openWurli
