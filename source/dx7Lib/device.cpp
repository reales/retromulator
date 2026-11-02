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

	// Without a firmware ROM the clean-room controller drives the EGS
	if(_params.romData.empty())
	{
		m_dx7.midiFilter.set_f(10.6f / kNativeSamplerate);
		bootClean();
		return;
	}

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

		m_dx7.initControllers();
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
	return m_clean || m_dx7.isRomLoaded();
}

template<typename G> static auto& ctrlOf(G& g, int n)
{
	return n == 0 ? g.wheel : n == 1 ? g.breath : n == 2 ? g.foot : g.aftertouch;
}

void Device::setController(int n, int range, int assign)
{
	if(!m_clean)
	{
		m_dx7.setController(n, static_cast<uint8_t>(range), static_cast<uint8_t>(assign));
		return;
	}

	// n: 0 wheel, 1 breath, 2 foot, 3 aftertouch
	auto g = m_clean->global();
	auto& c = ctrlOf(g, n & 3);
	c.range = static_cast<uint8_t>(std::clamp(range, 0, 99));
	c.assign = static_cast<uint8_t>(assign & 7);
	setFunctions(g);
}

// Power-on function memory as measured (INTERFACE.md 6), except pitch bend range: the measured
// unit holds 0, which disables bend. 2 semitones is the General MIDI default and the old adapter's fallback
dx7clean::Global Device::defaults()
{
	dx7clean::Global g;
	g.bendRange = 2;
	return g;
}

void Device::setFunctions(const dx7clean::Global& g)
{
	if(!m_clean)
		return;
	namespace fn = dx7clean::fn;
	// compared with the core, so values changed by function sysex from MIDI are not skipped
	const auto& cur = m_clean->global();
	auto set = [this](uint8_t p, uint8_t now, uint8_t want) { if(now != want) m_clean->setFunction(p, want); };
	set(fn::PolyMono, cur.mono, g.mono);
	set(fn::BendRange, cur.bendRange, g.bendRange);
	set(fn::BendStep, cur.bendStep, g.bendStep);
	set(fn::PortaMode, cur.portaMode, g.portaMode);
	set(fn::Glissando, cur.glissando, g.glissando);
	set(fn::PortaTime, cur.portaTime, g.portaTime);
	set(fn::WheelRange, cur.wheel.range, std::min<uint8_t>(g.wheel.range, 99));
	set(fn::WheelAssign, cur.wheel.assign, g.wheel.assign & 7);
	set(fn::FootRange, cur.foot.range, std::min<uint8_t>(g.foot.range, 99));
	set(fn::FootAssign, cur.foot.assign, g.foot.assign & 7);
	set(fn::BreathRange, cur.breath.range, std::min<uint8_t>(g.breath.range, 99));
	set(fn::BreathAssign, cur.breath.assign, g.breath.assign & 7);
	set(fn::AtRange, cur.aftertouch.range, std::min<uint8_t>(g.aftertouch.range, 99));
	set(fn::AtAssign, cur.aftertouch.assign, g.aftertouch.assign & 7);
}

// top bytes, version 1: version, bend range | mono << 4 | porta mode << 5 | glissando << 6, bend step,
// porta time. Bit 16 of word 0 (firmware choice in older sessions) is ignored
void Device::packFunctions(const dx7clean::Global& g, int32_t (&w)[4])
{
	const uint32_t fn[4] = { 1u, static_cast<uint32_t>(g.bendRange | (g.mono ? 0x10 : 0) | ((g.portaMode & 1) << 5) | (g.glissando ? 0x40 : 0)),
	                         g.bendStep, g.portaTime };
	for(int n = 0; n < 4; n++)
	{
		const auto& c = ctrlOf(g, n);
		w[n] = static_cast<int32_t>(c.range | (c.assign << 8) | (fn[n] << 24));
	}
}

dx7clean::Global Device::unpackFunctions(const int32_t (&w)[4])
{
	auto g = defaults();
	const auto top = [&](int n) { return static_cast<uint32_t>(w[n]) >> 24; };
	for(int n = 0; n < 4; n++)
	{
		auto& c = ctrlOf(g, n);
		c.range = static_cast<uint8_t>(std::min(99, w[n] & 0xff));
		c.assign = static_cast<uint8_t>((w[n] >> 8) & 7);
	}
	if(top(0) >= 1)
	{
		g.bendRange = static_cast<uint8_t>(std::min(12u, top(1) & 0x0f));
		g.mono = (top(1) & 0x10) != 0;
		g.portaMode = static_cast<uint8_t>((top(1) >> 5) & 1);
		g.glissando = (top(1) & 0x40) != 0;
		g.bendStep = static_cast<uint8_t>(std::min(12u, top(2)));
		g.portaTime = static_cast<uint8_t>(std::min(99u, top(3)));
	}
	return g;
}

