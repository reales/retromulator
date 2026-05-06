/**
 * OpenWurli DSP — Tremolo: Twin-T circuit oscillator + CdS LDR model
 * Ported from Rust openwurli-dsp 0.3 (GPL v3)
 *
 * Uses melange-generated Twin-T oscillator circuit (real waveform shape).
 * The real 200A has no rate knob — oscillation is fixed at ~5.6 Hz
 * by passive components.
 */
#pragma once

#include "owGenTremolo.h"
#include <cmath>
#include <algorithm>

namespace openWurli
{

class Tremolo
{
public:
	Tremolo() = default;

	void init(double depth, double sampleRate)
	{
		constexpr double attackTau = 0.003;
		constexpr double releaseTau = 0.050;
		// LDR bright-phase floor lifted from 50 → 18 320 Ω so the shunt
		// (680 Ω + r_ldr) lands at the documented 19 kΩ bright calibration
		// point instead of clamping into the preamp's 1 kΩ floor.
		constexpr double rLdrMin = 18320.0;

		m_depth = depth;
		m_rLdr = m_rLdrMax;
		m_ldrEnvelope = 0.0;
		m_ldrAttack = std::exp(-1.0 / (attackTau * sampleRate));
		m_ldrRelease = std::exp(-1.0 / (releaseTau * sampleRate));
		m_gamma = 1.1;
		m_lnRMax = std::log(m_rLdrMax);
		m_lnMinMinusMax = std::log(rLdrMin) - m_lnRMax;

		// Initialize Twin-T circuit oscillator
		m_oscState = genTremolo::CircuitState();
		if (std::abs(sampleRate - genTremolo::SAMPLE_RATE) > 0.5)
			m_oscState.setSampleRate(sampleRate);

		// Settle oscillator to reach steady-state amplitude
		const auto settleCount = static_cast<size_t>(sampleRate * 2.0);
		for (size_t i = 0; i < settleCount; ++i)
			genTremolo::processSample(0.0, m_oscState);
	}

	void setDepth(double depth)
	{
		// The 50 kΩ VIBRATO pot is in the LED drive path (already scaled
		// via `led_drive = osc * depth`) — it is NOT in the shunt path.
		// Shunt = R_SHUNT_SERIES (680 Ω, LG-1 pin 5) + r_ldr only.
		m_depth = std::clamp(depth, 0.0, 1.0);
	}

	double process()
	{
		// Step 1: Get oscillator drive (0..1) from Twin-T circuit
		const double ledDrive = oscillatorDrive() * m_depth;

		// Step 2: CdS LDR envelope (asymmetric attack/release)
		const double coeff = (ledDrive > m_ldrEnvelope) ? m_ldrAttack : m_ldrRelease;
		m_ldrEnvelope = ledDrive + coeff * (m_ldrEnvelope - ledDrive);

		// Step 3: CdS power-law resistance
		const double drive = std::clamp(m_ldrEnvelope, 0.0, 1.0);
		if (drive < 1e-6)
			m_rLdr = m_rLdrMax;
		else
		{
			const double logR = m_lnRMax + m_lnMinMinusMax * std::pow(drive, m_gamma);
			m_rLdr = std::exp(logR);
		}

		return R_SHUNT_SERIES + m_rLdr;
	}

	double currentResistance() const { return R_SHUNT_SERIES + m_rLdr; }

	void reset()
	{
		m_oscState.reset();
		m_ldrEnvelope = 0.0;
		m_rLdr = m_rLdrMax;
	}

private:
	double oscillatorDrive()
	{
		const double vOut = genTremolo::processSample(0.0, m_oscState)[0];
		// Map collector voltage to LED drive: low V = bright LED = high drive
		return std::clamp((V_OUT_MAX - vOut) / (V_OUT_MAX - V_OUT_MIN), 0.0, 1.0);
	}

	// Twin-T oscillator output voltage range (from ngspice/melange validation)
	static constexpr double V_OUT_MIN = 0.70;
	static constexpr double V_OUT_MAX = 10.95;

	// Fixed series resistance in the LDR shunt path (680 Ω on LG-1 pin 5).
	static constexpr double R_SHUNT_SERIES = 680.0;

	genTremolo::CircuitState m_oscState;
	double m_depth = 0.5;
	double m_rLdr = 1000000.0;
	double m_ldrEnvelope = 0.0;
	double m_ldrAttack = 0.0;
	double m_ldrRelease = 0.0;
	static constexpr double m_rLdrMax = 1000000.0;
	double m_gamma = 1.1;
	double m_lnRMax = 0.0;
	double m_lnMinMinusMax = 0.0;
};

} // namespace openWurli
