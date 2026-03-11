/**
 *  DX7 Device adapter for Retromulator
 *  Wraps the VDX7 emulation engine as a synthLib::Device
 *
 *  Based on VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com (original VDX7 code, GPL v3)
**/

#include "device.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace dx7Emu
{

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
	, m_dx7(m_toSynth, m_toGui)
{
	// Wire up communication interfaces
	m_toSynth = &m_appToSynth;
	m_toGui = &m_nullToGui;

	// Default velocity curve (0.4 = convex, matches VDX7 default)
	setMidiVelocity(0.4f);

	// CPU cycles per sample at native rate
	// DX7 master clock: 9.4265 MHz, CPU divides by 2, then by 4
	// So CPU clock = 9.4265e6 / 2 / 4 = 1,178,312.5 Hz
	// Native sample rate = 49,096.354 Hz
	// Cycles per sample = 1,178,312.5 / 49,096.354 ≈ 24.0
	m_cpuCyclesPerSample = (9.4265e6 / 2.0 / 4.0) / static_cast<double>(kNativeSamplerate);

	// Load firmware ROM
	if(!_params.romData.empty())
	{
		m_dx7.loadFirmware(_params.romData.data(), _params.romData.size());
	}

	// Set midi filter for the volume DAC smoother
	m_dx7.midiFilter.set_f(10.6f / kNativeSamplerate);

	// Start the emulated CPU
	if(m_dx7.isRomLoaded())
	{
		m_dx7.start();

		// Run boot cycles to get the firmware initialized
		for(int i = 0; i < 3000000; i++)
			m_dx7.run();
	}
}

Device::~Device()
{
	m_shutdown.store(true);
}

float Device::getSamplerate() const
{
	return kNativeSamplerate;
}

bool Device::isValid() const
{
	return m_dx7.isRomLoaded();
}

bool Device::setDspClockPercent(uint32_t)
{
	return false; // fixed clock
}

uint64_t Device::getDspClockHz() const
{
	return 9426500; // 9.4265 MHz master clock
}

#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if(_type == synthLib::StateTypeGlobal)
	{
		// Save the 6K battery-backed RAM
		m_dx7.saveRAM(_state);
		return true;
	}
	return false;
}

bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if(_type == synthLib::StateTypeGlobal && _state.size() == 6144)
	{
		m_dx7.restoreRAM(_state);
		return true;
	}
	return false;
}
#endif


void Device::processAudio(const synthLib::TAudioInputs& /*_inputs*/, const synthLib::TAudioOutputs& _outputs, size_t _samples)
{
	if(!m_dx7.isRomLoaded() || m_shutdown.load()) return;

	auto* outL = _outputs[0];
	auto* outR = _outputs[1];

	size_t remaining = _samples;
	size_t outPos = 0;

	while(remaining > 0)
	{
		const int toGenerate = static_cast<int>(std::min(remaining, static_cast<size_t>(kBufSize)));
		const int generated = fillBuffer(m_internalBuffer, toGenerate);

		// MIDI volume filtering and expression
		const float mv = std::min(1.0f, m_dx7.midiVolTab[m_dx7.midiVolume] + m_midiExpression + 1e-18f);

		const int toCopy = std::min(generated, toGenerate);
		for(int i = 0; i < toCopy && outPos < _samples; i++, outPos++)
		{
			const float sample = m_internalBuffer[i] * m_volume * m_dx7.midiFilter.operate(mv);
			outL[outPos] = sample;
			outR[outPos] = sample; // mono to stereo
		}

		remaining -= static_cast<size_t>(toCopy);
		if(generated == 0) break; // safety
	}

	// Zero remaining if we underran
	for(; outPos < _samples; outPos++)
	{
		outL[outPos] = 0.0f;
		outR[outPos] = 0.0f;
	}

}

int Device::fillBuffer(float* outBuffer, int maxSamples)
{
	m_cycCount += m_cpuCyclesPerSample * maxSamples;
	int outCnt = 0;
	m_discardCnt = 0;
	Message msg;

	while(m_cycCount > 0 && !m_shutdown.load(std::memory_order_relaxed))
	{
		// Process messages from ToSynth queue
		if(!m_dx7.haveMsg)
			if(m_toSynth->pop(msg)) processMessage(msg);

		// Run one CPU instruction
		m_dx7.run();

		const int cycles = m_dx7.inst->cycles;

		// Clock the EGS and OPS — always clock to keep CPU/EGS in sync,
		// but use a discard buffer once we have enough output samples
		if(outCnt < maxSamples)
			m_dx7.egs.clock(outBuffer, outCnt, 4 * cycles);
		else
			m_dx7.egs.clock(m_discardBuffer, m_discardCnt, 4 * cycles);

		// Safety: ensure we always make forward progress
		m_cycCount -= (cycles > 0) ? cycles : 1;
	}

	return outCnt;
}

