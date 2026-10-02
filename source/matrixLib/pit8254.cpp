#include "pit8254.h"

namespace matrixLib
{
	void Pit8254::reset()
	{
		for(auto& c : m_counters)
			c = Counter();
	}

	void Pit8254::update(Counter& _c, const uint64_t _tick)
	{
		if(_c.hasPending && _tick >= _c.pendingAt)
		{
			_c.reload = _c.pendingReload;
			_c.loadedAt = _c.pendingAt;
			_c.phaseOfs = _c.pendingLow ? (effective(_c.reload) + 1) / 2 : 0;
			_c.hasPending = false;
		}
	}

	void Pit8254::loadCount(Counter& _c, const uint32_t _count, const uint64_t _tick)
	{
		++_c.writes;
		_c.nullCount = false;

		const bool reloading = (_c.mode == 2 || _c.mode == 3) && _c.running;
		if(!reloading)
			_c.phaseOfs = 0;

		switch(_c.mode)
		{
		case 0:
			_c.reload = _count;
			_c.loadedAt = _tick + 1;
			_c.running = true;
			_c.outIdle = false;
			_c.hasPending = false;
			break;
		case 4:
			_c.reload = _count;
			_c.loadedAt = _tick + 1;
			_c.running = true;
			_c.outIdle = true;
			_c.hasPending = false;
			break;
		case 2:
		case 3:
			if(!_c.running)
			{
				_c.reload = _count;
				_c.loadedAt = _tick + 1;
				_c.running = true;
				_c.hasPending = false;
			}
			else
			{
				// mode 2 takes a new count at the end of the period, mode 3 at the end of the half cycle
				update(_c, _tick);
				const uint64_t n = effective(_c.reload);
				const uint64_t half = (n + 1) / 2;
				const uint64_t phase = _tick >= _c.loadedAt ? (_tick - _c.loadedAt + _c.phaseOfs) % n : 0;
				const uint64_t base = (_tick >= _c.loadedAt ? _tick : _c.loadedAt) - phase;
				_c.pendingLow = _c.mode == 3 && phase < half;
				_c.pendingAt = base + (_c.pendingLow ? half : n);
				_c.pendingReload = _count;
				_c.hasPending = true;
			}
			break;
		default:
			// modes 1 and 5 wait for a GATE edge, which never comes with GATE tied high
			_c.reload = _count;
			_c.running = false;
			_c.outIdle = true;
			break;
		}
	}

	void Pit8254::gate(const uint32_t _counter, const uint64_t _lowAt, const uint64_t _highAt)
	{
		auto& c = m_counters[_counter];
		update(c, _lowAt);
		if(!c.running || (c.mode != 2 && c.mode != 3))
			return;
		if(c.hasPending)
		{
			c.reload = c.pendingReload;
			c.hasPending = false;
		}
		c.loadedAt = _highAt + 1;
		c.phaseOfs = 0;
	}

	void Pit8254::write(const uint8_t _reg, const uint8_t _val, const uint64_t _tick)
	{
		const uint8_t reg = _reg & 3;

		if(reg == 3)
		{
			const uint8_t sc = _val >> 6;

			if(sc == 3)
			{
				for(uint32_t i = 0; i < 3; ++i)
				{
					if(!(_val & (2 << i)))
						continue;
					auto& c = m_counters[i];
					if(!(_val & 0x20) && !c.countLatched)
					{
						c.latchedCount = getCount(i, _tick);
						c.countLatched = true;
						c.readMsbNext = false;
					}
					if(!(_val & 0x10) && !c.statusLatched)
					{
						c.latchedStatus = status(c, _tick);
						c.statusLatched = true;
					}
				}
				return;
			}

			auto& c = m_counters[sc];
			const uint8_t rw = (_val >> 4) & 3;

			if(rw == 0)
			{
				if(!c.countLatched)
				{
					c.latchedCount = getCount(sc, _tick);
					c.countLatched = true;
					c.readMsbNext = false;
				}
				return;
			}

			update(c, _tick);
			c.control = _val & 0x3f;
			c.rw = rw;
			c.mode = (_val >> 1) & 7;
			if(c.mode > 5)
				c.mode -= 4;
			c.writeMsbNext = false;
			c.readMsbNext = false;
			c.countLatched = false;
			c.statusLatched = false;
			c.running = false;
			c.hasPending = false;
			c.nullCount = true;
			c.outIdle = c.mode != 0;
			return;
		}

		auto& c = m_counters[reg];
		switch(c.rw)
		{
		case 1:
			loadCount(c, _val, _tick);
			break;
		case 2:
			loadCount(c, static_cast<uint32_t>(_val) << 8, _tick);
			break;
		default:
			if(!c.writeMsbNext)
			{
				c.writeLatch = _val;
				c.writeMsbNext = true;
				if(c.mode == 0)
				{
					// mode 0 stops counting after the first byte of a new count
					update(c, _tick);
					c.running = false;
					c.outIdle = false;
				}
			}
			else
			{
				c.writeMsbNext = false;
				loadCount(c, static_cast<uint32_t>(c.writeLatch) | (static_cast<uint32_t>(_val) << 8), _tick);
			}
			break;
		}
	}

