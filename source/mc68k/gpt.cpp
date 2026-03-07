#include "gpt.h"

#include "logging.h"
#include "mc68k.h"

namespace mc68k
{
	namespace
	{
		void funcTimerOverflow(Gpt* _gpt)
		{
			_gpt->timerOverflow();
		}
		void funcTimerNoOverflow(Gpt*)
		{
		}
		constexpr uint16_t g_tmsk1_ociBit[] = {11,12,13,14};
		constexpr uint16_t g_tmsk1_ociMask[] = {(1<<g_tmsk1_ociBit[0]), (1<<g_tmsk1_ociBit[1]), (1<<g_tmsk1_ociBit[2]), (1<<g_tmsk1_ociBit[3])};

		constexpr uint8_t g_vba_oc[] = {0b100, 0b100 + 1, 0b100 + 2, 0b100 + 3};

		constexpr uint16_t g_tflg1_ocfBit[] = {11, 12, 13, 14};
		constexpr uint16_t g_tflg1_ocfMask[] = {(1<<g_tflg1_ocfBit[0]), (1<<g_tflg1_ocfBit[1]), (1<<g_tflg1_ocfBit[2]), (1<<g_tflg1_ocfBit[3])};

		constexpr uint16_t g_tflg1_ocfAll = g_tflg1_ocfMask[0] | g_tflg1_ocfMask[1] | g_tflg1_ocfMask[2] | g_tflg1_ocfMask[3];

		constexpr uint16_t g_tocCount = 2;	// we only need 2 for now, save performance by ignoring the others
	}

	Gpt::Gpt(Mc68k& _mc68k): m_mc68k(_mc68k)
	{
		m_timerFuncs[0] = &funcTimerNoOverflow;
		m_timerFuncs[1] = &funcTimerOverflow;
		m_tocLoad.fill(0);

		write16(PeriphAddress::Tmsk1, 0);
		write16(PeriphAddress::Tflg1, 0);

		write16(PeriphAddress::Toc1, 0xffff);
		write16(PeriphAddress::Toc2, 0xffff);
		write16(PeriphAddress::Toc3, 0xffff);
		write16(PeriphAddress::Toc4, 0xffff);
	}

	void Gpt::write8(PeriphAddress _addr, const uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			m_portGP.setDirection(_val);
			return;
		case PeriphAddress::PortGp:
//			LOG("Set PortGP to " << HEXN(_val,2));
			m_portGP.writeTX(_val);
			return;
		default:
			break;
		}

		MCLOG("write8 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(static_cast<int>(_val),2));
	}

	uint8_t Gpt::read8(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::PortGp:
			return m_portGP.read();
		case PeriphAddress::Tcnt:
			return static_cast<uint8_t>(read16(PeriphAddress::Tcnt) >> 8);
		case PeriphAddress::TcntLSB:
			return static_cast<uint8_t>(read16(PeriphAddress::Tcnt) & 0xff);
		default:
			MCLOG("read8 addr=" << MCHEXN(_addr, 8));
		}

		return PeripheralBase::read8(_addr);
	}

	void Gpt::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			m_portGP.setDirection(_val & 0xff);
			m_portGP.writeTX(_val>>8);
			break;
		case PeriphAddress::Toc1:	updateToc<0>();		break;
		case PeriphAddress::Toc2:	updateToc<1>();		break;
		case PeriphAddress::Toc3:	updateToc<2>();		break;
		case PeriphAddress::Toc4:	updateToc<3>();		break;
		case PeriphAddress::Tflg1:
			updateToc<0>();
			updateToc<1>();
			updateToc<2>();
			updateToc<3>();
			break;
		default:
