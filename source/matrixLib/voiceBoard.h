#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pit8254.h"
#include "cem3396.h"
#include "halfband2x.h"

namespace matrixClean
{
	// Matrix-1000 voice board without a CPU: CV DAC with 64 sample and holds, four 8254 DCO
	// timers, range, noise and control latches, six CEM3396 voices, output stage.
	// Writes are queued with a time in microseconds from reset() and take effect when render()
	// reaches them. Times are on the 2 MHz bus clock, 0.5 us resolution.
	class VoiceBoard
	{
	public:
		static constexpr uint32_t VoiceCount = 6;
		static constexpr uint32_t CvChannelCount = 64;
		static constexpr uint32_t BusClock = 2000000;          // timers count this clock
		static constexpr uint32_t Oversampling = 2;

		enum CvIndex : uint8_t { CvWsA, CvFreq, CvLog, CvPwA, CvLin, CvPwB, CvMod, CvWsB };
		static constexpr uint32_t voiceCv(uint32_t _voice, CvIndex _cv) { return _voice * 8 + _cv; }
		static constexpr uint32_t balanceCv(uint32_t _voice) { return 48 + _voice; }
		static constexpr uint32_t resonanceCv(uint32_t _voice) { return 54 + _voice; }

		// Range bits: bit 1 = A8 (bipolar, -2.97 to +2.94 V), bit 0 = A7 (-0.30 V)
		static constexpr uint8_t RangeA7 = 1;
		static constexpr uint8_t RangeA8 = 2;
		static constexpr uint16_t CvSlow = 0x1000;             // code bit 12: hold capacitor only

		// Control latch bits
		static constexpr uint8_t CtlWaveMask = 0x30;
		static constexpr uint8_t CtlSync1Off = 0x08;
		static constexpr uint8_t CtlSync2Off = 0x04;
		static constexpr uint8_t CtlMute = 0x02;

		enum class Op : uint8_t
		{
			Cv,          // a = channel, b = range, value = 13 bit code
			CvInhibit,   // value != 0: multiplexer off, no channel tracks the DAC
			Timer,       // a = chip (0-3), b = register (0-3), value = byte
			DcoMode,     // a = voice, b = dco, value = mode: control word, LSB then MSB access
			DcoCount,    // a = voice, b = dco, value = count, written per the counter's access mode
			Range,       // a = voice, b = dco, value != 0: high range
			Noise,       // value != 0: converter B of every voice takes the noise source
			Control,     // value = control latch byte
			Idle,        // value != 0: voices only run while the output is muted
			Latch        // a = latch offset, value = byte: other latches, no effect on the sound
		};

		struct Event
		{
			uint64_t tick = 0;     // bus clock ticks from reset
			Op op = Op::Cv;
			uint8_t a = 0;
			uint8_t b = 0;
			uint32_t value = 0;
		};

		VoiceBoard();

		void setSamplerate(float _rate);
		float getSamplerate() const { return m_rate; }

		// Analog spread (Clean/clean/analog.h). Off: the nominal voice model, today's samples.
		// On: per voice chip parameters, timing Rt*Ct per converter and range, summing gain
		using ChipParams = matrixLib::Cem3396::Params;
		static constexpr float RtCtLow = 1.517e-3f;      // 3.3 nF with 510k
		static constexpr float RtCtHigh = 1.0934e-4f;    // high range adds 36k in parallel
		void setSpread(bool _on) { m_spread = _on; }
		bool getSpread() const { return m_spread; }
		void setChipParams(uint32_t _voice, const ChipParams& _p) { m_voices[_voice].setParams(_p); m_bank.setParams(_voice, _p); }
		void setRtCt(uint32_t _voice, uint32_t _dco, float _low, float _high) { m_rtCt[_voice * 2 + _dco] = {_low, _high}; }
		void setVoiceGain(uint32_t _voice, float _gain) { m_voiceGain[_voice] = _gain; }

		// Render path. Off (default unless built with MATRIX_VOICE_REFERENCE=1): the six voices in
		// SIMD lanes (Cem3396Bank) and a cached DCO edge scan. On: the scalar per voice model.
		// Both give identical samples. Set it before reset(), the two paths keep separate state
		void setReference(bool _on) { m_reference = _on; }
		bool getReference() const { return m_reference; }

		// clears the hardware, the event queue and the clock
		void reset();

		// CV: channel 0-63, _code bits 11-0 = DAC, bit 12 = slow, _range = RangeA7 | RangeA8
		void writeCv(double _us, uint32_t _channel, uint16_t _code, uint8_t _range);
		void writeCv(double _us, uint32_t _channel, uint16_t _dac, bool _slow, uint8_t _range);
		void inhibitCv(double _us);

		// DCO timers. _mode 0-5; writeDcoTimer = control word then count
		void writeDcoTimer(double _us, uint32_t _voice, uint32_t _dco, uint8_t _mode, uint16_t _count);
		void writeDcoMode(double _us, uint32_t _voice, uint32_t _dco, uint8_t _mode);
		void writeDcoCount(double _us, uint32_t _voice, uint32_t _dco, uint16_t _count);
		// raw 8254 access. Chip 0: DCO1 voices 0-2, 1: DCO1 voices 3-5, 2: DCO2 voices 0-2,
		// 3: DCO2 voices 3-5; counter = voice % 3; register 3 = control word
		void writeTimer(double _us, uint32_t _chip, uint32_t _reg, uint8_t _value);

