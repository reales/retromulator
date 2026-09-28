/**
 * OpenWurli DSP — Per-note MLP v2 parameter corrections
 * Ported from Rust openwurli-dsp 0.9.0 mlp_correction.rs (GPL v3)
 *
 * Tiny neural network (2→16→16→11) run ONCE at note-on. Only the H2/H3
 * heads were trained; the others (and ds_correction) emit identity.
 */
#pragma once

#include "owMlpWeights.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace openWurli
{

static constexpr int MLP_N_FREQ = 5;
static constexpr int MLP_N_DECAY = 5;
static constexpr int MLP_N_TRAINED_HARMONICS = 2;
static constexpr double MLP_MIDI_MIN = 21.0;
static constexpr double MLP_MIDI_MAX = 108.0;
static constexpr double MLP_TRAIN_LO = 38.0;
static constexpr double MLP_TRAIN_HI = 86.0;
static constexpr double MLP_FADE_SEMITONES = 12.0;

struct MlpCorrections
{
	double freqOffsetsCents[MLP_N_FREQ] = {};
	double decayOffsets[MLP_N_DECAY] = {1.0, 1.0, 1.0, 1.0, 1.0};
	double dsCorrection = 1.0;

	static MlpCorrections identity()
	{
		return MlpCorrections{};
	}

	static MlpCorrections infer(uint8_t midiNote, double velocity)
	{
		const double midi = static_cast<double>(midiNote);

		double fade;
		if (midi < MLP_TRAIN_LO)
			fade = std::clamp((midi - (MLP_TRAIN_LO - MLP_FADE_SEMITONES)) / MLP_FADE_SEMITONES, 0.0, 1.0);
		else if (midi > MLP_TRAIN_HI)
			fade = std::clamp(((MLP_TRAIN_HI + MLP_FADE_SEMITONES) - midi) / MLP_FADE_SEMITONES, 0.0, 1.0);
		else
			fade = 1.0;

		if (fade <= 0.0)
			return identity();

		const double midiNorm = std::clamp((midi - MLP_MIDI_MIN) / (MLP_MIDI_MAX - MLP_MIDI_MIN), 0.0, 1.0);
		const double velNorm = std::clamp(velocity, 0.0, 1.0);
		const double input[2] = {midiNorm, velNorm};

		double h1[MLP_HIDDEN_SIZE];
		for (int i = 0; i < MLP_HIDDEN_SIZE; i++)
		{
			double sum = B1[i];
			for (int j = 0; j < 2; j++)
				sum += W1[i][j] * input[j];
			h1[i] = (sum > 0.0) ? sum : 0.0;
		}

		double h2[MLP_HIDDEN_SIZE];
		for (int i = 0; i < MLP_HIDDEN_SIZE; i++)
		{
			double sum = B2[i];
			for (int j = 0; j < MLP_HIDDEN_SIZE; j++)
				sum += W2[i][j] * h1[j];
			h2[i] = (sum > 0.0) ? sum : 0.0;
		}

		double raw[MLP_N_OUTPUTS];
		for (int i = 0; i < MLP_N_OUTPUTS; i++)
		{
			double sum = B3[i];
			for (int j = 0; j < MLP_HIDDEN_SIZE; j++)
				sum += W3[i][j] * h2[j];
			raw[i] = sum * TARGET_STDS[i] + TARGET_MEANS[i];
		}

		MlpCorrections c;
		for (int h = 0; h < std::min(MLP_N_FREQ, MLP_N_TRAINED_HARMONICS); h++)
			c.freqOffsetsCents[h] = std::clamp(raw[h] * fade, -100.0, 100.0);
		for (int h = 0; h < std::min(MLP_N_DECAY, MLP_N_TRAINED_HARMONICS); h++)
		{
			const double rawDecay = std::clamp(raw[MLP_N_FREQ + h], 0.3, 3.0);
			c.decayOffsets[h] = 1.0 + (rawDecay - 1.0) * fade;
		}
		c.dsCorrection = 1.0;

		return c;
	}
};

} // namespace openWurli
