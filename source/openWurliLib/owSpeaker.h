/**
 * OpenWurli DSP — Speaker cabinet model
 * Ported from Rust openwurli-dsp 0.9.0 speaker.rs (GPL v3)
 *
 * Hammerstein nonlinearity + HPF/LPF + thermal compression. At character 0
 * the stage is a true passthrough; the filters keep running so their state
 * is warm when character rises.
 */
#pragma once

#include "owFilters.h"
#include <cmath>
#include <algorithm>

namespace openWurli
{

class Speaker
{
public:
	Speaker() = default;

	void init(double sampleRate)
	{
		m_sampleRate = sampleRate;
		m_hpf = Biquad::highpass(HPF_AUTHENTIC_HZ, HPF_Q, sampleRate);
		m_lpf = Biquad::lowpass(LPF_AUTHENTIC_HZ, LPF_Q, sampleRate);
		m_character = 1.0;
		m_thermalAlpha = 1.0 / (THERMAL_TAU * sampleRate);
		m_thermalState = 0.0;
		updateCoefficients();
	}

	void setCharacter(double character)
	{
		const double c = std::clamp(character, 0.0, 1.0);
		if (std::abs(c - m_character) > 0.002)
		{
			m_character = c;
			updateCoefficients();
		}
	}

	double process(double input)
	{
		const double x2 = input * input;
		const double x3 = x2 * input;
		const double shaped = (input + m_a2 * x2 + m_a3 * x3) / (1.0 + m_a2 + m_a3);

		const double limited = (m_character < 0.001) ? shaped : std::tanh(shaped);

		m_thermalState += (x2 - m_thermalState) * m_thermalAlpha;
		const double thermalGain = 1.0 / (1.0 + m_thermalCoeff * std::sqrt(m_thermalState));

		const double filtered = m_hpf.process(limited * thermalGain);
		const double out = m_lpf.process(filtered);
		return (m_character < 0.001) ? input : out;
	}

	void reset()
	{
		m_hpf.reset();
		m_lpf.reset();
		m_thermalState = 0.0;
	}

private:
	static constexpr double HPF_AUTHENTIC_HZ = 30.0;
	static constexpr double HPF_Q = 0.75;
	static constexpr double LPF_AUTHENTIC_HZ = 5500.0;
	static constexpr double LPF_Q = 0.707;
	static constexpr double HPF_BYPASS_HZ = 20.0;
	static constexpr double LPF_BYPASS_HZ = 20000.0;
	static constexpr double THERMAL_TAU = 5.0;

	void updateCoefficients()
	{
		const double c = m_character;
		const double hpfHz = HPF_BYPASS_HZ * std::pow(HPF_AUTHENTIC_HZ / HPF_BYPASS_HZ, c);
		const double lpfHz = LPF_BYPASS_HZ * std::pow(LPF_AUTHENTIC_HZ / LPF_BYPASS_HZ, c);
		m_hpf.setHighpass(hpfHz, HPF_Q, m_sampleRate);
		m_lpf.setLowpass(lpfHz, LPF_Q, m_sampleRate);

		m_a2 = 0.2 * c;
		m_a3 = 0.6 * c;
		m_thermalCoeff = 2.0 * c;
	}

	Biquad m_hpf, m_lpf;
	double m_character = 1.0;
	double m_sampleRate = 44100.0;
	double m_a2 = 0.0, m_a3 = 0.0;
	double m_thermalCoeff = 0.0;
	double m_thermalAlpha = 0.0;
	double m_thermalState = 0.0;
};

} // namespace openWurli
