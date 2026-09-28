/**
 * OpenWurli DSP — Power amplifier: behavioral closed-loop model with the drawn
 * R-30/C-10 feedback shelf (in band 69x, unity at DC, -3 dB at 33 Hz)
 * Ported from Rust openwurli-dsp 0.9.0 power_amp.rs (GPL v3)
 */
#pragma once

#include <cmath>
#include <algorithm>

namespace openWurli
{

class PowerAmp
{
public:
	PowerAmp() { init(44100.0); }

	/// The forward path is closed-form; only the R-30/C-10 feedback section depends on the rate.
	void init(double sampleRate)
	{
		const double k = 2.0 * sampleRate;
		const double tauZero = R30 * C10;          // 4.84 ms, zero at 33 Hz
		const double tauPole = (R30 + R31) * C10;  // 335 ms, pole at 0.48 Hz
		const double a0 = 1.0 + tauPole * k;
		m_fbB0 = (1.0 + tauZero * k) / a0;
		m_fbB1 = (1.0 - tauZero * k) / a0;
		m_fbA1 = (1.0 - tauPole * k) / a0;
		m_fbY1 = 0.0;
		m_fbPrev = 0.0;
	}

	double process(double input)
	{
		constexpr int NR_MAX_ITER = 8;
		constexpr double NR_TOL = 1e-6;

		const double fbHist = m_fbB1 * m_fbY1 - m_fbA1 * m_fbPrev;
		const double b0 = m_fbB0;

		double y = std::clamp((input - fbHist) * OPEN_LOOP_GAIN / (1.0 + OPEN_LOOP_GAIN * b0),
		                      -HEADROOM + NR_TOL, HEADROOM - NR_TOL);

		for (int iter = 0; iter < NR_MAX_ITER; iter++)
		{
			const double error = input - (b0 * y + fbHist);
			const double v = OPEN_LOOP_GAIN * error;

			double fVal, fDeriv;
			forwardPath(v, fVal, fDeriv);

			const double residual = y - fVal;
			const double jacobian = 1.0 + OPEN_LOOP_GAIN * b0 * fDeriv;
			const double delta = residual / jacobian;
			y -= delta;

			if (std::abs(delta) < NR_TOL)
				break;
		}

		m_fbPrev = b0 * y + fbHist;
		m_fbY1 = y;

		return y / HEADROOM;
	}

	void reset()
	{
		m_fbY1 = 0.0;
		m_fbPrev = 0.0;
	}

private:
	static constexpr double OPEN_LOOP_GAIN = 19000.0;
	static constexpr double R30 = 220.0;
	static constexpr double R31 = 15000.0;
	static constexpr double C10 = 22e-6;
	static constexpr double HEADROOM = 22.0;
	static constexpr double CROSSOVER_VT = 0.013;
	static constexpr double QUIESCENT_GAIN = 0.1;

	static void forwardPath(double v, double& fVal, double& fDeriv)
	{
		const double vSq = v * v;
		const double vtSq = CROSSOVER_VT * CROSSOVER_VT;
		const double expTerm = std::exp(-vSq / vtSq);
		const double q = QUIESCENT_GAIN;
		const double crossGain = q + (1.0 - q) * (1.0 - expTerm);
		const double vCross = v * crossGain;

		const double dcrossDv = crossGain + v * (1.0 - q) * (2.0 * v / vtSq) * expTerm;

		const double tanhVal = std::tanh(vCross / HEADROOM);
		fVal = HEADROOM * tanhVal;
		fDeriv = (1.0 - tanhVal * tanhVal) * dcrossDv;
	}

	double m_fbB0 = 0.0, m_fbB1 = 0.0, m_fbA1 = 0.0;
	double m_fbY1 = 0.0;
	double m_fbPrev = 0.0;
};

} // namespace openWurli
