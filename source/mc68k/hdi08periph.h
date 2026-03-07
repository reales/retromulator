#pragma once

#include "hdi08.h"

namespace mc68k
{
	template<uint32_t Base>
	class Hdi08Periph : public PeripheralBase<Base, 8>
	{
	public:
		uint8_t read8(const PeriphAddress _addr) override						{ return m_hdi08.read8  (toLocal(_addr)); }
		uint16_t read16(const PeriphAddress _addr) override						{ return m_hdi08.read16 (toLocal(_addr)); }
		void write8(const PeriphAddress _addr, const uint8_t _val) override		{ return m_hdi08.write8 (toLocal(_addr), _val); }
		void write16(const PeriphAddress _addr, const uint16_t _val) override	{ return m_hdi08.write16(toLocal(_addr), _val); }

		Hdi08& getHdi08() { return m_hdi08; }

		void exec(const uint32_t _deltaCycles) override							{ m_hdi08.exec(_deltaCycles); }

	private:
		static PeriphAddress toLocal(PeriphAddress _addr)						{ return static_cast<PeriphAddress>(static_cast<uint32_t>(_addr) - Base); }

		Hdi08 m_hdi08;
	};
}
