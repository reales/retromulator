#pragma once

#include <cstdint>

namespace matrixLib
{
	// Intel 82C54, evaluated lazily from the tick count of its clock input.
	// All three counters share one clock and have GATE tied high.
	class Pit8254
	{
	public:
		struct Counter
		{
			uint8_t control = 0;        // last control word, bits 5..0
			uint8_t mode = 0;
			uint8_t rw = 3;             // 1 LSB, 2 MSB, 3 LSB then MSB
			bool writeMsbNext = false;
			bool readMsbNext = false;
			uint8_t writeLatch = 0;

			uint32_t reload = 0;        // count in use, 0 means 65536
			uint32_t pendingReload = 0;
			uint64_t pendingAt = 0;
			bool hasPending = false;

			uint64_t loadedAt = 0;      // tick where counting started
			bool running = false;
			bool outIdle = true;        // OUT level while not running

			bool countLatched = false;
			uint16_t latchedCount = 0;
			bool statusLatched = false;
			uint8_t latchedStatus = 0;
			bool nullCount = true;
			uint32_t writes = 0;        // count register writes, for listeners
		};

		void reset();

		void write(uint8_t _reg, uint8_t _val, uint64_t _tick);
		uint8_t read(uint8_t _reg, uint64_t _tick);

		bool getOut(uint32_t _counter, uint64_t _tick);
		uint16_t getCount(uint32_t _counter, uint64_t _tick);

		// period in ticks of a counter running in mode 2 or 3, 0 if stopped
		uint32_t getPeriod(uint32_t _counter, uint64_t _tick);

		// tick of the next OUT transition after _tick, or ~0 if none is scheduled
		uint64_t nextOutEdge(uint32_t _counter, uint64_t _tick);

		const Counter& getCounter(uint32_t _counter) const { return m_counters[_counter]; }

	private:
		void update(Counter& _c, uint64_t _tick);
		void loadCount(Counter& _c, uint32_t _count, uint64_t _tick);
		uint8_t status(Counter& _c, uint64_t _tick);

		static uint32_t effective(uint32_t _reload) { return _reload ? _reload : 65536u; }

		Counter m_counters[3];
	};
}
