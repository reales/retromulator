/**
 * OpenWurli DSP — Power amplifier with closed-loop negative feedback
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include <cmath>
#include <algorithm>

namespace openWurli
{

class PowerAmp
{
public:
	PowerAmp()
		: m_openLoopGain(19000.0)
		, m_feedbackBeta(220.0 / (220.0 + 15000.0))
		, m_crossoverVt(0.013)
		, m_railLimit(22.0)
		, m_quiescentGain(0.1)
	{
		m_closedLoopGain = m_openLoopGain / (1.0 + m_openLoopGain * m_feedbackBeta);
	}

	double process(double input)
	{
		constexpr int NR_MAX_ITER = 8;
		constexpr double NR_TOL = 1e-6;

		double y = std::clamp(input * m_closedLoopGain, -m_railLimit + NR_TOL, m_railLimit - NR_TOL);

		for (int iter = 0; iter < NR_MAX_ITER; iter++)
		{
			const double error = input - m_feedbackBeta * y;
			const double v = m_openLoopGain * error;

			double fVal, fDeriv;
			forwardPath(v, fVal, fDeriv);

			const double residual = y - fVal;
			const double jacobian = 1.0 + m_openLoopGain * m_feedbackBeta * fDeriv;
			const double delta = residual / jacobian;
			y -= delta;

			if (std::abs(delta) < NR_TOL)
				break;
		}

		return y / m_railLimit;
	}

	void reset() {} // memoryless

private:
	void forwardPath(double v, double& fVal, double& fDeriv) const
	{
		const double vSq = v * v;
		const double vtSq = m_crossoverVt * m_crossoverVt;
		const double expTerm = std::exp(-vSq / vtSq);
		const double q = m_quiescentGain;
		const double crossGain = q + (1.0 - q) * (1.0 - expTerm);
		const double vCross = v * crossGain;

		const double dcrossDv = crossGain + v * (1.0 - q) * (2.0 * v / vtSq) * expTerm;

		const double tanhArg = vCross / m_railLimit;
		const double tanhVal = std::tanh(tanhArg);
		fVal = m_railLimit * tanhVal;
		fDeriv = (1.0 - tanhVal * tanhVal) * dcrossDv;
	}

	double m_openLoopGain;
	double m_feedbackBeta;
	double m_crossoverVt;
	double m_railLimit;
	double m_quiescentGain;
	double m_closedLoopGain;
};

} // namespace openWurli
