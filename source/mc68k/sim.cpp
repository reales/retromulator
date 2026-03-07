#include "sim.h"

#include "logging.h"
#include "mc68k.h"

namespace mc68k
{
	constexpr const char* g_csParFields[]     = {"CSBOOT", "CSPA0[1]", "CSPA0[2]", "CSPA0[3]", "CSPA0[4]", "CSPA0[5]", "CSPA0[6]", "CSPA1[0]", "CSPA1[1]", "CSPA1[2]", "CSPA1[3]", "CSPA1[4]"};
	constexpr const char* g_csParSignals[]    = {"CSBOOT", "CS0",      "CS1",      "CS2",      "CS3",      "CS4",      "CS5"     , "CS6",    "CS7",      "CS8",      "CS9",      "CS10"      };
	constexpr const char* g_csParAlternates[] = {"-",      "BR",       "BG",       "BGACK",    "FC0",      "FC1",      "FC2"     , "ADDR19", "ADDR20",   "ADDR21",   "ADDR22",   "ADDR23"    };
	constexpr const char* g_csParDiscretes[]  = {"-",      "-",        "-",        "-",        "PC0",      "PC1",      "PC2"     , "PC3",    "PC4",      "PC5",      "PC6",      "ECLK"      };

	Sim::Sim(Mc68k& _mc68k) : m_mc68k(_mc68k)
	{
		write16(PeriphAddress::Syncr, 0x3f00);
		write16(PeriphAddress::Picr, 0xf);
	}

	uint16_t Sim::read16(const PeriphAddress _addr)
	{
		auto r = PeripheralBase::read16(_addr);

		switch (_addr)
		{
		case PeriphAddress::Syncr:
			r |= (1<<3);	// code waits until frequency has locked in, yes it has
			return r;
		default:
			MCLOG("read16 addr=" << MCHEXN(_addr, 8));
			return r;
		}
	}

	uint8_t Sim::read8(const PeriphAddress _addr)
	{
		const auto r = PeripheralBase::read8(_addr);

		switch (_addr)
		{
		case PeriphAddress::PortE0:
		case PeriphAddress::PortE1:
			return m_portE.read();
		case PeriphAddress::PortF0:
		case PeriphAddress::PortF1:
			return m_portF.read();
		default:
			MCLOG("read16 addr=" << MCHEXN(_addr, 8));
			return r;
		}
	}

