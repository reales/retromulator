/**
 * OpenWurli DSP — Modal reed oscillator (7 damped sinusoidal modes)
 * Ported from Rust openwurli-dsp 0.9.0 reed.rs (GPL v3)
 *
 * Each mode uses a quadrature oscillator (sin/cos pair rotated per sample)
 * and holds its frequency exactly. Mode 1 carries an amplitude-dependent
 * loss (quadratic air drag).
 */
#pragma once

#include "owTables.h"
#include <cmath>
#include <cstdint>
#include <array>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

static constexpr uint64_t RENORM_INTERVAL = 1024;

struct ReedMode
{
	double s = 0.0;           // quadrature sine
	double c = 1.0;           // quadrature cosine
	double cosInc = 1.0;
	double sinInc = 0.0;
	double amplitude = 0.0;
	double decayMult = 1.0;
	double envelope = 1.0;
	double damperRate = 0.0;
	double damperMult = 1.0;
};

class ModalReed
{
public:
	ModalReed() = default;

	void init(
		double fundamentalHz,
		const std::array<double, NUM_MODES>& modeRatiosArr,
		const std::array<double, NUM_MODES>& amplitudes,
		const std::array<double, NUM_MODES>& decayRatesDb,
		double onsetTimeS,
		double velocity,
		double sampleRate)
	{
		for (int i = 0; i < NUM_MODES; i++)
		{
			auto& m = m_modes[i];
			const double freq = fundamentalHz * modeRatiosArr[i];
			const double phaseInc = 2.0 * M_PI * freq / sampleRate;
			const double alphaNepers = decayRatesDb[i] / 8.686;
			const double decayPerSample = alphaNepers / sampleRate;

			m.s = 0.0;
			m.c = 1.0;
			m.cosInc = std::cos(phaseInc);
			m.sinInc = std::sin(phaseInc);
			m.amplitude = amplitudes[i];
			m.decayMult = std::exp(-decayPerSample);
			m.envelope = 1.0;
			m.damperRate = 0.0;
			m.damperMult = 1.0;
		}

		const auto rampSamps = static_cast<uint64_t>(std::round(onsetTimeS * sampleRate));
		m_onsetRampSamples = rampSamps;
		m_onsetRampInc = (rampSamps > 0) ? (M_PI / static_cast<double>(rampSamps)) : 0.0;
		m_onsetShapeExp = 1.0 + (1.0 - velocity);

		m_sample = 0;
		m_damperActive = false;
		m_damperRampSamples = 0.0;
		m_damperReleaseCount = 0.0;
		m_damperRampDone = false;
		m_dragK = 0.0;
	}

	/// Quadratic air drag on mode 1: betaDbPerMm is the extra decay in dB/s per mm of swing
	/// at the pickup; swingMm is the swing one unit of model displacement stands for.
	void setAirDrag(double betaDbPerMm, double swingMm, double sampleRate)
	{
		const double betaNepers = betaDbPerMm / 8.686;
		m_dragK = betaNepers * std::abs(m_modes[0].amplitude) * swingMm / sampleRate;
	}

	void startDamper(uint8_t midiNote, double sampleRate)
	{
		if (midiNote >= 92) return; // top 5 keys: no damper

		const double baseRate = std::max(55.0 * std::pow(2.0, (static_cast<double>(midiNote) - 60.0) / 24.0), 0.5);
		for (int i = 0; i < NUM_MODES; i++)
		{
			auto& m = m_modes[i];
			const double factor = std::min(baseRate * std::pow(3.0, static_cast<double>(i)), 2000.0);
			m.damperRate = factor / sampleRate;
			m.damperMult = std::exp(-m.damperRate);
		}

		double rampTime;
		if (midiNote < 48) rampTime = 0.050;
		else if (midiNote < 72) rampTime = 0.025;
		else rampTime = 0.008;

		m_damperRampSamples = rampTime * sampleRate;
		m_damperActive = true;
		m_damperReleaseCount = 0.0;
		m_damperRampDone = false;
	}

	void render(double* output, size_t numSamples)
	{
		for (size_t n = 0; n < numSamples; n++)
		{
			double sum = 0.0;

			if (m_damperActive)
			{
				m_damperReleaseCount += 1.0;
				const double t = m_damperReleaseCount;
				const double ramp = m_damperRampSamples;
				if (!m_damperRampDone)
				{
					if (t > ramp)
						m_damperRampDone = true;
					else
					{
						for (auto& m : m_modes)
						{
							const double instRate = m.damperRate * t / ramp;
							m.envelope *= std::exp(-instRate);
						}
					}
				}
				if (m_damperRampDone)
				{
					for (auto& m : m_modes)
						m.envelope *= m.damperMult;
				}
			}

			double onset = 1.0;
			if (m_sample < m_onsetRampSamples)
			{
				const double nn = static_cast<double>(m_sample);
				const double cosine = 0.5 * (1.0 - std::cos(nn * m_onsetRampInc));
				if (m_onsetShapeExp <= 1.001)
					onset = cosine;
				else if (m_onsetShapeExp >= 1.999)
					onset = cosine * cosine;
				else
					onset = std::pow(cosine, m_onsetShapeExp);
			}

			for (auto& m : m_modes)
			{
				sum += m.amplitude * m.s * onset * m.envelope;

				const double sNew = m.s * m.cosInc + m.c * m.sinInc;
				const double cNew = m.c * m.cosInc - m.s * m.sinInc;
				m.s = sNew;
				m.c = cNew;

				m.envelope *= m.decayMult;
			}

			if (m_dragK > 0.0)
			{
				const double env = m_modes[0].envelope;
				m_modes[0].envelope = env * (1.0 - m_dragK * env);
			}

			if ((m_sample & (RENORM_INTERVAL - 1)) == 0 && m_sample > 0)
			{
				for (auto& m : m_modes)
				{
					const double rSq = m.s * m.s + m.c * m.c;
					const double rInv = 1.0 / std::sqrt(rSq);
					m.s *= rInv;
					m.c *= rInv;
				}
			}

			output[n] += sum;
			m_sample++;
		}
	}

	bool isSilent(double thresholdDb) const
	{
		const double thresholdLinear = std::pow(10.0, thresholdDb / 20.0);
		for (const auto& m : m_modes)
		{
			if (std::abs(m.amplitude * m.envelope) > thresholdLinear)
				return false;
		}
		return true;
	}

	bool isDamping() const { return m_damperActive; }

	double releaseSeconds(double sampleRate) const
	{
		return m_damperActive ? m_damperReleaseCount / sampleRate : 0.0;
	}

private:
	std::array<ReedMode, NUM_MODES> m_modes;
	uint64_t m_sample = 0;
	uint64_t m_onsetRampSamples = 0;
	double m_onsetRampInc = 0.0;
	double m_onsetShapeExp = 1.0;
	bool m_damperActive = false;
	double m_damperRampSamples = 0.0;
	double m_damperReleaseCount = 0.0;
	bool m_damperRampDone = false;
	double m_dragK = 0.0;
};

} // namespace openWurli
