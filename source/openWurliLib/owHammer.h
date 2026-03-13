/**
 * OpenWurli DSP — Hammer model: Gaussian dwell filter + attack noise
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include "owFilters.h"
#include "owTables.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace openWurli
{

/// Hammer dwell time (contact duration)
inline double dwellTime(double velocity, double fundamentalHz)
{
	const double cycles = 0.75 + 0.25 * (1.0 - velocity);
	return std::clamp(cycles / fundamentalHz, 0.0003, 0.020);
}

/// Onset ramp time (reed mechanical inertia)
inline double onsetRampTime(double velocity, double fundamentalHz)
{
	const double periodS = 1.0 / fundamentalHz;
	const double periods = 2.5 + 2.5 * (1.0 - velocity);
	return std::clamp(periods * periodS, 0.002, 0.030);
}

/// Gaussian dwell filter per-mode attenuation
inline std::array<double, NUM_MODES> dwellAttenuation(
	double velocity, double fundamentalHz,
	const std::array<double, NUM_MODES>& modeRatiosArr)
{
	const double tDwell = dwellTime(velocity, fundamentalHz);
	constexpr double sigmaSq = 8.0 * 8.0;

	std::array<double, NUM_MODES> atten;
	for (int i = 0; i < NUM_MODES; i++)
	{
		const double ft = fundamentalHz * modeRatiosArr[i] * tDwell;
		atten[i] = std::exp(-ft * ft / (2.0 * sigmaSq));
	}

	const double a0 = atten[0];
	if (a0 > 1e-30)
	{
		for (auto& a : atten)
			a /= a0;
	}
	return atten;
}

/// Attack noise generator — exponentially decaying bandpass noise
class AttackNoise
{
public:
	AttackNoise() = default;

	void init(double velocity, double fundamentalHz, double sampleRate, uint32_t seed)
	{
		m_amplitude = 0.025 * velocity * velocity;
		constexpr double tau = 0.003;
		m_decayPerSample = std::exp(-1.0 / (tau * sampleRate));
		m_remaining = static_cast<uint32_t>(0.015 * sampleRate);

		const double center = std::clamp(fundamentalHz * 5.0, 200.0, 2000.0);
		m_bpf = Biquad::bandpass(center, 0.7, sampleRate);
		m_rngState = seed;
	}

	size_t render(double* output, size_t numSamples)
	{
		const size_t count = std::min(static_cast<size_t>(m_remaining), numSamples);
		double amp = m_amplitude;

		for (size_t i = 0; i < count; i++)
		{
			const double noise = nextNoise();
			const double filtered = m_bpf.process(noise);
			output[i] += amp * filtered;
			amp *= m_decayPerSample;
		}

		m_amplitude = amp;
		m_remaining -= static_cast<uint32_t>(count);
		return count;
	}

	bool isDone() const { return m_remaining == 0; }
	void disable() { m_remaining = 0; }

private:
	double nextNoise()
	{
		m_rngState = m_rngState * 1664525u + 1013904223u;
		return static_cast<double>(static_cast<int32_t>(m_rngState)) / static_cast<double>(INT32_MAX);
	}

	double m_amplitude = 0.0;
	double m_decayPerSample = 0.0;
	uint32_t m_remaining = 0;
	Biquad m_bpf;
	uint32_t m_rngState = 0;
};

} // namespace openWurli
