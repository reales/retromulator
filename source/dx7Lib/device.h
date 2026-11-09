/**
 *  DX7 Device adapter for Retromulator
 *  The clean-room controller (clean/) drives the EGS/OPS hardware models
 *
 *  EGS/OPS models based on VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com (GPL v3)
**/

#pragma once

#include "../synthLib/device.h"
#include "EGS.h"
#include "filter.h"
#include "clean/Core.h"

#include <memory>
#include <string>
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
	bool isValid() const override { return true; }

#if SYNTHLIB_DEMO_MODE == 0
	bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
	bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

	uint32_t getChannelCountIn() override { return 0; }
	uint32_t getChannelCountOut() override { return 2; }

	bool setDspClockPercent(uint32_t _percent) override;
	uint32_t getDspClockPercent() const override { return 100; }
	uint64_t getDspClockHz() const override;

	// n: 0 wheel, 1 breath, 2 foot, 3 aftertouch. Range 0-99, assign bits 1=pitch 2=amp 4=EG bias
	void setController(int n, int range, int assign);

	// function settings: a new instance starts from defaults(), setFunctions sends only the changed values
	static dx7clean::Global defaults();
	const dx7clean::Global* functions() const { return &m_core->global(); }
	void setFunctions(const dx7clean::Global& g);

	// session words: range | assign << 8 per controller, function values in the top bytes.
	// Words without them (older sessions) unpack to defaults()
	static void packFunctions(const dx7clean::Global& g, int32_t (&w)[4]);
	static dx7clean::Global unpackFunctions(const int32_t (&w)[4]);

	// back to the state of a fresh instance (controllers, pedals, functions, voices), voice memory kept.
	// Call with processing suspended
	void resetPerformance();

	// DX7 packed voice (VMEM, 128 bytes): name at offset 118
	static std::string extractPatchName(const uint8_t* voiceData, size_t size);

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	void busWrite(uint16_t addr, uint8_t value);
	void coreIn(uint8_t a, uint8_t b, uint8_t c);
	void boot();

	static constexpr float kNativeSamplerate = 49096.0f;
	static constexpr int kBufSize = 256;

	// EGS register block (0x3000-0x30FF), OPS mode/algorithm (0x2804/0x2805), volume DAC (0x280A)
	uint8_t m_egsRegs[256] = {};
	EGS m_egs{m_egsRegs};
	uint8_t m_opsMode = 0;
	uint8_t m_opsAlg = 0;
	uint8_t m_dac = 7;
	LP1 m_dacFilter;

	std::unique_ptr<dx7clean::Core> m_core;

	// 24 controller cycles make one sample; the remainder of a block is carried over
	float m_pending[kBufSize + 4] = {0};
	int m_pendingCnt = 0;

	float m_midiExpression = 1.0f;
};

} // namespace dx7Emu
