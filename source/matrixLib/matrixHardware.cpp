#include "matrixHardware.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace matrixLib
{
	Hardware::Hardware() : m_cpu(*this), m_acia(MidiCyclesPerByte)
	{
		m_rom.assign(0x8000, 0xff);
		m_patchRom.assign(0x10000, 0xff);
		m_sram.resize(0x8000);

		// an SRAM powers up with noise, the firmware treats an all-zero cold-start flag as valid RAM
		uint32_t seed = 0x1234567;
		for(auto& b : m_sram)
		{
			seed = seed * 1103515245u + 12345u;
			b = static_cast<uint8_t>(seed >> 16);
		}
	}

	bool Hardware::setFirmware(const std::vector<uint8_t>& _rom)
	{
		if(_rom.size() != 0x8000)
			return false;
		m_rom = _rom;
		return true;
	}

	void Hardware::setPatchRom(const std::vector<uint8_t>& _rom)
	{
		m_patchRom.assign(0x10000, 0xff);
		std::memcpy(m_patchRom.data(), _rom.data(), std::min<size_t>(_rom.size(), m_patchRom.size()));
	}

	void Hardware::reset()
	{
		for(auto& t : m_timers)
			t.reset();
		m_sysTimer.reset();
		m_acia.reset();
		m_bankLatch = 0;
		m_cpu.reset();
		for(auto& d : m_dco)
			d = DcoScan();
	}

	void Hardware::scanDcoA(const uint32_t _voice, const uint64_t _until)
	{
		auto& d = m_dco[_voice];
		auto& timer = m_timers[_voice >= 3 ? 1 : 0];
		const uint32_t ch = _voice % 3;

		uint64_t t = d.scannedA;
		if(_until <= t)
			return;
		while(true)
		{
			const bool level = timer.getOut(ch, t);
			if(level != d.levelA)
			{
				d.levelA = level;
				if(level)
				{
					// a sync discharge may already be queued behind this tick
					auto& a = d.events.a;
					a.insert(std::upper_bound(a.begin(), a.end(), t), t);
				}
			}
			const uint64_t e = timer.nextOutEdge(ch, t);
			if(e >= _until)
				break;
			t = e;
		}
		d.scannedA = _until;
	}

	void Hardware::scanDco(const uint32_t _voice, const uint64_t _until)
	{
		auto& d = m_dco[_voice];
		auto& timerA = m_timers[_voice >= 3 ? 1 : 0];
		auto& timerB = m_timers[_voice >= 3 ? 3 : 2];
		const uint32_t ch = _voice % 3;

		// $1D00 bit 3 = SYNC1*, bit 2 = SYNC2*
		const uint8_t ctl = m_latches[0x100];
		const bool syncGate = !(ctl & 0x08);
		const bool syncDischarge = !(ctl & 0x04);

		uint64_t t = d.scannedB;
		if(_until > t)
		{
			while(true)
			{
				const bool level = timerB.getOut(ch, t);
				if(level != d.levelB)
				{
					d.levelB = level;
					if(level)
					{
						d.events.b.push_back(t);
					}
					else if(syncGate || syncDischarge)
					{
						// the differentiated falling edge holds the gates low for SyncPulseTicks
						scanDcoA(_voice, t);
						if(syncGate)
							timerA.gate(ch, t, t + SyncPulseTicks);
						if(syncDischarge)
							d.events.a.push_back(t + SyncPulseTicks);
					}
				}
				const uint64_t e = timerB.nextOutEdge(ch, t);
				if(e >= _until)
					break;
				t = e;
			}
			d.scannedB = _until;
		}
		scanDcoA(_voice, _until);
	}

	void Hardware::scanTimer(const uint32_t _timer, const uint64_t _until)
	{
		const uint32_t first = (_timer & 1) * 3;
		for(uint32_t v = first; v < first + 3; ++v)
			scanDco(v, _until);
	}

	Hardware::DcoEvents& Hardware::scanDco(const uint32_t _voice)
	{
		scanDco(_voice, getTimerTick());
		return m_dco[_voice].events;
	}

	uint8_t* Hardware::sramPtr(const uint16_t _addr)
	{
		if(_addr >= 0x4000 && _addr < 0x6000)
			return &m_sram[_addr - 0x4000];
		if(_addr >= 0x6000 && _addr < 0x8000)
		{
			const uint32_t page = ((m_bankLatch + 1) & 3) * 0x2000;
			return &m_sram[page + (_addr - 0x6000)];
		}
		return nullptr;
	}

	uint8_t Hardware::getLatch(const uint16_t _addr) const
	{
		return m_latches[_addr & 0x3ff];
	}

	uint8_t Hardware::read(const uint16_t _addr)
	{
		if(_addr >= 0x8000)
			return m_rom[_addr - 0x8000];

		if(auto* p = sramPtr(_addr))
			return *p;

		if(_addr >= 0x2000)
			return m_patchRom[(m_bankLatch & 7) * 0x2000 + (_addr - 0x2000)];

		const uint64_t t = m_cpu.getCycles();
		uint8_t v = 0;

		if(_addr < 0x1000)
		{
			// the timers evaluate lazily, the edges up to now have to be collected first
			scanTimer((_addr >> 10) & 3, t * 2);
			v = m_timers[(_addr >> 10) & 3].read(_addr & 3, t * 2);
		}
		else if(_addr >= 0x1400 && _addr < 0x1800)
		{
			if(_addr & 0x0200)
				v = m_sysTimer.read(_addr & 3, t);
			else
			{
				m_acia.advance(t);
				v = m_acia.read(_addr & 1, t);
			}
		}
		else if(_addr >= 0x1800 && _addr < 0x1c00)
			v = (_addr & 1) ? m_switch1 : m_switch0;
		else
			v = 0xff;

		if(onIoAccess)
			onIoAccess(_addr, v, false);
		return v;
	}

	void Hardware::write(const uint16_t _addr, const uint8_t _val)
	{
		if(_addr >= 0x8000)
			return;

		if(auto* p = sramPtr(_addr))
		{
			*p = _val;
			return;
		}

		if(onIoAccess)
			onIoAccess(_addr, _val, true);

		if(_addr >= 0x2000)
			return;

		const uint64_t t = m_cpu.getCycles();

		if(_addr < 0x1000)
		{
			scanTimer((_addr >> 10) & 3, t * 2);
			m_timers[(_addr >> 10) & 3].write(_addr & 3, _val, t * 2);
		}
		else if(_addr < 0x1400)
		{
			// every write latches the S&H address, even = DAC high byte, odd = low byte.
			// Bit 2 of the low byte inhibits the multiplexer, the selected S&H tracks the DAC otherwise
			m_cvChannel = cvChannel(_addr);
			m_cvRange = static_cast<uint8_t>((_addr >> 7) & 3);
			if(_addr & 1)
			{
				m_dacLow = _val;
				m_cvInhibit = (_val & 0x04) != 0;
			}
			else
			{
				m_dacHigh = _val;
			}

			if(!m_cvInhibit)
			{
				const auto raw = static_cast<uint16_t>((m_dacHigh << 8) | m_dacLow);
				const auto code = static_cast<uint16_t>(raw >> 3);
				// 12 bit DAC on bits 14-3, 0 to -DacFullScale at the I/V converter. The output
				// amplifier inverts with 12k feedback: A8 (RANGE2) removes the 133k in parallel
				// and adds 20k from the reference, A7 (RANGE1) adds 182k from the reference
				const float dac = static_cast<float>(raw & 0x7ff8) * (DacFullScale / 32768.0f);
				const float feedback = (m_cvRange & 2) ? 12.0f : 12.0f * 133.0f / 145.0f;
				float volts = dac * feedback / 10.0f;
				if(m_cvRange & 2)
					volts -= DacReference * feedback / 20.0f;
				if(m_cvRange & 1)
					volts -= DacReference * feedback / 182.0f;
				m_cv[m_cvChannel] = code;
				m_dacVolts = volts;
				m_dacFast = !(raw & 0x8000) && m_cvChannel < VoiceCount * 8;
				m_cvHold[m_cvChannel] = volts;
				if(m_dacFast)
					m_cvVolts[m_cvChannel] = volts;
				if(onCvWrite)
					onCvWrite(_addr & 0xfffe, code);
			}
		}
		else if(_addr < 0x1800)
		{
			if(_addr & 0x0200)
				m_sysTimer.write(_addr & 3, _val, t);
			else
			{
				m_acia.advance(t);
				m_acia.write(_addr & 1, _val, t);
			}
		}
		else if(_addr >= 0x1c00)
		{
			if((_addr & 0x3ff) == 0x100)
			{
				// the sync gates change
				for(uint32_t v = 0; v < VoiceCount; ++v)
					scanDco(v, t * 2);
			}
			m_latches[_addr & 0x3ff] = _val;
			if(_addr == 0x1d80)
				m_bankLatch = _val;
		}
	}

	void Hardware::advanceCv(const float _seconds)
	{
		constexpr float hold = 33e-9f;
		constexpr float pin = 6.8e-9f;
		constexpr float tau = 1e6f * hold * pin / (hold + pin);
		const float moved = 1.0f - std::exp(-_seconds / tau);
		const float toPin = moved * hold / (hold + pin);
		const float fromHold = moved * pin / (hold + pin);

		for(size_t i = 0; i < m_cvVolts.size(); ++i)
		{
			const float d = m_cvHold[i] - m_cvVolts[i];
			m_cvVolts[i] += d * toPin;
			m_cvHold[i] -= d * fromHold;
		}

		// the multiplexer stays on its channel until the next write, the DAC keeps driving it
		if(!m_cvInhibit)
		{
			m_cvHold[m_cvChannel] = m_dacVolts;
			if(m_dacFast)
				m_cvVolts[m_cvChannel] = m_dacVolts;
		}
	}

	uint32_t Hardware::getDcoPeriod(const uint32_t _voice, const uint32_t _dco)
	{
		scanDco(_voice, getTimerTick());
		auto& timer = m_timers[(_dco ? 2 : 0) + (_voice >= 3 ? 1 : 0)];
		return timer.getPeriod(_voice % 3, getTimerTick());
	}

	void Hardware::updateIrq()
	{
		const uint64_t t = m_cpu.getCycles();
		m_acia.advance(t);
		m_cpu.setFirq(m_acia.getIrq(t));
		m_cpu.setIrq(!m_sysTimer.getOut(2, t));
	}

	void Hardware::runUntil(const uint64_t _targetCycle)
	{
		while(m_cpu.getCycles() < _targetCycle)
		{
			updateIrq();
			if(onInstruction)
				onInstruction(m_cpu.getPC());
			m_cpu.step();
		}
	}
}
