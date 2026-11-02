#pragma once

#include <cstdint>
#include <vector>

namespace matrixClean
{
	struct MidiMessage
	{
		uint8_t status = 0;
		uint8_t data1 = 0;
		uint8_t data2 = 0;
		std::vector<uint8_t> sysex;   // complete F0..F7 when status == 0xf0
	};

	// serial MIDI byte parser with running status; real time bytes pass through
	class MidiIn
	{
	public:
		// longest message the unit takes is the opcode 03 master block (351 bytes); longer SysEx
		// (sample dumps, other devices) is dropped up to its F7 and never delivered
		static constexpr size_t MaxSysex = 512;

		MidiIn() { m_sysex.reserve(MaxSysex); }
		void reset();
		// returns true when _out holds a complete message. Reuse _out: its SysEx buffer keeps its
		// capacity, so the parser does not allocate once both are sized
		bool push(uint8_t _byte, MidiMessage& _out);

	private:
		static uint32_t dataCount(uint8_t _status);
		static void begin(MidiMessage& _out, uint8_t _status);

		uint8_t m_status = 0;
		uint8_t m_data[2] = {};
		uint32_t m_count = 0;
		bool m_inSysex = false;
		bool m_sysexOverflow = false;
		std::vector<uint8_t> m_sysex;
	};
}
