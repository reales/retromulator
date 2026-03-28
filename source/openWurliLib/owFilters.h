/**
 * OpenWurli DSP — Shared filter primitives
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

// ── Biquad — Direct Form II Transposed ──

class Biquad
{
public:
	Biquad() = default;

	static Biquad bandpass(double centerHz, double q, double sampleRate)
	{
		Biquad f;
		const double w0 = 2.0 * M_PI * centerHz / sampleRate;
		const double alpha = std::sin(w0) / (2.0 * q);
		const double cosW0 = std::cos(w0);
		const double a0 = 1.0 + alpha;
		f.m_b0 = alpha / a0;
		f.m_b1 = 0.0;
		f.m_b2 = -alpha / a0;
		f.m_a1 = -2.0 * cosW0 / a0;
		f.m_a2 = (1.0 - alpha) / a0;
		return f;
	}

	static Biquad lowpass(double cutoffHz, double q, double sampleRate)
	{
		Biquad f;
		const double w0 = 2.0 * M_PI * cutoffHz / sampleRate;
		const double alpha = std::sin(w0) / (2.0 * q);
		const double cosW0 = std::cos(w0);
		const double b1 = 1.0 - cosW0;
		const double a0 = 1.0 + alpha;
		f.m_b0 = (b1 / 2.0) / a0;
		f.m_b1 = b1 / a0;
		f.m_b2 = f.m_b0;
		f.m_a1 = -2.0 * cosW0 / a0;
		f.m_a2 = (1.0 - alpha) / a0;
		return f;
	}

	static Biquad highpass(double cutoffHz, double q, double sampleRate)
	{
		Biquad f;
		const double w0 = 2.0 * M_PI * cutoffHz / sampleRate;
		const double alpha = std::sin(w0) / (2.0 * q);
		const double cosW0 = std::cos(w0);
		const double b1 = -(1.0 + cosW0);
		const double a0 = 1.0 + alpha;
		f.m_b0 = (-b1 / 2.0) / a0;
		f.m_b1 = b1 / a0;
		f.m_b2 = f.m_b0;
		f.m_a1 = -2.0 * cosW0 / a0;
		f.m_a2 = (1.0 - alpha) / a0;
		return f;
	}

	void setHighpass(double cutoffHz, double q, double sampleRate)
	{
		auto tmp = highpass(cutoffHz, q, sampleRate);
		m_b0 = tmp.m_b0; m_b1 = tmp.m_b1; m_b2 = tmp.m_b2;
		m_a1 = tmp.m_a1; m_a2 = tmp.m_a2;
	}

	void setLowpass(double cutoffHz, double q, double sampleRate)
	{
		auto tmp = lowpass(cutoffHz, q, sampleRate);
		m_b0 = tmp.m_b0; m_b1 = tmp.m_b1; m_b2 = tmp.m_b2;
		m_a1 = tmp.m_a1; m_a2 = tmp.m_a2;
	}

	double process(double x)
	{
		const double y = m_b0 * x + m_s1;
		m_s1 = m_b1 * x - m_a1 * y + m_s2;
		m_s2 = m_b2 * x - m_a2 * y;
		return y;
	}

	void reset() { m_s1 = 0.0; m_s2 = 0.0; }

private:
	double m_b0 = 0.0, m_b1 = 0.0, m_b2 = 0.0;
	double m_a1 = 0.0, m_a2 = 0.0;
	double m_s1 = 0.0, m_s2 = 0.0;
};

} // namespace openWurli