void Device::bootClean()
{
	std::unique_ptr<dx7clean::Core> old = std::move(m_clean);
	m_clean = std::make_unique<dx7clean::Core>();
	if(old)
		for(int i = 0; i < static_cast<int>(dx7clean::kVoicesPerBank); i++)
			m_clean->storeInternal(i, old->internal(i));
	m_clean->onWrite = [this](uint16_t addr, uint8_t value, uint64_t) { m_dx7.busWrite(addr, value); };

	m_clean->presetFunction(dx7clean::fn::BendRange, defaults().bendRange);

	float discard[kBufSize + 4];
	for(int i = 0; i < 4096; i++)
	{
		int cnt = 0;
		m_clean->advance(24);
		m_dx7.egs.clock(discard, cnt, 96);
	}
}

void Device::resetPerformance()
{
	if(!m_clean)
		return;
	bootClean();
	m_midiExpression = 1.0f;
	m_dx7.midiFilter.reset();
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
	if(_type == synthLib::StateTypeGlobal && m_clean)
	{
		_state.clear();
		for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
		{
			const auto packed = dx7clean::pack(m_clean->internal(static_cast<int>(i)));
			_state.insert(_state.end(), packed.begin(), packed.end());
		}
		return true;
	}
	if(_type == synthLib::StateTypeGlobal)
	{
		m_dx7.saveRAM(_state);
		return true;
	}
	return false;
}

bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if(_type == synthLib::StateTypeGlobal && m_clean)
	{
		if(_state.size() != dx7clean::kVmemSize)
			return false;
		for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
			m_clean->storeInternal(static_cast<int>(i), dx7clean::unpack(_state.data() + i * 128));
		return true;
	}
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
	if(m_shutdown.load()) return;
	if(m_clean)
	{
		processAudioClean(_outputs, _samples);
		return;
	}
	if(!m_dx7.isRomLoaded()) return;

	auto* outL = _outputs[0];
	auto* outR = _outputs[1];

	size_t outPos = 0;
	Message msg;

	while(outPos < _samples)
	{
		const int needed = static_cast<int>(std::min(_samples - outPos, static_cast<size_t>(kBufSize)));

		// One sample every 24 CPU cycles, but the CPU steps whole instructions
		while(m_pendingCnt < needed)
		{
			if(m_shutdown.load(std::memory_order_relaxed))
				return;

			if(!m_dx7.haveMsg)
				if(m_toSynth->pop(msg)) processMessage(msg);

			m_dx7.run();
			m_dx7.egs.clock(m_pending, m_pendingCnt, 4 * std::max(m_dx7.inst->cycles, 1));
		}

		// MIDI volume filtering and expression
		const float mv = m_dx7.midiVolTab[m_dx7.midiVolume] * m_midiExpression + 1e-18f;

		for(int i = 0; i < needed; i++, outPos++)
		{
			const float sample = m_pending[i] * m_volume * m_dx7.midiFilter.operate(mv);
			outL[outPos] = sample;
			outR[outPos] = sample; // mono to stereo
		}

		m_pendingCnt -= needed;
		std::copy_n(m_pending + needed, m_pendingCnt, m_pending);
	}
}

void Device::processAudioClean(const synthLib::TAudioOutputs& _outputs, size_t _samples)
{
	auto* outL = _outputs[0];
	auto* outR = _outputs[1];

	size_t outPos = 0;
	while(outPos < _samples)
	{
		const int needed = static_cast<int>(std::min(_samples - outPos, static_cast<size_t>(kBufSize)));

		// 24 controller cycles = 96 EGS clocks = one sample
		while(m_pendingCnt < needed)
		{
			m_clean->advance(24);
			m_dx7.egs.clock(m_pending, m_pendingCnt, 96);
		}

		const float mv = m_dx7.midiVolTab[m_dx7.midiVolume] * m_midiExpression + 1e-18f;

		for(int i = 0; i < needed; i++, outPos++)
		{
			const float sample = m_pending[i] * m_volume * m_dx7.midiFilter.operate(mv);
			outL[outPos] = sample;
			outR[outPos] = sample;
		}

		m_pendingCnt -= needed;
		std::copy_n(m_pending + needed, m_pendingCnt, m_pending);
	}
}

