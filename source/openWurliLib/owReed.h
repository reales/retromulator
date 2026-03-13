/**
 * OpenWurli DSP — Modal reed oscillator (7 damped sinusoidal modes)
 * Ported from Rust openwurli-dsp (GPL v3)
 *
 * Each mode uses a quadrature oscillator (sin/cos pair rotated per sample)
 * instead of computing sin(phase) per sample. Per-mode frequency jitter
 * (Ornstein-Uhlenbeck process) breaks digital phase coherence.
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

static constexpr double JITTER_SIGMA = 0.0004;
static constexpr double JITTER_TAU = 0.020;
static constexpr double SQRT_3 = 1.7320508080;
static constexpr uint64_t JITTER_SUBSAMPLE = 16;
static constexpr uint64_t RENORM_INTERVAL = 1024;

struct ReedMode
{
	double s = 0.0;           // quadrature sine
	double c = 1.0;           // quadrature cosine
	double cosInc = 1.0;
	double sinInc = 0.0;
	double phaseInc = 0.0;
	double amplitude = 0.0;
	double decayMult = 1.0;
	double envelope = 1.0;
	double jitterDrift = 0.0;
	double damperRate = 0.0;
	double damperMult = 1.0;
};

/// LCG PRNG → scaled uniform noise with unit variance
inline double lcgUniformScaled(uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	const double u = static_cast<double>(state >> 1) / (4294967295.0 / 2.0);
	return (u * 2.0 - 1.0) * SQRT_3;
}

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
		double sampleRate,
		uint32_t jitterSeed)
	{
		const double dt = 1.0 / sampleRate;
		m_jitterRevert = std::exp(-dt / JITTER_TAU);
		m_jitterDiffusion = JITTER_SIGMA * std::sqrt(1.0 - m_jitterRevert * m_jitterRevert);
		m_jitterState = std::max(jitterSeed, 1u);

		// Box-Muller initial drifts
		double initialDrifts[NUM_MODES];
		for (int i = 0; i < NUM_MODES; i++)
		{
			m_jitterState = m_jitterState * 1664525u + 1013904223u;
			double u1 = static_cast<double>(m_jitterState >> 1) / (4294967295.0 / 2.0);
			m_jitterState = m_jitterState * 1664525u + 1013904223u;
			double u2 = static_cast<double>(m_jitterState >> 1) / (4294967295.0 / 2.0);
			double r = std::sqrt(-2.0 * std::log(std::max(u1, 1e-30)));
			initialDrifts[i] = JITTER_SIGMA * r * std::cos(2.0 * M_PI * u2);
		}

		for (int i = 0; i < NUM_MODES; i++)
		{
			auto& m = m_modes[i];
			const double freq = fundamentalHz * modeRatiosArr[i];
			m.phaseInc = 2.0 * M_PI * freq / sampleRate;
			const double alphaNepers = decayRatesDb[i] / 8.686;
			const double decayPerSample = alphaNepers / sampleRate;

			m.s = 0.0;
			m.c = 1.0;
			m.cosInc = std::cos(m.phaseInc);
			m.sinInc = std::sin(m.phaseInc);
			m.amplitude = amplitudes[i];
			m.decayMult = std::exp(-decayPerSample);
			m.envelope = 1.0;
			m.jitterDrift = initialDrifts[i];
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
		const double revert = m_jitterRevert;
		const double diffusion = m_jitterDiffusion;

		for (size_t n = 0; n < numSamples; n++)
		{
			double sum = 0.0;

			// Advance damper
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

			// Onset ramp
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

			// Subsample jitter update
			if ((m_sample & (JITTER_SUBSAMPLE - 1)) == 0)
			{
				for (auto& m : m_modes)
				{
					const double noise = lcgUniformScaled(m_jitterState);
					m.jitterDrift = revert * m.jitterDrift + diffusion * noise;
				}
			}

			// Quadrature oscillator
			for (auto& m : m_modes)
			{
				sum += m.amplitude * m.s * onset * m.envelope;

				const double deltaPhase = m.jitterDrift * m.phaseInc;
				const double ci = m.cosInc - deltaPhase * m.sinInc;
				const double si = m.sinInc + deltaPhase * m.cosInc;
				const double sNew = m.s * ci + m.c * si;
				const double cNew = m.c * ci - m.s * si;
				m.s = sNew;
				m.c = cNew;

				m.envelope *= m.decayMult;
			}

			// Renormalize quadrature radius
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
	uint32_t m_jitterState = 1;
	double m_jitterRevert = 0.0;
	double m_jitterDiffusion = 0.0;
};

} // namespace openWurli
