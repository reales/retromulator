#pragma once

#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// Per voice control values after modulation, in musical units. The mapping to hardware codes
	// below is a stub until the static datasets (pitch, dco, vcf, vca) are published.
	struct VoiceControls
	{
		int32_t pitch[2] = {};     // 1/256 semitone, 0 = lowest DCO table entry (cvDco.h)
		int32_t shape[2] = {};     // 0-63 scale, 1/256 steps
		int32_t pw[2] = {};        // 1/512 parameter step (cvDco::pulseWidth)
		bool pulse[2] = {};        // pulse enabled (wave select)
		int32_t mix = 0;
		// cvFilterAmp: vcf, fm, vca1, vca2 in u16 (1/16 DAC step), res as DAC code
		int32_t vcf = 0;
		int32_t res = 0;
		int32_t fm = 0;
		int32_t vca1 = 0;
		int32_t vca2 = 0;
		bool vcfFast = false;
	};

	struct DcoTimer
	{
		uint8_t mode = 3;
		uint16_t count = 0;
		bool rangeHigh = false;
	};

	struct GlobalControls
	{
		uint8_t wave1 = 0;         // patch wave enables
		uint8_t wave2 = 0;
		uint8_t sync = 0;
		bool click[2] = {};
		bool mute = false;
	};

	namespace cvMap
	{
		// 13-bit code (bit 12 slow) | range << 13, as in the write log
		uint32_t cvValue(uint32_t _voice, CvIndex _index, const VoiceControls& _c);
		uint32_t balanceValue(uint32_t _voice, const VoiceControls& _c);
		uint32_t resonanceValue(uint32_t _voice, const VoiceControls& _c);
		DcoTimer dcoTimer(uint32_t _voice, uint32_t _dco, int32_t _pitch, const GlobalControls& _g);
		uint8_t controlLatch(const GlobalControls& _g);
		bool noiseLatch(const GlobalControls& _g);

		// DAC range bits the refresh uses per channel (timing/, dco/, vcf/, vca/ write logs)
		uint8_t dacRange(uint32_t _channel, uint32_t _dac12);

		// latch at offset 135, written with the control and noise latches on a patch load
		constexpr uint16_t AuxLatch = 135;
		constexpr uint8_t AuxLatchValue = 0;
	}
}
