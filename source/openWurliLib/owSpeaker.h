/**
 * OpenWurli DSP — Speaker cabinet model
 * Ported from Rust openwurli-dsp (GPL v3)
 *
 * Hammerstein nonlinearity + HPF/LPF + thermal compression
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
		m_hpf = Biquad::highpass(95.0, 0.75, sampleRate);
		m_lpf = Biquad::lowpass(5500.0, 0.707, sampleRate);
		m_character = 1.0;
		m_thermalAlpha = 1.0 / (5.0 * sampleRate);
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
		// 1. Polynomial waveshaper
		const double x2 = input * input;
		const double x3 = x2 * input;
		const double shaped = (input + m_a2 * x2 + m_a3 * x3) / (1.0 + m_a2 + m_a3);

		// 2. Cone excursion limiter
		const double limited = std::tanh(shaped);

		// 3. Thermal voice coil compression
		m_thermalState += (x2 - m_thermalState) * m_thermalAlpha;
		const double thermalGain = 1.0 / (1.0 + m_thermalCoeff * std::sqrt(m_thermalState));

		// 4. Linear filters
		const double filtered = m_hpf.process(limited * thermalGain);
		return m_lpf.process(filtered);
	}

	void reset()
	{
		m_hpf.reset();
		m_lpf.reset();
		m_thermalState = 0.0;
	}

private:
	void updateCoefficients()
	{
		const double c = m_character;
		const double hpfHz = 20.0 * std::pow(95.0 / 20.0, c);
		const double lpfHz = 20000.0 * std::pow(5500.0 / 20000.0, c);
		m_hpf.setHighpass(hpfHz, 0.75, m_sampleRate);
		m_lpf.setLowpass(lpfHz, 0.707, m_sampleRate);

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
