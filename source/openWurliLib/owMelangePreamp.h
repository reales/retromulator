/**
 * OpenWurli DSP — Melange-generated DK preamp adapter
 * Ported from Rust openwurli-dsp 0.3 dk_preamp/melange_adapter.rs (GPL v3)
 *
 * Uses the 12-node melange-generated circuit solver with Sherman-Morrison
 * pot correction. Shadow preamp for pump cancellation.
 */
#pragma once

#include "owGenPreamp.h"
#include <cmath>
#include <algorithm>

namespace openWurli
{

class MelangePreamp
{
public:
	MelangePreamp() = default;

	void init(double sampleRate)
	{
		m_sampleRate = sampleRate;
		m_main = computeSettledState();
		m_shadow = m_main;

		if (std::abs(sampleRate - genPreamp::SAMPLE_RATE) > 0.5)
		{
			m_main.setSampleRate(sampleRate);
			m_shadow.setSampleRate(sampleRate);
		}
	}

	double processSample(double input)
	{
		const double mainOut = genPreamp::processSample(input, m_main)[0];
		const double pump = genPreamp::processSample(0.0, m_shadow)[0];
		const double result = mainOut - pump;

		if (!std::isfinite(result))
		{
			reset();
			return 0.0;
		}
		return result;
	}

	void setLdrResistance(double rLdrPath)
	{
		const double r = std::max(rLdrPath, 1000.0);
		m_main.pot_0_resistance = r;
		m_shadow.pot_0_resistance = r;
	}

	void reset()
	{
		m_main = computeSettledState();
		m_shadow = m_main;

		if (std::abs(m_sampleRate - genPreamp::SAMPLE_RATE) > 0.5)
		{
			m_main.setSampleRate(m_sampleRate);
			m_shadow.setSampleRate(m_sampleRate);
		}
	}

private:
	static genPreamp::CircuitState computeSettledState()
	{
		genPreamp::CircuitState s;
		for (int i = 0; i < 176400; ++i)
			genPreamp::processSample(0.0, s);
		return s;
	}

	genPreamp::CircuitState m_main;
	genPreamp::CircuitState m_shadow;
	double m_sampleRate = 48000.0;
};

} // namespace openWurli
