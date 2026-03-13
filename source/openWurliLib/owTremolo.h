/**
 * OpenWurli DSP — Tremolo: LFO + LED + CdS LDR model
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

class Tremolo
{
public:
	Tremolo() = default;

	void init(double rate, double depth, double sampleRate)
	{
		constexpr double attackTau = 0.003;
		constexpr double releaseTau = 0.050;
		constexpr double rLdrMin = 50.0;

		m_phase = 0.0;
		m_phaseInc = 2.0 * M_PI * rate / sampleRate;
		m_depth = depth;
		m_rLdr = m_rLdrMax;
		m_ldrEnvelope = 0.0;
		m_ldrAttack = std::exp(-1.0 / (attackTau * sampleRate));
		m_ldrRelease = std::exp(-1.0 / (releaseTau * sampleRate));
		m_gamma = 1.1;
		m_lnRMax = std::log(m_rLdrMax);
		m_lnMinMinusMax = std::log(rLdrMin) - m_lnRMax;
		m_rSeries = 18000.0;
	}

	void setRate(double rate, double sampleRate)
	{
		m_phaseInc = 2.0 * M_PI * rate / sampleRate;
	}

	void setDepth(double depth)
	{
		m_depth = std::clamp(depth, 0.0, 1.0);
		const double potResistance = 50000.0 * (1.0 - m_depth);
		m_rSeries = 18000.0 + potResistance;
	}

	double process()
	{
		const double lfo = std::sin(m_phase);
		m_phase += m_phaseInc;
		if (m_phase >= 2.0 * M_PI)
			m_phase -= 2.0 * M_PI;

		const double ledDrive = std::max(lfo, 0.0) * m_depth;

		const double coeff = (ledDrive > m_ldrEnvelope) ? m_ldrAttack : m_ldrRelease;
		m_ldrEnvelope = ledDrive + coeff * (m_ldrEnvelope - ledDrive);

		const double drive = std::clamp(m_ldrEnvelope, 0.0, 1.0);
		if (drive < 1e-6)
			m_rLdr = m_rLdrMax;
		else
		{
			const double logR = m_lnRMax + m_lnMinMinusMax * std::pow(drive, m_gamma);
			m_rLdr = std::exp(logR);
		}

		return m_rSeries + m_rLdr;
	}

	double currentResistance() const { return m_rSeries + m_rLdr; }

	void reset()
	{
		m_phase = 0.0;
		m_ldrEnvelope = 0.0;
		m_rLdr = m_rLdrMax;
	}

private:
	double m_phase = 0.0;
	double m_phaseInc = 0.0;
	double m_depth = 0.5;
	double m_rLdr = 1000000.0;
	double m_ldrEnvelope = 0.0;
	double m_ldrAttack = 0.0;
	double m_ldrRelease = 0.0;
	static constexpr double m_rLdrMax = 1000000.0;
	double m_gamma = 1.1;
	double m_lnRMax = 0.0;
	double m_lnMinMinusMax = 0.0;
	double m_rSeries = 18000.0;
};

} // namespace openWurli
