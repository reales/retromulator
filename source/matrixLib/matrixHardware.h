#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "m6809.h"
#include "pit8254.h"
#include "acia6850.h"

namespace matrixLib
{
	// Oberheim Matrix-1000 processor board
	class Hardware : public M6809Bus
	{
	public:
		static constexpr uint32_t CpuClock = 2000000;              // E clock, 8 MHz XTAL / 4
		static constexpr uint32_t VoiceTimerClock = 4000000;       // DCO timers count at twice the E clock
		static constexpr uint32_t MidiCyclesPerByte = CpuClock * 10 / 31250;
		static constexpr uint32_t VoiceCount = 6;
		static constexpr uint32_t CvChannelCount = 64;             // 8 multiplexers x 8 sample and holds

		Hardware();

		bool setFirmware(const std::vector<uint8_t>& _rom);
		void setPatchRom(const std::vector<uint8_t>& _rom);

		void reset();

		// runs the CPU until its cycle counter reaches _targetCycle
		void runUntil(uint64_t _targetCycle);

		uint64_t getCycles() const { return m_cpu.getCycles(); }

		void midiIn(uint8_t _byte) { m_acia.pushRx(_byte); }
		void midiOut(std::vector<uint8_t>& _dst) { m_acia.readTx(_dst); }
		size_t midiInPending() const { return m_acia.rxQueueSize(); }

		uint8_t read(uint16_t _addr) override;
		void write(uint16_t _addr, uint8_t _val) override;

		M6809& getCpu() { return m_cpu; }
		Acia6850& getAcia() { return m_acia; }

		// DCO1 and DCO2 period of a voice in voice timer ticks, 0 while stopped
		uint32_t getDcoPeriod(uint32_t _voice, uint32_t _dco);
		bool getRangeHigh(uint32_t _voice, uint32_t _dco) const { return m_latches[(_dco ? 0x80 : 0x00) + _voice] != 0; }
		Pit8254& getVoiceTimer(uint32_t _index) { return m_timers[_index]; }
		Pit8254& getSystemTimer() { return m_sysTimer; }

		std::vector<uint8_t>& getSram() { return m_sram; }

		// S&H mux 0-5 feed the voices, mux 6-7 hold balance and resonance; A8 shifts the DAC
		// by -2.5 V. Codes are raw >> 3, a normal write sets bit 15: 4096 is 0 V, 8191 is +5 V
		enum CvIndex : uint8_t { CvWsA, CvFreq, CvLog, CvPwA, CvLin, CvPwB, CvMod, CvWsB };

		static constexpr uint32_t cvChannel(uint16_t _addr) { return (_addr >> 1) & 0x3f; }
		static constexpr uint32_t voiceCv(uint32_t _voice, CvIndex _cv) { return _voice * 8 + _cv; }
		static constexpr uint32_t balanceCv(uint32_t _voice) { return 48 + _voice; }
		static constexpr uint32_t resonanceCv(uint32_t _voice) { return 54 + _voice; }

		// held voltage per channel
		const std::array<float, CvChannelCount>& getCvVolts() const { return m_cvVolts; }
		float getCvVolts(uint32_t _channel) const { return m_cvVolts[_channel]; }

		// last 13 bit DAC code per channel, for tracing
		uint16_t getCv(uint16_t _addr) const { return m_cv[cvChannel(_addr)]; }
		uint8_t getLatch(uint16_t _addr) const;

		std::function<void(uint16_t, uint8_t, bool)> onIoAccess;
		std::function<void(uint16_t, uint16_t)> onCvWrite;
		std::function<void(uint16_t)> onInstruction;     // debugging, called before each instruction

	private:
		uint8_t* sramPtr(uint16_t _addr);
		void updateIrq();

		M6809 m_cpu;
		Pit8254 m_timers[4];
		Pit8254 m_sysTimer;
		Acia6850 m_acia;

		std::vector<uint8_t> m_rom;          // 32 KB system EPROM at $8000
		std::vector<uint8_t> m_patchRom;     // 64 KB patch EPROM, 8 KB pages at $2000
		std::vector<uint8_t> m_sram;         // 32 KB battery-backed RAM

		uint8_t m_bankLatch = 0;             // $1D80
		std::array<uint8_t, 0x400> m_latches{};
		std::array<uint16_t, CvChannelCount> m_cv{};
		std::array<float, CvChannelCount> m_cvVolts{};
		uint8_t m_dacHigh = 0;
		uint8_t m_dacLow = 0;
		uint32_t m_cvChannel = 0;
		uint8_t m_cvRange = 0;
		bool m_cvInhibit = true;

		uint8_t m_switch0 = 0;
		uint8_t m_switch1 = 0;
	};
}
