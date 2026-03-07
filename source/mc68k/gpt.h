#pragma once

#include "peripheralBase.h"
#include "peripheralTypes.h"
#include "port.h"

namespace mc68k
{
	class Mc68k;

	class Gpt final : public PeripheralBase<g_gptBase, g_gptSize>
	{
	public:
		typedef void (*TTimerFunc)(Gpt*);

		explicit Gpt(Mc68k& _mc68k);

		void write8(PeriphAddress _addr, uint8_t _val) override;
		uint8_t read8(PeriphAddress _addr) override;
		void write16(PeriphAddress _addr, uint16_t _val) override;
		uint16_t read16(PeriphAddress _addr) override;

		Port& getPortGP() { return m_portGP; }

		void injectInterrupt(uint8_t _vba);

		void exec(uint32_t _deltaCycles) override;

		void timerOverflow();

	private:
		template<uint32_t TocIndex>	void execToc(uint32_t _deltaCycles);
		template<uint32_t TocIndex>	void updateToc();

		uint64_t rawTcnt() const;
		Mc68k& m_mc68k;
		Port m_portGP;

		std::array<TTimerFunc, 2> m_timerFuncs;
		std::array<int32_t, 4> m_tocLoad;
	};
}
