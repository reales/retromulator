/**
 *  DX7 Device adapter for Retromulator
 *  Wraps the VDX7 emulation engine as a synthLib::Device
 *
 *  Based on VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com (original VDX7 code, GPL v3)
**/

#pragma once

#include "../synthLib/device.h"
#include "dx7.h"
#include "Message.h"
#include "clean/Core.h"

#include <atomic>
#include <memory>
#include <cmath>
#include <vector>
#include <cstdint>

namespace dx7Emu
{

class Device : public synthLib::Device
{
public:
	Device(const synthLib::DeviceCreateParams& _params);
	~Device() override;

	float getSamplerate() const override;
	bool isValid() const override;

#if SYNTHLIB_DEMO_MODE == 0
	bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
	bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

	uint32_t getChannelCountIn() override { return 0; }
	uint32_t getChannelCountOut() override { return 2; }

	bool setDspClockPercent(uint32_t _percent) override;
	uint32_t getDspClockPercent() const override { return 100; }
	uint64_t getDspClockHz() const override;

	void setController(int n, int range, int assign);

	// function settings: a new instance starts from defaults(), setFunctions sends only the changed values
	static dx7clean::Global defaults();
	const dx7clean::Global* functions() const { return m_clean ? &m_clean->global() : nullptr; }
	void setFunctions(const dx7clean::Global& g);

	// session words: range | assign << 8 per controller, function values in the top bytes.
	// Words without them (older sessions) unpack to defaults()
	static void packFunctions(const dx7clean::Global& g, int32_t (&w)[4]);
	static dx7clean::Global unpackFunctions(const int32_t (&w)[4]);

	// back to the state of a fresh instance (controllers, pedals, functions, voices), voice memory kept.
	// Call with processing suspended
	void resetPerformance();

	// true when running the firmware-free controller instead of the HD6303 firmware
	bool isClean() const { return m_clean != nullptr; }

	// DX7 patch name from internal RAM (current voice)
	static std::string extractPatchName(const uint8_t* voiceData, size_t size);

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	void processMessage(Message msg);
	void parseMIDI(const uint8_t* data, uint32_t size);
	bool parseMidiTx(uint32_t& size, uint8_t*& buffer);
	void processAudioClean(const synthLib::TAudioOutputs& _outputs, size_t _samples);
	void sendMidiClean(const synthLib::SMidiEvent& _ev);
	void cleanIn(uint8_t a, uint8_t b, uint8_t c);
	void bootClean();

	// DX7 hardware emulator
	ToSynth* m_toSynth = nullptr;
	ToGui* m_toGui = nullptr;
	App_ToSynth m_appToSynth;
	NullToGui m_nullToGui;
	DX7 m_dx7;
	std::unique_ptr<dx7clean::Core> m_clean;

	// Audio generation state
	static constexpr float kNativeSamplerate = 49096.0f;
	static constexpr int kBufSize = 256;
	// One CPU instruction yields at most one sample, so the overshoot past a block is carried over
	float m_pending[kBufSize + 4] = {0};
	int m_pendingCnt = 0;

	// Volume
	float m_volume = 1.0f;
	float m_midiExpression = 1.0f;

	// MIDI velocity curve
	uint8_t m_midiVelocity[128] = {0};
	void setMidiVelocity(float c);

	// MIDI TX parser state
	enum MidiTxState { Ctrl, Data1, Data2, Sysex };
	MidiTxState m_txState = Ctrl;
	static const int kMaxSysex = 4104;
	uint8_t m_midiBuf[kMaxSysex];
	uint32_t m_midiTxSize = 0;

	// Collected MIDI output events
	std::vector<synthLib::SMidiEvent> m_midiOut;

	// Factory voices data (kept alive for setBank)
	std::vector<uint8_t> m_voicesData;

	// Shutdown flag to prevent audio processing during destruction
	std::atomic<bool> m_shutdown{false};
};

} // namespace dx7Emu
