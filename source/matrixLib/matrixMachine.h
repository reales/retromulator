#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "matrixHardware.h"
#include "cem3396.h"
#include "halfband2x.h"

namespace matrixLib
{
	// Processor board and voice board, run in lockstep one audio sample at a time
	class Machine
	{
	public:
		Machine();

		bool setFirmware(const std::vector<uint8_t>& _rom) { return m_hw.setFirmware(_rom); }
		void setPatchRom(const std::vector<uint8_t>& _rom) { m_hw.setPatchRom(_rom); }

		void setSamplerate(float _rate);
		float getSamplerate() const { return m_rate; }

		void reset();

		// renders _count mono samples, the CPU runs alongside
		void render(float* _out, size_t _count);

		Hardware& getHardware() { return m_hw; }

		std::function<void(float)> onComparatorSample;   // debugging

		// true while $1D00 bit 1 mutes the output and routes the voice sum to the ACIA CTS pin
		bool isCalibrating() const { return (m_hw.getLatch(0x1d00) & 0x02) != 0; }

		static constexpr uint32_t Oversampling = 2;

	private:
		void buildControls(uint32_t _voice, Cem3396::Controls& _c);
		void buildDischarges(uint32_t _voice, double _from, double _to, Cem3396::Discharges& _d0, Cem3396::Discharges& _d1);
		float noise();

		Hardware m_hw;
		std::array<Cem3396, Hardware::VoiceCount> m_voices;
		Halfband2x m_decimator;

		float m_rate = 48000.0f;
		double m_cyclesPerSample = Hardware::CpuClock / 48000.0;
		double m_cycleAcc = 0.0;
		uint64_t m_sampleCycle = 0;

		static constexpr double EdgeMargin = 2.0;        // one CPU cycle in timer ticks
		double m_edgeTick = 0.0;

		float m_dcIn = 0.0f;
		float m_dcOut = 0.0f;
		float m_dcCoeff = 0.9995f;
		float m_cmpIn = 0.0f;
		float m_cmpOut = 0.0f;
		float m_cmpCoeff = 0.997f;
		float m_lastSum = 0.0f;
		bool m_comparator = false;

		uint32_t m_noiseState = 0x12345678;
	};
}
