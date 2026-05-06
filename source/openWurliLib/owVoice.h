/**
 * OpenWurli DSP — Single voice: reed + hammer + pickup + decay
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include "owReed.h"
#include "owPickup.h"
#include "owHammer.h"
#include "owTables.h"
#include "owVariation.h"
#include "owMlpCorrection.h"

#include <cstring>

namespace openWurli
{

class Voice
{
public:
	Voice() = default;

	void noteOn(uint8_t midiNote, double velocity, double sampleRate, uint32_t noiseSeed, bool mlpEnabled)
	{
		m_midiNote = std::clamp(midiNote, MIDI_LO, MIDI_HI);
		m_sampleRate = sampleRate;

		const auto params = noteParams(m_midiNote);
		const double detunedFundamental = params.fundamentalHz * freqDetune(m_midiNote);

		const auto dwell = dwellAttenuation(velocity, detunedFundamental, params.modeRatiosArr);
		const double onsetTime = onsetRampTime(velocity, detunedFundamental);
		const auto ampOffsets = modeAmplitudeOffsets(m_midiNote);

		std::array<double, NUM_MODES> amplitudes;
		for (int i = 0; i < NUM_MODES; i++)
			amplitudes[i] = params.modeAmplitudes[i] * dwell[i] * ampOffsets[i];

		// Velocity curve
		const double velExp = velocityExponent(m_midiNote);
		const double velScale = std::pow(velocityScurve(velocity), velExp);
		for (auto& a : amplitudes)
			a *= velScale;

		// MLP corrections
		const auto corrections = mlpEnabled ? MlpCorrections::infer(m_midiNote, velocity) : MlpCorrections::identity();

		// Apply frequency corrections to modes 1-5
		auto correctedRatios = params.modeRatiosArr;
		for (int i = 0; i < std::min(5, NUM_MODES - 1); i++)
			correctedRatios[i + 1] *= std::pow(2.0, corrections.freqOffsetsCents[i] / 1200.0);

		// Apply decay corrections to modes 1-5
		auto correctedDecay = params.modeDecayRatesArr;
		for (int i = 0; i < std::min(5, NUM_MODES - 1); i++)
			correctedDecay[i + 1] /= corrections.decayOffsets[i];

		// Displacement scale correction
		const double baseDs = pickupDisplacementScale(m_midiNote);
		const double correctedDs = baseDs * corrections.dsCorrection;

		m_reed.init(detunedFundamental, correctedRatios, amplitudes, correctedDecay,
					onsetTime, velocity, sampleRate, noiseSeed);

		m_pickup.init(sampleRate, correctedDs);
		m_noise.init(velocity, detunedFundamental, sampleRate, noiseSeed);

		// MLP ds_correction shifts pickup drive, which changes output level
		// as a side effect (the MLP targets H2/H1 spectral shape, not level).
		// sqrt of the proxy ratio matches the RC pickup's smoothing — the
		// static Fourier proxy overestimates response at high ds because
		// charge dynamics self-limit peak excursions. Empirically: half-in-dB.
		double mlpLevelCompensation = 1.0;
		if (std::abs(corrections.dsCorrection - 1.0) > 1e-6)
		{
			constexpr double HPF_FC = 2312.0;
			const double f0 = midiToFreq(m_midiNote);
			const double proxyBase = pickupRmsProxy(baseDs, f0, HPF_FC);
			const double proxyCorrected = pickupRmsProxy(correctedDs, f0, HPF_FC);
			if (proxyCorrected > 1e-10)
				mlpLevelCompensation = std::sqrt(proxyBase / proxyCorrected);
		}

		m_postPickupGain = outputScale(m_midiNote, velocity) * mlpLevelCompensation;
		m_active = true;
	}

	void noteOff()
	{
		m_reed.startDamper(m_midiNote, m_sampleRate);
	}

	void render(double* output, size_t numSamples)
	{
		std::memset(output, 0, numSamples * sizeof(double));

		m_reed.render(output, numSamples);

		if (!m_noise.isDone())
			m_noise.render(output, numSamples);

		m_pickup.process(output, numSamples);

		const double gain = m_postPickupGain;
		for (size_t i = 0; i < numSamples; i++)
			output[i] *= gain;
	}

	bool isSilent() const
	{
		if (m_reed.isDamping() && m_reed.releaseSeconds(m_sampleRate) > 10.0)
			return true;
		return m_reed.isSilent(-80.0);
	}

	bool isActive() const { return m_active; }
	void setInactive() { m_active = false; }

private:
	ModalReed m_reed;
	Pickup m_pickup;
	AttackNoise m_noise;
	double m_postPickupGain = 1.0;
	double m_sampleRate = 44100.0;
	uint8_t m_midiNote = 60;
	bool m_active = false;
};

} // namespace openWurli