	void Sim::write8(PeriphAddress _addr, uint8_t _val)
	{
		PeripheralBase::write8(_addr, _val);

		switch (_addr)
		{
		case PeriphAddress::Syncr:
			updateClock();
			return;
		case PeriphAddress::DdrE:
			MCLOG("Port E direction set to " << MCHEXN(_val, 2));
			m_portE.setDirection(_val);
			return;
		case PeriphAddress::DdrF:
			MCLOG("Port F direction set to " << MCHEXN(_val, 2));
			m_portF.setDirection(_val);
			return;
		case PeriphAddress::PEPar:
			MCLOG("Port E Pin assignment set to " << MCHEXN(_val, 2));
			m_portE.enablePins(~_val);
			return;
		case PeriphAddress::PFPar:
			MCLOG("Port F Pin assignment set to " << MCHEXN(_val, 2));
			m_portF.enablePins(~_val);
			return;
		case PeriphAddress::PortE0:
		case PeriphAddress::PortE1:
//			MCLOG("Port E write: " << MCHEXN(_val, 2));
			m_portE.writeTX(_val);
			return;
		case PeriphAddress::PortF0:
		case PeriphAddress::PortF1:
//			MCLOG("Port F write: " << MCHEXN(_val, 2));
			m_portF.writeTX(_val);
			return;
		case PeriphAddress::Picr:
		case PeriphAddress::Pitr:
			initTimer();
			return;
		default:
			break;
		}

		MCLOG("write8 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(static_cast<int>(_val),2));
	}

	void Sim::write16(PeriphAddress _addr, uint16_t _val)
	{
		PeripheralBase::write16(_addr, _val);

		MCLOG("write16 addr=" << MCHEXN(_addr, 8) << ", val=" << MCHEXN(_val,4));

		switch (_addr)
		{
		case PeriphAddress::Syncr:
			updateClock();
			return;
		case PeriphAddress::Picr:
		case PeriphAddress::Pitr:
			initTimer();
			return;
		case PeriphAddress::Cspar0:
			logChipSelectPinAssignments(_val, 0, 7);
			break;
		case PeriphAddress::Cspar1:
			logChipSelectPinAssignments(_val, 7, 5);
			break;
		case PeriphAddress::Csbarbt:
		case PeriphAddress::Csbar0:
		case PeriphAddress::Csbar1:
		case PeriphAddress::Csbar2:
		case PeriphAddress::Csbar3:
		case PeriphAddress::Csbar4:
		case PeriphAddress::Csbar5:
		case PeriphAddress::Csbar6:
		case PeriphAddress::Csbar7:
		case PeriphAddress::Csbar8:
		case PeriphAddress::Csbar9:
		case PeriphAddress::Csbar10:
			logChipSelectBaseAddressRegister((static_cast<uint32_t>(_addr) - static_cast<uint32_t>(PeriphAddress::Csbarbt)) >> 2, _val);
			break;
		case PeriphAddress::Csorbt:
		case PeriphAddress::Csor0:
		case PeriphAddress::Csor1:
		case PeriphAddress::Csor2:
		case PeriphAddress::Csor3:
		case PeriphAddress::Csor4:
		case PeriphAddress::Csor5:
		case PeriphAddress::Csor6:
		case PeriphAddress::Csor7:
		case PeriphAddress::Csor8:
		case PeriphAddress::Csor9:
		case PeriphAddress::Csor10:
			logChipSelectOptionRegister((static_cast<uint32_t>(_addr) - static_cast<uint32_t>(PeriphAddress::Csorbt)) >> 2, _val);
			break;
		default:
			break;
		}
	}

	void Sim::exec(const uint32_t _deltaCycles)
	{
		if(!m_timerLoadValue)
			return;

		m_timerCurrentValue -= static_cast<int32_t>(_deltaCycles);

		if(m_timerCurrentValue <= 0)
		{
			const auto picr = PeripheralBase::read16(PeriphAddress::Picr);
			const auto iv = picr & PivMask;
			const auto il = (picr & PirqlMask) >> PirqlShift;

			if(!m_mc68k.hasPendingInterrupt(static_cast<uint8_t>(iv), static_cast<uint8_t>(il)))
				m_mc68k.injectInterrupt(static_cast<uint8_t>(iv), static_cast<uint8_t>(il));

			m_timerCurrentValue += m_timerLoadValue;
		}
	}

	void Sim::setExternalClockHz(const uint32_t _hz)
	{
		if(_hz == m_externalClockHz)
			return;
		m_externalClockHz = _hz;
		updateClock();
	}

	void Sim::initTimer()
	{
		const auto picr = read16(PeriphAddress::Picr);
		const auto pitr = read16(PeriphAddress::Pitr);

		if(!(picr & PirqlMask))
		{
			m_timerLoadValue = 0;
			return;
		}

		const auto prescale = pitr & Ptp ? 512 : 1;
		const auto pitm = pitr & Pitm;

		// PIT Period = ((PIT Modulus)(Prescaler Value)(4)) / EXTAL Frequency
		const auto scale = m_systemClockHz / m_externalClockHz;
		m_timerLoadValue = static_cast<int32_t>(pitm * prescale * 4 * scale);
	}

	void Sim::updateClock()
	{
		const auto syncr = PeripheralBase::read16(PeriphAddress::Syncr);

		const auto w = (syncr>>15) & 1;
		const auto x = (syncr>>14) & 1;
		const auto y = (syncr>>8) & 0x3f;

		const auto hz = m_externalClockHz * (4 * (y+1) * (1<<(2*w+x)));

		const float mhz = static_cast<float>(hz) / 1000000.0f;

		MCLOG("Fsys=" << hz << "Hz / " << mhz << "MHz, Fext=" << m_externalClockHz << "Hz, SYNCR=$" << MCHEXN(syncr,4) << ", W=" << w << ", X=" << x << ", Y=" << y );

		m_systemClockHz = hz;

		initTimer();
	}

	void Sim::logChipSelectPinAssignments(uint16_t _val, int _index, int _count)
	{
		for(int i=0; i<_count; ++i)
		{
			const auto v = (_val >> (i<<1) & 0x3);
			const auto idx = i + _index;

			const char* name = g_csParFields[idx];
			const char* out = nullptr;

			switch (v)
			{
			case 0:		out = g_csParDiscretes[idx];	break;
			case 1:		out = g_csParAlternates[idx];	break;
			case 2:
			case 3:		out = g_csParSignals[idx];		break;
			}
			MCLOG("CSPAR" << ((idx < 7) ? "0" : "1") << ": " << name << " = " << out);
		}
	}

	void Sim::logChipSelectBaseAddressRegister(const uint32_t _index, uint16_t _val)
	{
		const auto baseAddr = (_val & ~7) << 8;
		uint32_t bs = 0;
		switch (_val & 7)
		{
		case 0b000:	bs = 2 * 1024;		break;
		case 0b001:	bs = 8 * 1024;		break;
		case 0b010:	bs = 16 * 1024;		break;
		case 0b011:	bs = 64 * 1024;		break;
		case 0b100:	bs = 128 * 1024;	break;
		case 0b101:	bs = 256 * 1024;	break;
		case 0b110:	bs = 512 * 1024;	break;
		case 0b111:	bs = 1024 * 1024;	break;
		}

		MCLOG("CSBAR" << (_index == 0 ? "BT" : std::to_string(_index-1)) << ", baseAddr=$" << MCHEXN(baseAddr, 1) << ", blockSize=$" << MCHEXN(bs, 1));
	}

	void Sim::logChipSelectOptionRegister(uint32_t _index, uint16_t _val)
	{
		const auto avec = _val & 1;
		const auto ipl = (_val >> 1) & 7;
		const auto space = (_val >> 4) & 3;
		const auto dsack = (_val >> 6) & 15;
		const auto strb = (_val >> 10) & 1;
		const auto rw = (_val >> 11) & 3;
		const auto byte = (_val >> 13) & 3;
		const auto mode = (_val >> 15) & 1;

		const char* sAvec = avec ? "On" : "Off";
		const std::string sIpl = ipl ? "Priority " + std::to_string(ipl) : "All";

		const char* sSpace = "";
		switch (space)
		{
		case 0:	sSpace = "CPU SP";	break;
		case 1:	sSpace = "User SP";	break;
		case 2:	sSpace = "Supv SP";	break;
		case 3:	sSpace = "S/U SP";	break;
		}

		std::string sDsack;

		switch (dsack)
		{
		case 0b1110:	sDsack = "F term";		break;
		case 0b1111:	sDsack = "External";	break;
		default:		sDsack = std::to_string(dsack) + " Wait";
		}

		const char* sStrb = strb ? "DS" : "AS";

		const char* sRw = "";
		switch (rw)
		{
		case 0: sRw = "Reserved";	break;
		case 1: sRw = "Read";	break;
		case 2: sRw = "Write";	break;
		case 3: sRw = "R&W";	break;
		}

		const char* sByte = "";
		switch (byte)
		{
		case 0:	sByte = "Disable";	break;
		case 1:	sByte = "Lower";	break;
		case 2:	sByte = "Upper";	break;
		case 3:	sByte = "Both";		break;
		}

		const char* sMode = mode ? "Sync" : "Async";

		MCLOG("CSOR" << (_index == 0 ? "BT" : std::to_string(_index-1)) << ": AVEC=" << sAvec << ", IPL=" << sIpl << ", SPACE=" << sSpace << ", DSACK=" << sDsack << ", STRB=" << sStrb << ", R/W=" << sRw << ", BYTE=" << sByte << ", MODE=" << sMode);
	}
}
