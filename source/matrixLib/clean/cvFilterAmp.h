#pragma once

#include <array>
#include <cstdint>

#include "cvMap.h"
#include "patch.h"
#include "types.h"

namespace matrixClean
{
	// VCF frequency, resonance, FM, VCA1 and VCA2 from the patch and the modulation sources.
	// Internal unit u16: 1/16 of a DAC step; for the VCF one step is 1/32 semitone and u16 0
	// is P21 12 with keyboard off. Sources are Mod with 8-bit source value s = Mod / 64.
	namespace cvFilterAmp
	{
		struct Buses
		{
			std::array<int32_t, 10> v{};
			uint8_t n = 0;
		};

		struct Inputs
		{
			uint32_t voice = 0;
			int32_t keyPitch = 0;          // 1/256 semitone (note * 256)
			int32_t portaPitch = 0;
			Mod env1 = 0, env2 = 0, env3 = 0;
			Mod pressure = 0;
			uint8_t velocity = 0;
			Mod bend = 0;                  // +-ModFull for the full 14-bit range
			uint8_t bend8 = 128;           // bend >> 6
			uint8_t bendRange = 2;
			int32_t fineTune = 0;          // RPN 1, 1/256 semitone
			Mod vibrato = 0;
			// matrix buses per destination in bus order, u16 each (matrix.h); added after the
			// base and before the fixed routes, the sum held in range after every add
			Buses busVcf, busRes, busFm, busVca1, busVca2;
			uint8_t volume = 255;          // 8 bit, 255 until the first CC7
			int vcfByEnv1 = 0;             // as latched at patch load
			int fmByEnv3 = 0;
		};

		// CC7 value (-1 = none since boot) and master byte 170 to the 8-bit volume
		uint8_t volume(int _cc7, uint8_t _invertByte);

		// fixed route amount curve, 0..65280 for 0..63 (amt/)
		int32_t amountCurve(int _amount);

		void compute(const Patch& _p, const Inputs& _in, VoiceControls& _c);

		uint32_t vcfValue(uint32_t _voice, const VoiceControls& _c);
		// analog spread: the same interpolation over a per voice table in the vcfCal unit
		uint32_t vcfValue(const int32_t (&_table)[9], const VoiceControls& _c);
		// measured 9 point VCF table per voice, 1/8 DAC code at u16 = 8192 k
		const int32_t (&vcfCal(uint32_t _voice))[9];
		uint32_t fmValue(const VoiceControls& _c);
		uint32_t vca1Value(const VoiceControls& _c);
		uint32_t vca2Value(const VoiceControls& _c);
		uint32_t resonanceValue(const VoiceControls& _c);
	}
}
