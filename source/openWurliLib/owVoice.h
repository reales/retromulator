/**
 * OpenWurli DSP — Single voice: reed + hammer + pickup + decay
 * Ported from Rust openwurli-dsp 0.9.0 voice.rs (GPL v3)
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

		// Sigmoid → power-law velocity curve (hammer force, pre-pickup)
		const double velExp = velocityExponent(m_midiNote);
		const double velScale = std::pow(velocityScurve(velocity), velExp);
		for (auto& a : amplitudes)
			a *= velScale;

		const auto corrections = mlpEnabled ? MlpCorrections::infer(m_midiNote, velocity) : MlpCorrections::identity();

		auto correctedRatios = params.modeRatiosArr;
		for (int i = 0; i < std::min(5, NUM_MODES - 1); i++)
			correctedRatios[i + 1] *= std::pow(2.0, corrections.freqOffsetsCents[i] / 1200.0);

		auto correctedDecay = params.modeDecayRatesArr;
		for (int i = 0; i < std::min(5, NUM_MODES - 1); i++)
			correctedDecay[i + 1] /= corrections.decayOffsets[i];

		// The note's ff swing at the pickup (mm); the velocity curve scales the reed from there.
		const double swingMm = pickupSwingMm(m_midiNote);

		m_reed.init(detunedFundamental, correctedRatios, amplitudes, correctedDecay,
					onsetTime, velocity, sampleRate);
		m_airDragDbPerMm = airDragDbPerMm(m_midiNote);
		m_reed.setAirDrag(m_airDragDbPerMm, swingMm, sampleRate);

		m_pickup.init(sampleRate, m_midiNote, swingMm);
		m_noise.init(velocity, detunedFundamental, sampleRate, noiseSeed);

		m_postPickupGain = outputScale(m_midiNote, velocity);
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
	double m_airDragDbPerMm = 0.0;
	double m_sampleRate = 44100.0;
	uint8_t m_midiNote = 60;
	bool m_active = false;
};

} // namespace openWurli