void Device::processMessage(Message msg)
{
	switch(Message::CtrlID(msg.byte1))
	{
		case Message::CtrlID::volume:
			m_volume = static_cast<float>(std::pow(2.0, msg.byte2 / 127.0) - 1.0);
			break;

		case Message::CtrlID::sustain:
			m_dx7.sustain(msg.byte2 != 0);
			break;

		case Message::CtrlID::porta:
			m_dx7.porta(msg.byte2 != 0);
			break;

		case Message::CtrlID::cartridge:
			m_dx7.cartPresent(msg.byte2 != 0);
			break;

		case Message::CtrlID::cartridge_num:
			m_dx7.setBank(msg.byte2, true);
			break;

		case Message::CtrlID::protect:
			m_dx7.cartWriteProtect(msg.byte2 != 0);
			break;

		default:
			// Key events: velocity is inverted for DX7 internal keyboard
			if(msg.byte1 > 158 && msg.byte2 != 0)
				msg.byte2 = 128 - msg.byte2;
			m_dx7.msg = msg;
			m_dx7.haveMsg = true;
			break;
	}
}

bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
{
	if(!_ev.sysex.empty())
	{
		// DX7 32-voice bulk dump: write directly into internal voice RAM
		// This bypasses the firmware's "MEMORY PROTECTED" rejection
		if(_ev.sysex.size() == 4104 &&
		   _ev.sysex[0] == 0xF0 && _ev.sysex[1] == 0x43 &&
		   _ev.sysex[3] == 0x09 && _ev.sysex[4] == 0x20 && _ev.sysex[5] == 0x00)
		{
			// Copy 4096 bytes of voice data into internal voice RAM at 0x1000
			memcpy(m_dx7.memory + 0x1000, _ev.sysex.data() + 6, 4096);
			return true;
		}

		// Other sysex: send through serial interface
		for(const auto byte : _ev.sysex)
			m_dx7.midiSerialRx.write(byte);
		return true;
	}

	// System real-time messages (F8-FF): ignore, they are 1-byte and the DX7
	// firmware doesn't use them. MIDI Clock (F8) floods from DAWs would
	// otherwise overwhelm the serial RX buffer.
	if(_ev.a >= 0xF8)
		return true;

	// Standard MIDI message
	const uint8_t buf[3] = { _ev.a, _ev.b, _ev.c };
	uint32_t len = 3;

	// 2-byte messages
	if((_ev.a & 0xF0) == 0xC0 || (_ev.a & 0xF0) == 0xD0)
		len = 2;

	parseMIDI(buf, len);
	return true;
}

void Device::parseMIDI(const uint8_t* data, uint32_t size)
{
	if(size < 1 || size > 3) return;

	const uint8_t status = data[0] & 0xF0;

	switch(status)
	{
	case 0x80: // Note Off
		if(data[1] >= 34)
			m_toSynth->key_off(data[1] - 34);
		return;

	case 0x90: // Note On
		if(data[1] >= 34)
		{
			if(data[2] == 0)
				m_toSynth->key_off(data[1] - 34);
			else
				m_toSynth->key_on(data[1] - 34, m_midiVelocity[data[2]]);
		}
		return;

	case 0xB0: // Control Change
		switch(data[1])
		{
		case 0: return; // Bank MSB - eat to avoid firmware bug
		case 1: m_toSynth->analog(Message::CtrlID::modulate, data[2]); return;
		case 2: m_toSynth->analog(Message::CtrlID::breath, data[2]); return;
		case 4: m_toSynth->analog(Message::CtrlID::foot, data[2]); return;
		case 6: m_toSynth->analog(Message::CtrlID::data, data[2]); return;
		case 7: // Volume - forward to serial for DX7's 3-bit DAC
			break;
		case 11: // Expression
			m_midiExpression = data[2] / 127.0f;
			return;
		case 32: // Bank change LSB: load factory ROM cartridge
			m_dx7.setBank(data[2] % 8, true);
			return;
		case 64: // Sustain
			m_toSynth->analog(Message::CtrlID::sustain, data[2]);
			return;
		case 65: // Portamento
			m_toSynth->analog(Message::CtrlID::porta, data[2]);
			return;
		case 123: // All notes off
			for(int i = 0; i < 61; i++)
				m_toSynth->key_off(static_cast<uint8_t>(i));
			break; // also forward to serial
		default:
			break; // forward to serial
		}
		break;

	case 0xD0: // Channel pressure
		m_toSynth->analog(Message::CtrlID::aftertouch, data[1]);
		return;

	case 0xE0: // Pitch bend (MSB only)
		m_toSynth->analog(Message::CtrlID::pitchbend, data[2]);
		return;

	default:
		break;
	}

	// Forward unhandled messages to DX7 serial interface
	for(uint32_t i = 0; i < size; i++)
		m_dx7.midiSerialRx.write(data[i]);
}

