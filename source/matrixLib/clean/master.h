#pragma once

#include <array>
#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// 172 byte global parameter block (INTERFACE.md 5.4)
	struct Master
	{
		static constexpr size_t Size = 172;
		std::array<uint8_t, Size> data{};

		Master() { reinit(); }

		// cold boot block as read back by opcode 04 type 3 (sysex/)
		void reinit();
		// opcode 03 receive: every byte kept, bank lock keeps only its MSB
		void receive(const std::array<uint8_t, Size>& _data);

		uint8_t vibSpeed() const { return data[1]; }
		uint8_t vibSpeedModSource() const { return data[2] & 3; }
		uint8_t vibSpeedModAmount() const { return data[3]; }
		uint8_t vibWave() const { return data[4] & 7; }
		uint8_t vibAmp() const { return data[5]; }
		uint8_t vibAmpModSource() const { return data[6] & 3; }
		uint8_t vibAmpModAmount() const { return data[7]; }
		int tune() const { return static_cast<int8_t>(data[8]); }
		uint8_t channel() const { return data[11] & 0x0f; }
		bool omni() const { return data[12] & 1; }
		bool controllersEnabled() const { return data[13] & 1; }
		bool patchChangeEnabled() const { return data[14] & 1; }
		uint8_t pedal1Cc() const { return data[17] & 0x7f; }
		uint8_t pedal2Cc() const { return data[18] & 0x7f; }
		uint8_t lever2Cc() const { return data[19] & 0x7f; }
		uint8_t lever3Cc() const { return data[20] & 0x7f; }
		bool echo() const { return data[32] & 1; }
		int transpose() const { return static_cast<int8_t>(data[34]); }
		bool monoMode() const { return data[35] & 1; }
		uint8_t bendRange() const { return data[164]; }
		bool bankLock() const { return (data[165] & 0x80) != 0; }
		bool groupMode() const { return (data[168] & 0x80) != 0; }
		bool unison() const { return data[169] != 0; }
		bool volumeInvert() const { return data[170] != 0; }
		bool memoryProtect() const { return data[171] != 0; }

		void setBankLock(bool _on) { data[165] = _on ? 0x80 : 0; }
	};
}
