#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace matrixLib
{
	// MC6850 ACIA at byte level: one frame takes a fixed number of CPU cycles
	class Acia6850
	{
	public:
		explicit Acia6850(uint32_t _cyclesPerByte) : m_cyclesPerByte(_cyclesPerByte) {}

		void reset();

		uint8_t read(uint8_t _reg, uint64_t _cycle);
		void write(uint8_t _reg, uint8_t _val, uint64_t _cycle);

		void advance(uint64_t _cycle);

		void pushRx(uint8_t _byte) { m_rxQueue.push_back(_byte); }
		bool rxQueueEmpty() const { return m_rxQueue.empty(); }
		size_t rxQueueSize() const { return m_rxQueue.size(); }

		void readTx(std::vector<uint8_t>& _dst) { _dst.insert(_dst.end(), m_txOut.begin(), m_txOut.end()); m_txOut.clear(); }

		bool getIrq(uint64_t _cycle) const;

		// cycle of the next internal event, ~0 if idle
		uint64_t nextEvent() const;

		// CTS input level, switching from _before to _after at cycle _at
		void setCts(bool _before, uint64_t _at, bool _after) { m_ctsBefore = _before; m_ctsAt = _at; m_ctsAfter = _after; }
		void setCyclesPerByte(uint32_t _c) { m_cyclesPerByte = _c; }

	private:
		void startTx(uint64_t _cycle);
		bool cts(uint64_t _cycle) const { return _cycle >= m_ctsAt ? m_ctsAfter : m_ctsBefore; }

		uint32_t m_cyclesPerByte;

		uint8_t m_control = 0;
		bool m_masterReset = true;

		bool m_rdrf = false;
		bool m_ovrn = false;
		uint8_t m_rdr = 0;
		uint64_t m_nextRxAt = 0;

		bool m_tdre = true;
		bool m_txShifting = false;
		uint8_t m_tdr = 0;
		uint8_t m_tsr = 0;
		uint64_t m_txDoneAt = 0;

		bool m_ctsBefore = false;
		bool m_ctsAfter = false;
		uint64_t m_ctsAt = 0;

		std::deque<uint8_t> m_rxQueue;
		std::vector<uint8_t> m_txOut;
	};
}
