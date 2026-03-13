/**
 * OpenWurli DSP — Deterministic per-note variation
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include "owTables.h"
#include <cstdint>
#include <array>

namespace openWurli
{

/// Simple deterministic hash: takes MIDI note + seed, returns 0.0..1.0
inline double hashF64(uint8_t midi, uint32_t seed)
{
	uint32_t h = 2166136261u;
	h ^= static_cast<uint32_t>(midi);
	h *= 16777619u;
	h ^= seed;
	h *= 16777619u;
	h ^= h >> 16;
	h *= 2654435769u;
	return static_cast<double>(h & 0x00FFFFFFu) / 16777216.0;
}

/// Frequency detuning factor: multiplier in range [1-0.00173, 1+0.00173] (±3 cents)
inline double freqDetune(uint8_t midi)
{
	const double r = hashF64(midi, 0xDEAD) * 2.0 - 1.0;
	return 1.0 + r * 0.00173;
}

/// Per-mode amplitude variation factors: multipliers ±8%
inline std::array<double, NUM_MODES> modeAmplitudeOffsets(uint8_t midi)
{
	std::array<double, NUM_MODES> out;
	for (int i = 0; i < NUM_MODES; i++)
	{
		const double r = hashF64(midi, 0xBEEF + static_cast<uint32_t>(i)) * 2.0 - 1.0;
		out[i] = 1.0 + r * 0.08;
	}
	return out;
}

} // namespace openWurli
