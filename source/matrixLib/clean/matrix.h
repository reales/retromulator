#pragma once

#include <array>
#include <cstdint>

#include "patch.h"
#include "types.h"

namespace matrixClean
{
	// Modulation matrix, fixed routes and destination accumulators (measurements/matrix, ctrl).
	//
	// Sources enter as a level s = Mod >> 6: unipolar 0..255, bipolar -128..127 (lever 1).
	// A route of amount a adds sign * ((|s| * table[|a|]) >> 8), sign = sign(s) * sign(a).
	// CV destinations accumulate in 1/8 DAC code units, saturating at 0..32767 after every add:
	// base (patch parameter) first, then buses 0-9 in order, then the fixed routes.
	namespace matrix
	{
		constexpr int32_t AccMax = 32767;

		enum class Curve : uint8_t { Exp, Linear, Pitch, EnvAmp, Unit };

		int32_t amountTable(Curve _c, int _amount);
		int sourceLevel(Mod _m);
		int32_t route(int _s, int _amount, Curve _c);
		// the same with the source at full Mod precision (pitch destinations: matrix/ audio_env1_dco1)
		int32_t routeFine(Mod _m, int _amount, Curve _c);
		inline int32_t sat(const int32_t _acc, const int32_t _v)
		{
			const int32_t r = _acc + _v;
			return r < 0 ? 0 : (r > AccMax ? AccMax : r);
		}

		// controller levels as Mod (s = 2 * value)
		inline Mod controller(const uint8_t _v) { return static_cast<Mod>(_v) * 128; }
		inline Mod bend(const uint8_t _b8) { return (static_cast<Mod>(_b8) - 128) * 64; }
		Mod keyboard(uint8_t _note);
		constexpr Mod KeyGateOn = 255 * 64;

		// pitch bend in 1/256 semitone from the 8 bit bend value (bend >> 6)
		int32_t bendPitch(uint8_t _b8, int _rangeSemitones);

		// base codes of the patch parameters (1/8 code units)
		int32_t baseMix(uint8_t _p);
		int32_t baseFm(uint8_t _p);
		int32_t baseRes(uint8_t _p);
		int32_t baseVca1(uint8_t _p);
		int32_t basePw(uint8_t _p);
		int32_t baseShape(uint8_t _p);
		// VCF accumulator: 256 per semitone, VCF parameter p at p * 256 - VcfOffset
		constexpr int32_t VcfOffset = 3024;

		// final DAC codes from the accumulators
		inline int32_t code(const int32_t _acc) { return _acc >> 3; }
		inline int32_t pwCode(const int32_t _acc) { return (_acc * 31) >> 9; }
		inline int32_t vca2Code(const int32_t _acc, const int _volume) { return (_acc * _volume) >> 11; }

		// sum of the buses per destination, without saturation (time and rate destinations), in
		// 1/8 code units (Exp curve; Linear for the envelope amplitudes)
		struct BusSums
		{
			std::array<int32_t, static_cast<size_t>(Dest::Count)> v{};
		};

		using Sources = std::array<Mod, static_cast<size_t>(Source::Count)>;

		// adds the buses of _p that target _d to _acc in order, saturating
		int32_t applyBuses(const Patch& _p, const Sources& _src, Dest _d, int32_t _acc, Curve _c);
		int32_t busSum(const Patch& _p, const Sources& _src, Dest _d, Curve _c);

		// sources the patch makes the controller compute (envelopes, LFOs, ramps), taken at patch
		// load; opcode 06 edits do not change it (measurements/matrix edit_vs_patch)
		struct Active
		{
			bool env[3] = {true, true, true};
			bool lfo[2] = {true, true};
			bool ramp[2] = {true, true};
		};
		Active activeSources(const Patch& _p);
	}
}