void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
{
	// Parse MIDI stream from DX7 serial TX
	uint32_t size;
	uint8_t* buf;

	while(parseMidiTx(size, buf))
	{
		synthLib::SMidiEvent ev(synthLib::MidiEventSource::Device);

		if(buf[0] == 0xF0)
		{
			// Sysex
			ev.sysex.assign(buf, buf + size);
		}
		else
		{
			ev.a = buf[0];
			ev.b = (size > 1) ? buf[1] : 0;
			ev.c = (size > 2) ? buf[2] : 0;
		}

		_midiOut.push_back(std::move(ev));
	}
}

bool Device::parseMidiTx(uint32_t& s, uint8_t*& buffer)
{
	buffer = m_midiBuf;

	while(!m_dx7.midiSerialTx.empty())
	{
		uint8_t byte = m_dx7.midiSerialTx.read();

		switch(m_txState)
		{
		case Ctrl:
			switch(byte & 0xF0)
			{
			case 0x80: case 0x90: case 0xA0: case 0xB0:
				m_midiBuf[0] = byte; m_midiTxSize = 3; m_txState = Data1;
				break;
			case 0xC0: case 0xD0:
				m_midiBuf[0] = byte; m_midiTxSize = 2; m_txState = Data1;
				break;
			case 0xE0:
				m_midiBuf[0] = byte; m_midiTxSize = 3; m_txState = Data1;
				break;
			case 0xF0:
				if((byte & 0x0F) == 0x00) {
					m_txState = Sysex;
					m_midiTxSize = 0;
					m_midiBuf[m_midiTxSize++] = byte;
				}
				break;
			}
			break;

		case Data1:
			m_midiBuf[1] = byte;
			if(m_midiTxSize == 2) {
				m_txState = Ctrl;
				s = m_midiTxSize;
				return true;
			}
			m_txState = Data2;
			break;

		case Data2:
			m_midiBuf[2] = byte;
			m_txState = Ctrl;
			s = m_midiTxSize;
			return true;

		case Sysex:
			if(byte & 0x80 && byte != 0xF7) break; // ignore RT messages
			m_midiBuf[m_midiTxSize++] = byte;
			if(byte == 0xF7 || m_midiTxSize >= kMaxSysex) {
				m_txState = Ctrl;
				s = m_midiTxSize;
				return true;
			}
			break;
		}
	}
	return false;
}

void Device::setMidiVelocity(float c)
{
	if(c < 0.25f || c > 4.0f) c = 1.0f;
	for(int i = 0; i < 128; i++)
		m_midiVelocity[i] = static_cast<uint8_t>(127.0f * std::pow(static_cast<float>(i) / 127.0f, c) + 0.5f);
}

std::string Device::extractPatchName(const uint8_t* voiceData, size_t size)
{
	// DX7 packed voice format: 128 bytes per voice, name at offset 118, length 10
	// This matches the packed format stored in internal memory (32 voices × 128 bytes = 4096 bytes)
	if(size < 128) return {};

	std::string name;
	name.reserve(10);
	for(int i = 0; i < 10; i++)
	{
		char c = static_cast<char>(voiceData[118 + i]);
		if(c < 32 || c > 126) c = ' ';
		name += c;
	}

	// Trim trailing spaces
	while(!name.empty() && name.back() == ' ')
		name.pop_back();

	return name;
}

} // namespace dx7Emu
