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

#include <atomic>
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

	// DX7 patch name from internal RAM (current voice)
	static std::string extractPatchName(const uint8_t* voiceData, size_t size);

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	void processMessage(Message msg);
	int fillBuffer(float* outBuffer, int maxSamples);
	void parseMIDI(const uint8_t* data, uint32_t size);
	bool parseMidiTx(uint32_t& size, uint8_t*& buffer);

	// DX7 hardware emulator
	ToSynth* m_toSynth = nullptr;
	ToGui* m_toGui = nullptr;
	App_ToSynth m_appToSynth;
	NullToGui m_nullToGui;
	DX7 m_dx7;

	// Audio generation state
	static constexpr float kNativeSamplerate = 49096.0f;
	static constexpr int kBufSize = 256;
	double m_cpuCyclesPerSample = 0.0;
	double m_cycCount = 0.0;
	float m_internalBuffer[kBufSize * 2] = {0};
	float m_discardBuffer[kBufSize] = {0};
	int m_discardCnt = 0;

	// Volume
	float m_volume = 1.0f;
	float m_midiExpression = 0.0f;

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