		void writeRangeLatch(double _us, uint32_t _voice, uint32_t _dco, bool _high);
		void writeNoiseLatch(double _us, bool _on);
		void writeControlLatch(double _us, uint8_t _value);
		void setIdle(double _us, bool _idle);

		void push(const Event& _e);

		// mono output, one sample per call slot at the current samplerate
		void render(float* _out, size_t _count);

		uint64_t getTick() const { return m_sampleTick; }
		double getTimeUs() const { return static_cast<double>(m_sampleTick) * 1e6 / BusClock; }
		size_t pendingEvents() const { return m_events.size() - m_next; }

		// state after the events applied so far
		float getCvVolts(uint32_t _channel) const { return m_cvVolts[_channel]; }
		uint16_t getCvCode(uint32_t _channel) const { return m_cvCode[_channel]; }
		uint32_t getDcoPeriod(uint32_t _voice, uint32_t _dco);      // timer ticks, 0 if stopped
		uint8_t getControlLatch() const { return m_control; }
		bool isMuted() const { return (m_control & CtlMute) != 0; }

		// Write log CSV: time_us,target,channel,value (see README.md). Appends to _events
		static bool loadLog(const std::string& _path, std::vector<Event>& _events, std::string* _error = nullptr);
		static bool parseLogLine(const std::string& _line, Event& _e, std::string* _error = nullptr);
		// queues a whole log, returns false on a parse error
		bool replayLog(const std::string& _path, std::string* _error = nullptr);

		static uint64_t usToTick(double _us);

	private:
		struct DcoEvents
		{
			std::vector<uint64_t> a;
			std::vector<uint64_t> b;
		};
		struct DcoScan
		{
			DcoEvents events;
			uint64_t scannedA = 0;
			uint64_t scannedB = 0;
			bool levelA = true;
			bool levelB = true;
			// fast path: no OUT edge before these ticks (0 = unknown); timer writes and gates clear them
			uint64_t quietA = 0;
			uint64_t quietB = 0;
		};

		void apply(const Event& _e);
		void applyUntil(uint64_t _tick);
		void cvWrite(uint64_t _tick, uint32_t _channel, uint16_t _code, uint8_t _range);
		void timerWrite(uint64_t _tick, uint32_t _chip, uint32_t _reg, uint8_t _value);
		void advanceCv(float _seconds);

		void scanDcoA(uint32_t _voice, uint64_t _until);
		void scanDco(uint32_t _voice, uint64_t _until);
		void scanChip(uint32_t _chip, uint64_t _until);
		void pruneDischarges(std::vector<uint64_t>& _ticks) const;

		void buildControls(uint32_t _voice, matrixLib::Cem3396::Controls& _c);
		void renderVoices(double _edgeFrom, double _edgeTo, float& _sum0, float& _sum1);
		void renderVoicesReference(double _edgeFrom, double _edgeTo, float& _sum0, float& _sum1);
		void buildDischarges(uint32_t _voice, double _from, double _to, matrixLib::Cem3396::Discharges& _d0, matrixLib::Cem3396::Discharges& _d1);
		float noise();

		matrixLib::Pit8254 m_timers[4];
		std::array<DcoScan, VoiceCount> m_dco;
		std::array<bool, VoiceCount * 2> m_rangeHigh{};
		bool m_noise = false;
		uint8_t m_control = 0;
		bool m_idle = false;

		std::array<uint16_t, CvChannelCount> m_cvCode{};
		std::array<float, CvChannelCount> m_cvVolts{};
		std::array<float, CvChannelCount> m_cvHold{};
		float m_dacVolts = 0.0f;
		float m_cvStepSeconds = -1.0f;
		float m_cvStepMoved = 0.0f;
		bool m_dacFast = false;
		uint32_t m_cvChannel = 0;
		bool m_cvInhibit = true;

		std::array<matrixLib::Cem3396, VoiceCount> m_voices;
		matrixLib::Cem3396Bank m_bank;
		bool m_reference = MATRIX_VOICE_REFERENCE != 0;
		bool m_spread = false;
		std::array<std::array<float, 2>, VoiceCount * 2> m_rtCt;
		std::array<float, VoiceCount> m_voiceGain;
		matrixLib::Halfband2x m_decimator;

		std::vector<Event> m_events;
		size_t m_next = 0;
		uint64_t m_applied = 0;      // events before this tick have been applied

		float m_rate = 48000.0f;
		double m_ticksPerSample = BusClock / 48000.0;
		double m_tickAcc = 0.0;
		uint64_t m_sampleTick = 0;
		double m_edgeTick = -1.0;

		float m_dcIn = 0.0f;
		float m_dcOut = 0.0f;
		float m_dcCoeff = 0.9995f;
		uint32_t m_noiseState = 0x12345678;
	};
}
