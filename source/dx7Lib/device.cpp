/**
 *  DX7 Device adapter for Retromulator
 *  The clean-room controller (clean/) drives the EGS/OPS hardware models
 *
 *  EGS/OPS models based on VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com (GPL v3)
**/

#include "device.h"

#include <algorithm>

namespace dx7Emu
{

namespace
{
	// Volume DAC output per 3-bit code
	constexpr float kDacGain[8] = { 0, 710/4790.0f, 200/4790.0f, 2590/4790.0f,
		100/4790.0f, 1390/4790.0f, 380/4790.0f, 4790/4790.0f };
}

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
{
	m_dacFilter.set_f(10.6f / kNativeSamplerate);
	boot();
}

Device::~Device() = default;

float Device::getSamplerate() const
{
	return kNativeSamplerate;
}

void Device::busWrite(uint16_t addr, uint8_t value)
{
	if((addr & 0xFF00) == 0x3000)
	{
		m_egsRegs[addr & 0xFF] = value;
		m_egs.update(uint8_t(addr));
	}
	else if(addr == 0x2804)
		m_opsMode = value;
	else if(addr == 0x2805)
	{
		m_opsAlg = value;
		m_egs.setAlgorithm(m_opsMode, m_opsAlg);
	}
	else if(addr == 0x280A)
		m_dac = value & 7;
}

template<typename G> static auto& ctrlOf(G& g, int n)
{
	return n == 0 ? g.wheel : n == 1 ? g.breath : n == 2 ? g.foot : g.aftertouch;
}

void Device::setController(int n, int range, int assign)
{
	auto g = m_core->global();
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
	namespace fn = dx7clean::fn;
	// compared with the core, so values changed by function sysex from MIDI are not skipped
	const auto& cur = m_core->global();
	auto set = [this](uint8_t p, uint8_t now, uint8_t want) { if(now != want) m_core->setFunction(p, want); };
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

// a new core keeps the voice memory of the previous one
void Device::boot()
{
	std::unique_ptr<dx7clean::Core> old = std::move(m_core);
	m_core = std::make_unique<dx7clean::Core>();
	if(old)
		for(int i = 0; i < static_cast<int>(dx7clean::kVoicesPerBank); i++)
			m_core->storeInternal(i, old->internal(i));
	m_core->onWrite = [this](uint16_t addr, uint8_t value, uint64_t) { busWrite(addr, value); };

	m_core->presetFunction(dx7clean::fn::BendRange, defaults().bendRange);

	// settle the power-on writes
	float discard[kBufSize + 4];
	for(int i = 0; i < 4096; i++)
	{
		int cnt = 0;
		m_core->advance(24);
		m_egs.clock(discard, cnt, 96);
	}
}

void Device::resetPerformance()
{
	boot();
	m_midiExpression = 1.0f;
	m_dacFilter.reset();
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
	if(_type != synthLib::StateTypeGlobal)
		return false;
	_state.clear();
	for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
	{
		const auto packed = dx7clean::pack(m_core->internal(static_cast<int>(i)));
		_state.insert(_state.end(), packed.begin(), packed.end());
	}
	return true;
}

bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if(_type != synthLib::StateTypeGlobal || _state.size() != dx7clean::kVmemSize)
		return false;
	for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
		m_core->storeInternal(static_cast<int>(i), dx7clean::unpack(_state.data() + i * 128));
	return true;
}
#endif

void Device::processAudio(const synthLib::TAudioInputs& /*_inputs*/, const synthLib::TAudioOutputs& _outputs, size_t _samples)
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
			m_core->advance(24);
			m_egs.clock(m_pending, m_pendingCnt, 96);
		}

		const float mv = kDacGain[m_dac] * m_midiExpression + 1e-18f;

		for(int i = 0; i < needed; i++, outPos++)
		{
			const float sample = m_pending[i] * m_dacFilter.operate(mv);
			outL[outPos] = sample;
			outR[outPos] = sample;
		}

		m_pendingCnt -= needed;
		std::copy_n(m_pending + needed, m_pendingCnt, m_pending);
	}
}

bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
{
	// The controller listens on channel 1 only; the plugin is omni
	if(!_ev.sysex.empty())
	{
		// 32-voice bulk dump goes straight to voice memory, at 31250 baud it would hold MIDI for 1.4 s
		const auto& s = _ev.sysex;
		if(s.size() == dx7clean::kVmemSize + 8 && s[0] == 0xF0 && s[1] == 0x43 && (s[2] & 0xF0) == 0 && s[3] == 0x09)
		{
			for(size_t i = 0; i < dx7clean::kVoicesPerBank; i++)
				m_core->storeInternal(static_cast<int>(i), dx7clean::unpack(s.data() + 6 + i * dx7clean::kVmemVoiceSize));
			return true;
		}

		auto sysex = _ev.sysex;
		if(sysex.size() > 2 && sysex[0] == 0xF0 && sysex[1] == 0x43)
			sysex[2] &= 0xF0;
		m_core->midiIn(sysex.data(), sysex.size());
		return true;
	}

	if(_ev.a >= 0xF8)
		return true;

	const uint8_t status = _ev.a & 0xF0;
	if(status == 0xB0 && _ev.b == 11)
	{
		m_midiExpression = _ev.c / 127.0f;
		return true;
	}
	if(status == 0xB0 && _ev.b == 120)
	{
		// all sound off: release every key, sustained ones too
		coreIn(0xB0, 64, 0);
		coreIn(0xB0, 123, 0);
		return true;
	}
	if(status == 0xB0 && _ev.b == 121)
	{
		// reset controllers to their power-on values (portamento pedal on, as the DX7 boots); volume stays
		m_midiExpression = 1.0f;
		coreIn(0xB0, 1, 0);
		coreIn(0xB0, 2, 0);
		coreIn(0xB0, 4, 0);
		coreIn(0xD0, 0, 0);
		coreIn(0xE0, 0, 64);
		coreIn(0xB0, 64, 0);
		coreIn(0xB0, 65, 127);
		return true;
	}
	if(status == 0xB0 && (_ev.b == 0 || _ev.b == 32 || _ev.b == 100 || _ev.b == 101))
		return true;

	coreIn(static_cast<uint8_t>(_ev.a < 0xF0 ? status : _ev.a), _ev.b, _ev.c);
	return true;
}

void Device::coreIn(uint8_t a, uint8_t b, uint8_t c)
{
	const uint8_t buf[3] = { a, b, c };
	const uint8_t status = a & 0xF0;
	const size_t len = (status == 0xC0 || status == 0xD0) ? 2 : 3;
	m_core->midiIn(buf, len);
}

std::string Device::extractPatchName(const uint8_t* voiceData, size_t size)
{
	if(size < 128) return {};

	std::string name;
	name.reserve(10);
	for(int i = 0; i < 10; i++)
	{
		char c = static_cast<char>(voiceData[118 + i]);
		if(c < 32 || c > 126) c = ' ';
		name += c;
	}

	while(!name.empty() && name.back() == ' ')
		name.pop_back();

	return name;
}

} // namespace dx7Emu