//			MCLOG("write16 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(_val,4));
			break;
		}
	}

	uint16_t Gpt::read16(PeriphAddress _addr)
	{
		switch (_addr)
		{
		case PeriphAddress::DdrGp:
			{
				const uint16_t dir = m_portGP.getDirection();
				const uint16_t data = m_portGP.read();
				return (dir << 8) | data;
			}
		case PeriphAddress::Tcnt:
			{
				const auto r = rawTcnt() & 0xffff;
//				MCLOG("Read TCNT=" << MCHEXN(r,4) << " at PC=" << m_mc68k.getPC());
				return static_cast<uint16_t>(r);
			}
		default:
			break;
		}

//		MCLOG("read16 addr=" << MCHEXN(_addr, 8));

		return PeripheralBase::read16(_addr);
	}

	void Gpt::injectInterrupt(uint8_t _vba)
	{
		const auto icr = read16(PeriphAddress::Icr);
		const auto level = static_cast<uint8_t>((icr >> 8) & 3);
		const auto msb = icr & 0xf0;
		const auto vba = static_cast<uint8_t>(msb | _vba);

		if(!m_mc68k.hasPendingInterrupt(vba, level))
			m_mc68k.injectInterrupt(vba, level);
	}

	void Gpt::exec(const uint32_t _deltaCycles)
	{
		PeripheralBase::exec(_deltaCycles);

		if constexpr (g_tocCount > 0)	execToc<0>(_deltaCycles);
		if constexpr (g_tocCount > 1)	execToc<1>(_deltaCycles);
		if constexpr (g_tocCount > 2)	execToc<2>(_deltaCycles);
		if constexpr (g_tocCount > 3)	execToc<3>(_deltaCycles);
	}

	void Gpt::timerOverflow()
	{
		const auto tmsk = read16(PeriphAddress::Tmsk1);

		write16(PeriphAddress::Tflg1, read16(PeriphAddress::Tflg1) | g_tflg1_ocfAll);

		if(tmsk & g_tmsk1_ociMask[0])		injectInterrupt(g_vba_oc[0]);
		if(tmsk & g_tmsk1_ociMask[1])		injectInterrupt(g_vba_oc[1]);
		if(tmsk & g_tmsk1_ociMask[2])		injectInterrupt(g_vba_oc[2]);
		if(tmsk & g_tmsk1_ociMask[3])		injectInterrupt(g_vba_oc[3]);
	}

	template<uint32_t TocIndex>
	void Gpt::execToc(const uint32_t _deltaCycles)
	{
		constexpr auto tocDiff = static_cast<uint32_t>(PeriphAddress::Toc2) - static_cast<uint32_t>(PeriphAddress::Toc1);
		constexpr auto tocAddr = static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::Toc1) + (TocIndex * tocDiff));
		constexpr auto finishedMask = g_tmsk1_ociMask[TocIndex];
		constexpr auto interruptMask = g_tflg1_ocfMask[TocIndex];
		constexpr auto vba = g_vba_oc[TocIndex];

		auto& tocLoad = m_tocLoad[TocIndex];

		const auto tocTarget = static_cast<int32_t>(read16(tocAddr)) << 2;

		tocLoad += static_cast<int32_t>(_deltaCycles);

		if(tocLoad < tocTarget)
			return;

		const auto tflg = PeripheralBase::read16(PeriphAddress::Tflg1);
		const auto wasFinished = tflg & finishedMask;

		if(wasFinished)
			return;

		// set finished flag
		PeripheralBase::write16(PeriphAddress::Tflg1, tflg | finishedMask);

		const auto tmsk = PeripheralBase::read16(PeriphAddress::Tmsk1);
		const auto interruptEnabled = tmsk & interruptMask;

		// inject interrupt if enabled
		if(interruptEnabled)
			injectInterrupt(vba);
	}

	template <uint32_t TocIndex> void Gpt::updateToc()
	{
		constexpr auto tocDiff = static_cast<uint32_t>(PeriphAddress::Toc2) - static_cast<uint32_t>(PeriphAddress::Toc1);
		constexpr auto tocAddr = static_cast<PeriphAddress>(static_cast<uint32_t>(PeriphAddress::Toc1) + (TocIndex * tocDiff));

		const auto value = PeripheralBase::read16(tocAddr) << 2;

		auto& load = m_tocLoad[TocIndex];

		while(load > value)
			load -= 0x40000;

		while((value - load) >= 0x40000)
			load += 0x40000;
	}

	uint64_t Gpt::rawTcnt() const
	{
		return (m_mc68k.getCycles() >> 2);
	}
}