void Device::sendMidiClean(const synthLib::SMidiEvent& _ev)
{
	// The controller listens on channel 1 only; the plugin is omni
	if(!_ev.sysex.empty())
	{
		// 32-voice bulk dump goes straight to voice memory, at 31250 baud it would hold MIDI for 1.4 s
		const auto& s = _ev.sysex;
		if(s.size() == dx7clean::kVmemSize + 8 && s[0] == 0xF0 && s[1] == 0x43 && (s[2] & 0xF0) == 0 && s[3] == 0x09)
		{
			for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
				m_clean->storeInternal(static_cast<int>(i), dx7clean::unpack(s.data() + 6 + i * dx7clean::kVmemVoiceSize));
			return;
		}

		auto sysex = _ev.sysex;
		if(sysex.size() > 2 && sysex[0] == 0xF0 && sysex[1] == 0x43)
			sysex[2] &= 0xF0;
		m_clean->midiIn(sysex.data(), sysex.size());
		return;
	}

	if(_ev.a >= 0xF8)
		return;

	const uint8_t status = _ev.a & 0xF0;
	if(status == 0xB0 && _ev.b == 11)
	{
		m_midiExpression = _ev.c / 127.0f;
		return;
	}
	if(status == 0xB0 && _ev.b == 120)
	{
		// all sound off: release every key, sustained ones too
		cleanIn(0xB0, 64, 0);
		cleanIn(0xB0, 123, 0);
		return;
	}
	if(status == 0xB0 && _ev.b == 121)
	{
		// reset controllers to their power-on values (portamento pedal on, as the DX7 boots); volume stays
		m_midiExpression = 1.0f;
		cleanIn(0xB0, 1, 0);
		cleanIn(0xB0, 2, 0);
		cleanIn(0xB0, 4, 0);
		cleanIn(0xD0, 0, 0);
		cleanIn(0xE0, 0, 64);
		cleanIn(0xB0, 64, 0);
		cleanIn(0xB0, 65, 127);
		return;
	}
	if(status == 0xB0 && (_ev.b == 0 || _ev.b == 32 || _ev.b == 100 || _ev.b == 101))
		return;

	cleanIn(static_cast<uint8_t>(_ev.a < 0xF0 ? status : _ev.a), _ev.b, _ev.c);
}

void Device::cleanIn(uint8_t a, uint8_t b, uint8_t c)
{
	const uint8_t buf[3] = { a, b, c };
	const uint8_t status = a & 0xF0;
	const size_t len = (status == 0xC0 || status == 0xD0) ? 2 : 3;
	m_clean->midiIn(buf, len);
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

		case Message::CtrlID::pitchbend:
		{
			// Read patch pitch bend range from edit buffer (0=use default 2)
			uint8_t pbRange = m_dx7.memory[0x2076] & 0x0F;
			if(pbRange == 0) pbRange = 2;
			// EGS: 1 semitone ≈ 85.3 freq units, pitchMod = EGS_reg / 16
			// So 1 semitone = 1365 EGS units. Scale by patch PB range.
			int centered = static_cast<int>(msg.byte2) - 64;
			m_dx7.pitchBendOffset = static_cast<int16_t>(centered * 1365 * pbRange / 63);
			// Also deliver to firmware via sub-CPU
			m_dx7.msg = msg;
			m_dx7.haveMsg = true;
			break;
		}

		case Message::CtrlID::modulate:
			// Firmware drives mod wheel -> LFO depth natively via the sub-CPU.
			// No direct EGS injection: the wheel modulates the LFO, it does not
			// transpose pitch.
			m_dx7.msg = msg;
			m_dx7.haveMsg = true;
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
	if(m_clean)
	{
		sendMidiClean(_ev);
		return true;
	}

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
			// Flush the serial RX buffer so any accumulated CC100/101 garbage
			// (Logic AU floods these) doesn't delay or corrupt the PC that follows.
			m_dx7.midiSerialRx.flush();
			// Firmware's bulk-dump handler clears master tune (writes 0 to 0x2311).
			// Re-apply A440 tuning so notes don't drift after a patch reload.
			m_dx7.tune(0);
			return true;
		}

		// Voice parameter change (F0 43 1n gg pp vv F7): the firmware only accepts
		// it on its own RX channel, so patch the channel nibble in.
		if(_ev.sysex.size() == 7 && _ev.sysex[1] == 0x43 && (_ev.sysex[2] & 0xF0) == 0x10)
		{
			m_dx7.setSysInfoAvail(true);
			auto sysex = _ev.sysex;
			sysex[2] = static_cast<uint8_t>(0x10 | (m_dx7.getMidiRxChannel() & 0x0F));
			for(const auto byte : sysex)
				m_dx7.midiSerialRx.write(byte);
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
		if(data[1] >= 36)
			m_toSynth->key_off(data[1] - 36);
		return;

	case 0x90: // Note On
		if(data[1] >= 36)
		{
			if(data[2] == 0)
				m_toSynth->key_off(data[1] - 36);
			else
				m_toSynth->key_on(data[1] - 36, m_midiVelocity[data[2]]);
		}
		return;

	case 0xB0: // Control Change
		switch(data[1])
		{
		case 0:   return; // Bank MSB - eat to avoid firmware bug
		case 100: return; // RPN LSB  - Logic AU floods these; eat to protect serial RX buffer
		case 101: return; // RPN MSB  - Logic AU floods these; eat to protect serial RX buffer
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
