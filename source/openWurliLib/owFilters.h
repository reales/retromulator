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

// ── 1-pole HPF: y[n] = alpha * (y[n-1] + x[n] - x[n-1]) ──

class OnePoleHpf
{
public:
	OnePoleHpf() = default;

	void init(double cutoffHz, double sampleRate)
	{
		const double rc = 1.0 / (2.0 * M_PI * cutoffHz);
		const double dt = 1.0 / sampleRate;
		m_alpha = rc / (rc + dt);
		m_prevX = 0.0;
		m_prevY = 0.0;
	}

	double process(double x)
	{
		const double y = m_alpha * (m_prevY + x - m_prevX);
		m_prevX = x;
		m_prevY = y;
		return y;
	}

	void reset()
	{
		m_prevX = 0.0;
		m_prevY = 0.0;
	}

private:
	double m_alpha = 0.0;
	double m_prevX = 0.0;
	double m_prevY = 0.0;
};

// ── 1-pole LPF: y[n] = alpha * x[n] + (1 - alpha) * y[n-1] ──

class OnePoleLpf
{
public:
	OnePoleLpf() = default;

	void init(double cutoffHz, double sampleRate)
	{
		m_dt = 1.0 / sampleRate;
		const double rc = 1.0 / (2.0 * M_PI * cutoffHz);
		m_alpha = m_dt / (rc + m_dt);
		m_oneMinusAlpha = 1.0 - m_alpha;
		m_prevY = 0.0;
	}

	void setCutoff(double cutoffHz)
	{
		const double rc = 1.0 / (2.0 * M_PI * cutoffHz);
		m_alpha = m_dt / (rc + m_dt);
		m_oneMinusAlpha = 1.0 - m_alpha;
	}

	double process(double x)
	{
		const double y = m_alpha * x + m_oneMinusAlpha * m_prevY;
		m_prevY = y;
		return y;
	}

	double prevY() const { return m_prevY; }
	void setPrevY(double v) { m_prevY = v; }

	void reset() { m_prevY = 0.0; }

private:
	double m_alpha = 0.0;
	double m_oneMinusAlpha = 1.0;
	double m_dt = 0.0;
	double m_prevY = 0.0;
};

// ── TPT (Topology-Preserving Transform) 1-pole LPF ──

class TptLpf
{
public:
	TptLpf() = default;

	void init(double cutoffHz, double sampleRate)
	{
		m_sampleRate = sampleRate;
		m_g = std::tan(M_PI * cutoffHz / sampleRate);
		m_gDenom = m_g / (1.0 + m_g);
		m_s = 0.0;
	}

	double process(double x)
	{
		const double v = (x - m_s) * m_gDenom;
		const double y = v + m_s;
		m_s = y + v;
		return y;
	}

	void setCutoff(double cutoffHz)
	{
		m_g = std::tan(M_PI * cutoffHz / m_sampleRate);
		m_gDenom = m_g / (1.0 + m_g);
	}

	double saveState() const { return m_s; }
	void restoreState(double s) { m_s = s; }
	void reset() { m_s = 0.0; }

private:
	double m_g = 0.0;
	double m_gDenom = 0.0;
	double m_s = 0.0;
	double m_sampleRate = 44100.0;
};

// ── DC Blocker (1-pole HPF at 20 Hz) ──

class DcBlocker
{
public:
	DcBlocker() = default;
	void init(double sampleRate) { m_hpf.init(20.0, sampleRate); }
	double process(double x) { return m_hpf.process(x); }
	void reset() { m_hpf.reset(); }
private:
	OnePoleHpf m_hpf;
};

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
