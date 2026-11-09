#pragma once

#include "synthLib/device.h"

#include "clean/analog.h"
#include "clean/core.h"
#include "voiceBoard.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace matrixLib
{
	// Firmware-free Matrix-1000, 18 voices: three stacked units, each a clean Core driving its
	// own voice board (the 6 voice Matrix-1000 as measured). Notes fill unit 0 first, then the
	// next; everything else goes to all of them. Units 1 to 5 sleep while silent. State =
	// unit 0's patch memory, master and edit buffer, which every unit shares
	class Device : public synthLib::Device
	{
	public:
		static constexpr uint32_t BoardCount = 6;

		explicit Device(const synthLib::DeviceCreateParams& _params);
		~Device() override;

		float getSamplerate() const override { return m_boards[0]->board.getSamplerate(); }
		void getSupportedSamplerates(std::vector<float>& _dst) const override;
		bool setSamplerate(float _samplerate) override;
		bool isValid() const override { return true; }

#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif
		// init patches and master everywhere, for a session that carries no core state
		void resetMemory();

		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }

		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return matrixClean::VoiceBoard::BusClock; }

		// Analog spread (clean/analog.h): per unit CEM3396 and part tolerances, calibrated like the
		// hardware. Off by default and for sessions saved without it. Amount 0..2: 0.5 tight,
		// 1 typical, 1.5 worn; the seed is unit 0's number, the other units derive theirs from
		// it. Saved with the state
		void setAnalogEnabled(bool _on);
		bool getAnalogEnabled() const;
		void setAnalogAmount(float _amount);
		float getAnalogAmount() const;
		void setAnalogSeed(uint32_t _seed);
		uint32_t getAnalogSeed() const;
		void setAnalogMode(matrixClean::analog::Thermal _mode);
		matrixClean::analog::Thermal getAnalogMode() const;
		void setAnalogResidual(matrixClean::analog::Residual _residual);
		matrixClean::analog::Residual getAnalogResidual() const;
		static uint32_t randomAnalogSeed();

		// LFO speed locked to the host tempo, division 0 = off. The nearest of the 64 LFO
		// speeds overrides the patch speed in the core, the patch data keeps its own
		static uint32_t getLfoDivisionCount();
		static const char* getLfoDivisionName(uint32_t _division);
		void setLfoSync(uint32_t _lfo, uint32_t _division);
		uint32_t getLfoSync(uint32_t _lfo) const { return m_lfoSync[_lfo & 1].load(); }
		void setHostBpm(float _bpm) { m_hostBpm.store(_bpm); }
		// host playhead tempo, else incoming MIDI clock, else the last SysEx tempo, else 120
		// (F0 7D 54 t1 t2 t3 F7: microseconds per quarter note, three 7 bit bytes, high first)
		float getHostBpm() const;
		bool hasTempoSource() const;
		// false when no speed step lands within a few percent of the division at this tempo
		bool isLfoDivisionReachable(uint32_t _division) const;

		// A/B: units never sleep
		void setSleepEnabled(bool _on) { m_sleepEnabled = _on; }
		bool isAsleep(uint32_t _board) const { return m_boards[_board % BoardCount]->asleep; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		struct Board
		{
			matrixClean::Core core;
			matrixClean::VoiceBoard board;
			matrixClean::analog::Unit analog;
			double lineFree = 0.0;
			std::vector<float> mono;
			bool asleep = false;
			uint32_t quietSamples = 0;
			uint32_t stillSamples = 0;
			float sleepGain = 1.0f;
			std::array<int32_t, matrixClean::VoiceCount * 3> envSnap{};
		};

		void pushMidi(const synthLib::SMidiEvent& _ev, double _timeUs);
		void pushBytes(Board& _b, const uint8_t* _bytes, size_t _count, double _timeUs);
		void broadcast(const uint8_t* _bytes, size_t _count, double _timeUs);
		int routeNoteOn(uint8_t _channel, uint8_t _note);
		void noteOff(uint8_t _channel, uint8_t _note);
		void clearNotes(int _channel);
		void wake(uint32_t _board, double _timeUs);
		void updateSleep(uint32_t _board, size_t _samples);
		uint32_t envelopeChanges(uint32_t _board);
		void flushWrites(uint32_t _board);
		void deriveAnalog();
		void applyAnalog();
		void applyLfoSync();
		void parseMidiOut(uint8_t _byte);

		mutable std::mutex m_mutex;
		std::mutex m_stateMutex;         // getState buffers, never taken by the audio thread
		std::vector<uint8_t> m_stateBuf;
		matrixClean::analog::Unit m_analogCopy;
		std::vector<matrixClean::HwWrite> m_writeBuf;
		std::vector<uint8_t> m_outBuf;
		std::array<std::unique_ptr<Board>, BoardCount> m_boards;

		// note router: the unit each held note went to, -1 = none
		std::array<std::array<int8_t, 128>, 16> m_noteBoard;
		std::array<std::array<uint32_t, 128>, 16> m_noteAge{};
		std::array<std::array<bool, 128>, 16> m_noteCounted{};
		std::array<uint8_t, BoardCount> m_boardNotes{};
		uint32_t m_noteClock = 0;
		bool m_sleepEnabled = true;

		// output peak limiter, unity until the sum passes what one unit can reach
		float m_limitEnv = 0.0f;
		float m_limitRelease = 0.0f;

		struct PendingEvent
		{
			uint32_t offset;
			synthLib::SMidiEvent event;
		};
		std::vector<PendingEvent> m_pending;

		std::vector<uint8_t> m_txSysex;
		std::vector<uint8_t> m_txShort;
		uint8_t m_txRunningStatus = 0;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		std::atomic<uint32_t> m_lfoSync[2] = {0, 0};
		std::atomic<float> m_hostBpm{0.0f};
		std::atomic<float> m_clockBpm{0.0f};
		std::atomic<float> m_sysexBpm{0.0f};
		uint64_t m_sampleTime = 0;
		uint64_t m_lastClock = 0;
		uint64_t m_clockBeatStart = 0;
		uint32_t m_clockCount = 0;
		std::atomic<bool> m_lfoSyncDirty{false};
		float m_sentBpm = 0.0f;
	};
}