	uint8_t Pit8254::read(const uint8_t _reg, const uint64_t _tick)
	{
		const uint8_t reg = _reg & 3;
		if(reg == 3)
			return 0xff;

		auto& c = m_counters[reg];

		if(c.statusLatched)
		{
			c.statusLatched = false;
			return c.latchedStatus;
		}

		const uint16_t v = c.countLatched ? c.latchedCount : getCount(reg, _tick);

		uint8_t res;
		switch(c.rw)
		{
		case 1:
			res = static_cast<uint8_t>(v);
			c.countLatched = false;
			break;
		case 2:
			res = static_cast<uint8_t>(v >> 8);
			c.countLatched = false;
			break;
		default:
			if(!c.readMsbNext)
			{
				res = static_cast<uint8_t>(v);
				c.readMsbNext = true;
			}
			else
			{
				res = static_cast<uint8_t>(v >> 8);
				c.readMsbNext = false;
				c.countLatched = false;
			}
			break;
		}
		return res;
	}

	uint8_t Pit8254::status(Counter& _c, const uint64_t _tick)
	{
		const auto idx = static_cast<uint32_t>(&_c - m_counters);
		return static_cast<uint8_t>((getOut(idx, _tick) ? 0x80 : 0) | (_c.nullCount ? 0x40 : 0) | _c.control);
	}

	bool Pit8254::getOut(const uint32_t _counter, const uint64_t _tick)
	{
		auto& c = m_counters[_counter];
		update(c, _tick);

		if(!c.running || _tick < c.loadedAt)
			return c.outIdle;

		const uint64_t n = effective(c.reload);
		const uint64_t elapsed = _tick - c.loadedAt + c.phaseOfs;

		switch(c.mode)
		{
		case 0:  return elapsed >= n;
		case 4:  return elapsed != n;
		case 2:  return (elapsed % n) != n - 1;
		case 3:  return (elapsed % n) < (n + 1) / 2;
		default: return c.outIdle;
		}
	}

	uint16_t Pit8254::getCount(const uint32_t _counter, const uint64_t _tick)
	{
		auto& c = m_counters[_counter];
		update(c, _tick);

		const uint64_t n = effective(c.reload);
		if(!c.running || _tick < c.loadedAt)
			return static_cast<uint16_t>(c.reload);

		const uint64_t elapsed = _tick - c.loadedAt + c.phaseOfs;

		switch(c.mode)
		{
		case 2:
			return static_cast<uint16_t>(n - (elapsed % n));
		case 3:
		{
			const uint64_t half = (n + 1) / 2;
			const uint64_t phase = elapsed % n;
			const uint64_t inHalf = phase < half ? phase : phase - half;
			return static_cast<uint16_t>((n & ~1ull) - 2 * inHalf);
		}
		default:
			return static_cast<uint16_t>(n - (elapsed & 0xffff));
		}
	}

	uint32_t Pit8254::getPeriod(const uint32_t _counter, const uint64_t _tick)
	{
		auto& c = m_counters[_counter];
		update(c, _tick);
		if(!c.running || (c.mode != 2 && c.mode != 3))
			return 0;
		return effective(c.reload);
	}

	uint64_t Pit8254::nextOutEdge(const uint32_t _counter, const uint64_t _tick)
	{
		auto& c = m_counters[_counter];
		update(c, _tick);

		constexpr uint64_t never = ~0ull;

		if(!c.running)
			return never;
		if(_tick < c.loadedAt)
			return c.mode == 0 ? c.loadedAt + effective(c.reload) : c.loadedAt;

		const uint64_t n = effective(c.reload);
		const uint64_t elapsed = _tick - c.loadedAt + c.phaseOfs;

		uint64_t edge = never;
		switch(c.mode)
		{
		case 0:
			edge = elapsed < n ? c.loadedAt + n : never;
			break;
		case 4:
			edge = elapsed < n ? c.loadedAt + n : (elapsed == n ? c.loadedAt + n + 1 : never);
			break;
		case 2:
		{
			const uint64_t phase = elapsed % n;
			const uint64_t base = _tick - phase;
			edge = phase < n - 1 ? base + n - 1 : base + n;
			break;
		}
		case 3:
		{
			const uint64_t half = (n + 1) / 2;
			const uint64_t phase = elapsed % n;
			const uint64_t base = _tick - phase;
			// a count of 1 never leaves the high half
			edge = half >= n ? never : (phase < half ? base + half : base + n);
			break;
		}
		default:
			break;
		}

		if(c.hasPending && edge > c.pendingAt)
			edge = c.pendingAt;
		return edge;
	}
}
