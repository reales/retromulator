#include "acia6850.h"

namespace matrixLib
{
	void Acia6850::reset()
	{
		m_control = 0;
		m_masterReset = true;
		m_rdrf = m_ovrn = false;
		m_tdre = true;
		m_txShifting = false;
		m_rxQueue.clear();
		m_txOut.clear();
	}

	bool Acia6850::getIrq() const
	{
		if(m_masterReset)
			return false;
		const bool rxIrq = (m_control & 0x80) && (m_rdrf || m_ovrn);
		const bool txIrq = ((m_control & 0x60) == 0x20) && m_tdre;
		return rxIrq || txIrq;
	}

	uint8_t Acia6850::read(const uint8_t _reg, const uint64_t _cycle)
	{
		if(!(_reg & 1))
		{
			const bool cts = _cycle >= m_ctsAt ? m_ctsAfter : m_ctsBefore;
			uint8_t s = 0;
			if(m_rdrf) s |= 0x01;
			if(m_tdre && !cts) s |= 0x02;
			if(cts) s |= 0x08;
			if(m_ovrn) s |= 0x20;
			if(getIrq()) s |= 0x80;
			return s;
		}

		m_rdrf = false;
		m_ovrn = false;
		return m_rdr;
	}

	void Acia6850::write(const uint8_t _reg, const uint8_t _val, const uint64_t _cycle)
	{
		if(!(_reg & 1))
		{
			m_control = _val;
			if((_val & 3) == 3)
			{
				m_masterReset = true;
				m_rdrf = m_ovrn = false;
				m_tdre = true;
				m_txShifting = false;
			}
			else if(m_masterReset)
			{
				m_masterReset = false;
				m_nextRxAt = _cycle + m_cyclesPerByte;
			}
			return;
		}

		m_tdr = _val;
		m_tdre = false;
		if(!m_txShifting)
		{
			m_tsr = m_tdr;
			m_tdre = true;
			m_txShifting = true;
			m_txDoneAt = _cycle + m_cyclesPerByte;
		}
	}

	void Acia6850::advance(const uint64_t _cycle)
	{
		if(m_masterReset)
			return;

		while(m_txShifting && _cycle >= m_txDoneAt)
		{
			m_txOut.push_back(m_tsr);
			if(!m_tdre)
			{
				m_tsr = m_tdr;
				m_tdre = true;
				m_txDoneAt += m_cyclesPerByte;
			}
			else
			{
				m_txShifting = false;
			}
		}

		// hold the next byte until the firmware has read the previous one instead of overrunning
		if(!m_rxQueue.empty() && _cycle >= m_nextRxAt && !m_rdrf)
		{
			m_rdr = m_rxQueue.front();
			m_rxQueue.pop_front();
			m_rdrf = true;
			m_nextRxAt = _cycle + m_cyclesPerByte;
		}
		else if(m_rxQueue.empty() && _cycle >= m_nextRxAt)
		{
			m_nextRxAt = _cycle;
		}
	}

	uint64_t Acia6850::nextEvent() const
	{
		uint64_t e = ~0ull;
		if(m_masterReset)
			return e;
		if(m_txShifting)
			e = m_txDoneAt;
		if(!m_rxQueue.empty() && m_nextRxAt < e)
			e = m_nextRxAt;
		return e;
	}
}
