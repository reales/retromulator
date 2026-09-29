#pragma once

#include "synthLib/device.h"

#include "matrixMachine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace matrixLib
{
	// Original firmware on an emulated 6809 board driving six CEM3396 models. romData = 32 KB system
	// EPROM followed by the optional 64 KB patch EPROM, homePath = calibration snapshot and ROM banks
	class Device : public synthLib::Device
	{
	public:
		explicit Device(const synthLib::DeviceCreateParams& _params);
		~Device() override;

		float getSamplerate() const override { return m_machine.getSamplerate(); }
		void getSupportedSamplerates(std::vector<float>& _dst) const override;
		bool setSamplerate(float _samplerate) override;
		bool isValid() const override { return m_valid; }

#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }
#endif

		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }

		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return Hardware::CpuClock; }

		// file names of the banks built from the patch EPROM, 2-9
		static std::string romBankFileName(uint32_t _bank);

		// LFO speed locked to the host tempo, division 0 = off. The nearest of the 64 firmware
		// speeds is sent after every tempo change and patch load
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

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		void boot();
		bool loadSnapshot();
		void saveSnapshot() const;
		void runSeconds(float _seconds);
		void selectFirmwareProgram();

		// asks the firmware for every patch of banks 2-9 and writes them as .syx files
		void createRomBanks();
		std::vector<uint8_t> requestPatch(uint32_t _number);

		void pushMidi(const synthLib::SMidiEvent& _ev);
		void sendLfoSync(bool _force);
		void parseMidiOut(uint8_t _byte);

		Machine m_machine;
		std::string m_homePath;
		bool m_valid = false;
		bool m_hasPatchRom = false;

		struct PendingEvent
		{
			uint32_t offset;
			synthLib::SMidiEvent event;
		};
		std::vector<PendingEvent> m_pending;
		std::vector<float> m_mono;

		std::vector<uint8_t> m_txBytes;
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
		int m_sentSpeed[2] = {-1, -1};
		int m_patchLfoSpeed[2] = {-1, -1};
	};
}
