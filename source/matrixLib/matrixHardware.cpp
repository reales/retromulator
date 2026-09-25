#include "matrixHardware.h"

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
			v = m_timers[(_addr >> 10) & 3].read(_addr & 3, t * 2);
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
				// the DAC sees the low 15 bits: 0 to +5 V
				float volts = static_cast<float>(raw & 0x7ff8) * (5.0f / 32768.0f);
				if(m_cvRange & 2)
					volts -= 2.5f;
				m_cv[m_cvChannel] = code;
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
			m_latches[_addr & 0x3ff] = _val;
			if(_addr == 0x1d80)
				m_bankLatch = _val;
		}
	}

	uint32_t Hardware::getDcoPeriod(const uint32_t _voice, const uint32_t _dco)
	{
		auto& timer = m_timers[(_dco ? 2 : 0) + (_voice >= 3 ? 1 : 0)];
		return timer.getPeriod(_voice % 3, m_cpu.getCycles() * 2);
	}

	void Hardware::updateIrq()
	{
		const uint64_t t = m_cpu.getCycles();
		m_acia.advance(t);
		m_cpu.setFirq(m_acia.getIrq());
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
