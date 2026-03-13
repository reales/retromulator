/**
 * OpenWurli DSP — Electrostatic pickup model (time-varying RC circuit)
 * Ported from Rust openwurli-dsp (GPL v3)
 *
 * C(y) = C_0 / (1 - y) — nonlinear capacitance modulation
 */
#pragma once

#include <cmath>
#include <algorithm>

namespace openWurli
{

static constexpr double PICKUP_TAU = 287.0e3 * 240.0e-12; // 68.88 µs → f_c = 2312 Hz
static constexpr double PICKUP_SENSITIVITY = 1.8375;
static constexpr double PICKUP_MAX_Y = 0.98;
static constexpr double PICKUP_DEFAULT_DS = 0.85;

class Pickup
{
public:
	Pickup() = default;

	void init(double sampleRate, double displacementScale = PICKUP_DEFAULT_DS)
	{
		const double dt = 1.0 / sampleRate;
		m_beta = dt / (2.0 * PICKUP_TAU);
		m_q = 1.0;
		m_displacementScale = displacementScale;
	}

	void setDisplacementScale(double scale) { m_displacementScale = scale; }

	void process(double* buffer, size_t numSamples)
	{
		const double scale = m_displacementScale;
		const double beta = m_beta;

		for (size_t i = 0; i < numSamples; i++)
		{
			double y = std::clamp(buffer[i] * scale, -PICKUP_MAX_Y, PICKUP_MAX_Y);
			const double oneMinusY = 1.0 - y;
			const double alpha = beta * oneMinusY;
			const double qNext = (m_q * (1.0 - alpha) + 2.0 * beta) / (1.0 + alpha);
			m_q = qNext;
			buffer[i] = (qNext * oneMinusY - 1.0) * PICKUP_SENSITIVITY;
		}
	}

	void reset() { m_q = 1.0; }

private:
	double m_q = 1.0;
	double m_beta = 0.0;
	double m_displacementScale = PICKUP_DEFAULT_DS;
};

} // namespace openWurli
