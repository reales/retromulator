#include "m6809.h"

namespace matrixLib
{
	namespace
	{
		constexpr uint16_t VEC_SWI3 = 0xfff2;
		constexpr uint16_t VEC_SWI2 = 0xfff4;
		constexpr uint16_t VEC_FIRQ = 0xfff6;
		constexpr uint16_t VEC_IRQ  = 0xfff8;
		constexpr uint16_t VEC_SWI  = 0xfffa;
		constexpr uint16_t VEC_NMI  = 0xfffc;
		constexpr uint16_t VEC_RST  = 0xfffe;

		int8_t s8(uint8_t _v) { return static_cast<int8_t>(_v); }
	}

	void M6809::reset()
	{
		m_dp = 0;
		m_cc = CC_I | CC_F;
		m_cwai = m_sync = false;
		m_nmiArmed = m_nmiPending = false;
		m_pc = rd16(VEC_RST);
	}

	void M6809::setNmi(const bool _active)
	{
		if(_active && !m_nmiLine)
			m_nmiPending = true;
		m_nmiLine = _active;
	}

	void M6809::setRegs(const Regs& _r)
	{
		setA(_r.a); setB(_r.b);
		m_dp = _r.dp; m_cc = _r.cc;
		m_x = _r.x; m_y = _r.y; m_u = _r.u; m_s = _r.s; m_pc = _r.pc;
	}

	uint16_t& M6809::indexReg(const uint8_t _postbyte)
	{
		switch((_postbyte >> 5) & 3)
		{
		case 0:  return m_x;
		case 1:  return m_y;
		case 2:  return m_u;
		default: return m_s;
		}
	}

	uint16_t M6809::eaIndexed()
	{
		const uint8_t pb = fetch();
		uint16_t& r = indexReg(pb);

		if(!(pb & 0x80))
		{
			int off = pb & 0x1f;
			if(off & 0x10)
				off -= 0x20;
			m_extraCycles += 1;
			return static_cast<uint16_t>(r + off);
		}

		uint16_t ea = 0;
		switch(pb & 0x0f)
		{
		case 0x0: ea = r; r += 1; m_extraCycles += 2; break;
		case 0x1: ea = r; r += 2; m_extraCycles += 3; break;
		case 0x2: r -= 1; ea = r; m_extraCycles += 2; break;
		case 0x3: r -= 2; ea = r; m_extraCycles += 3; break;
		case 0x4: ea = r; break;
		case 0x5: ea = static_cast<uint16_t>(r + s8(B())); m_extraCycles += 1; break;
		case 0x6: ea = static_cast<uint16_t>(r + s8(A())); m_extraCycles += 1; break;
		case 0x8: ea = static_cast<uint16_t>(r + s8(fetch())); m_extraCycles += 1; break;
		case 0x9: ea = static_cast<uint16_t>(r + fetch16()); m_extraCycles += 4; break;
		case 0xb: ea = static_cast<uint16_t>(r + m_d); m_extraCycles += 4; break;
		case 0xc: { const int8_t o = s8(fetch()); ea = static_cast<uint16_t>(m_pc + o); m_extraCycles += 1; break; }
		case 0xd: { const uint16_t o = fetch16(); ea = static_cast<uint16_t>(m_pc + o); m_extraCycles += 5; break; }
		case 0xf: ea = fetch16(); m_extraCycles += 2; break;
		default:  ea = r; break;
		}

		if(pb & 0x10)
		{
			ea = rd16(ea);
			m_extraCycles += 3;
		}
		return ea;
	}

	uint8_t M6809::add8(const uint8_t _a, const uint8_t _b, const uint8_t _carry)
	{
		const uint32_t r = _a + _b + _carry;
		m_cc &= ~(CC_H | CC_N | CC_Z | CC_V | CC_C);
		if((_a ^ _b ^ r) & 0x10) m_cc |= CC_H;
		if((_a ^ _b ^ r ^ (r >> 1)) & 0x80) m_cc |= CC_V;
		if(r & 0x100) m_cc |= CC_C;
		setNZ8(static_cast<uint8_t>(r));
		return static_cast<uint8_t>(r);
	}

	uint8_t M6809::sub8(const uint8_t _a, const uint8_t _b, const uint8_t _carry)
	{
		const uint32_t r = static_cast<uint32_t>(_a) - _b - _carry;
		m_cc &= ~(CC_N | CC_Z | CC_V | CC_C);
		if((_a ^ _b) & (_a ^ r) & 0x80) m_cc |= CC_V;
		if(r & 0x100) m_cc |= CC_C;
		setNZ8(static_cast<uint8_t>(r));
		return static_cast<uint8_t>(r);
	}

	uint16_t M6809::add16(const uint16_t _a, const uint16_t _b)
	{
		const uint32_t r = static_cast<uint32_t>(_a) + _b;
		m_cc &= ~(CC_N | CC_Z | CC_V | CC_C);
		if((_a ^ _b ^ r ^ (r >> 1)) & 0x8000) m_cc |= CC_V;
		if(r & 0x10000) m_cc |= CC_C;
		setNZ16(static_cast<uint16_t>(r));
		return static_cast<uint16_t>(r);
	}

	uint16_t M6809::sub16(const uint16_t _a, const uint16_t _b)
	{
		const uint32_t r = static_cast<uint32_t>(_a) - _b;
		m_cc &= ~(CC_N | CC_Z | CC_V | CC_C);
		if((_a ^ _b) & (_a ^ r) & 0x8000) m_cc |= CC_V;
		if(r & 0x10000) m_cc |= CC_C;
		setNZ16(static_cast<uint16_t>(r));
		return static_cast<uint16_t>(r);
	}

	uint8_t M6809::rmw(const uint8_t _op, const uint8_t _v, bool& _write)
	{
		_write = true;
		uint8_t r = 0;
		switch(_op & 0x0f)
		{
		case 0x0: case 0x1:
			r = sub8(0, _v, 0);
			break;
		case 0x2: case 0x3:
			r = static_cast<uint8_t>(~_v);
			setNZ8(r);
			m_cc = static_cast<uint8_t>((m_cc & ~CC_V) | CC_C);
			break;
		case 0x4: case 0x5:
			r = _v >> 1;
			setFlag(CC_C, _v & 1);
			setNZ8(r);
			break;
		case 0x6:
			r = static_cast<uint8_t>((_v >> 1) | (m_cc & CC_C ? 0x80 : 0));
			setFlag(CC_C, _v & 1);
			setNZ8(r);
			break;
		case 0x7:
			r = static_cast<uint8_t>((_v >> 1) | (_v & 0x80));
			setFlag(CC_C, _v & 1);
			setNZ8(r);
			break;
		case 0x8:
			r = static_cast<uint8_t>(_v << 1);
			setFlag(CC_C, _v & 0x80);
			setFlag(CC_V, (_v ^ (_v << 1)) & 0x80);
			setNZ8(r);
			break;
		case 0x9:
			r = static_cast<uint8_t>((_v << 1) | (m_cc & CC_C ? 1 : 0));
			setFlag(CC_C, _v & 0x80);
			setFlag(CC_V, (_v ^ (_v << 1)) & 0x80);
			setNZ8(r);
			break;
		case 0xa: case 0xb:
			r = static_cast<uint8_t>(_v - 1);
			setFlag(CC_V, _v == 0x80);
			setNZ8(r);
			break;
		case 0xc:
			r = static_cast<uint8_t>(_v + 1);
			setFlag(CC_V, _v == 0x7f);
			setNZ8(r);
			break;
		case 0xd:
			setNZ8(_v);
			m_cc &= ~CC_V;
			_write = false;
			r = _v;
			break;
		case 0xf:
			r = 0;
			m_cc = static_cast<uint8_t>((m_cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
			break;
		default:
			_write = false;
			r = _v;
			break;
		}
		return r;
	}

	bool M6809::branchCond(const uint8_t _op) const
	{
		const bool c = m_cc & CC_C, v = m_cc & CC_V, z = m_cc & CC_Z, n = m_cc & CC_N;
		switch(_op & 0x0f)
		{
		case 0x0: return true;
		case 0x1: return false;
		case 0x2: return !(c || z);
		case 0x3: return c || z;
		case 0x4: return !c;
		case 0x5: return c;
		case 0x6: return !z;
		case 0x7: return z;
		case 0x8: return !v;
		case 0x9: return v;
		case 0xa: return !n;
		case 0xb: return n;
		case 0xc: return n == v;
		case 0xd: return n != v;
		case 0xe: return !z && n == v;
		default:  return z || n != v;
		}
	}

	uint16_t M6809::readTfrReg(const uint8_t _code) const
	{
		switch(_code)
		{
		case 0x0: return m_d;
		case 0x1: return m_x;
		case 0x2: return m_y;
		case 0x3: return m_u;
		case 0x4: return m_s;
		case 0x5: return m_pc;
		case 0x8: return static_cast<uint16_t>(0xff00 | A());
		case 0x9: return static_cast<uint16_t>(0xff00 | B());
		case 0xa: return static_cast<uint16_t>(0xff00 | m_cc);
		case 0xb: return static_cast<uint16_t>(0xff00 | m_dp);
		default:  return 0xffff;
		}
	}

	void M6809::writeTfrReg(const uint8_t _code, const uint16_t _v)
	{
		switch(_code)
		{
		case 0x0: m_d = _v; break;
		case 0x1: m_x = _v; break;
		case 0x2: m_y = _v; break;
		case 0x3: m_u = _v; break;
		case 0x4: m_s = _v; m_nmiArmed = true; break;
		case 0x5: m_pc = _v; break;
		case 0x8: setA(static_cast<uint8_t>(_v)); break;
		case 0x9: setB(static_cast<uint8_t>(_v)); break;
		case 0xa: m_cc = static_cast<uint8_t>(_v); break;
		case 0xb: m_dp = static_cast<uint8_t>(_v); break;
		default: break;
		}
	}

	void M6809::pushAll()
	{
		push16(m_s, m_pc);
		push16(m_s, m_u);
		push16(m_s, m_y);
		push16(m_s, m_x);
		push8(m_s, m_dp);
		push8(m_s, B());
		push8(m_s, A());
		push8(m_s, m_cc);
	}

	bool M6809::checkInterrupts()
	{
		if(m_nmiPending && m_nmiArmed)
		{
			m_nmiPending = false;
			if(!m_cwai)
			{
				m_cc |= CC_E;
				pushAll();
			}
			m_cc |= CC_I | CC_F;
			m_pc = rd16(VEC_NMI);
			m_cwai = m_sync = false;
			m_extraCycles += 19;
			return true;
		}

		if(m_firqLine && !(m_cc & CC_F))
		{
			if(!m_cwai)
			{
				m_cc &= ~CC_E;
				push16(m_s, m_pc);
				push8(m_s, m_cc);
			}
			m_cc |= CC_I | CC_F;
			m_pc = rd16(VEC_FIRQ);
			m_cwai = m_sync = false;
			m_extraCycles += 10;
			return true;
		}

		if(m_irqLine && !(m_cc & CC_I))
		{
			if(!m_cwai)
			{
				m_cc |= CC_E;
				pushAll();
			}
			m_cc |= CC_I;
			m_pc = rd16(VEC_IRQ);
			m_cwai = m_sync = false;
			m_extraCycles += 19;
			return true;
		}
		return false;
	}

	uint32_t M6809::step()
	{
		m_extraCycles = 0;

		if(checkInterrupts())
		{
			m_cycles += m_extraCycles;
			return m_extraCycles;
		}

		if(m_cwai)
		{
			m_cycles += 1;
			return 1;
		}

		if(m_sync)
		{
			if(m_irqLine || m_firqLine || m_nmiPending)
				m_sync = false;
			m_cycles += 1;
			return 1;
		}

		const uint8_t op = fetch();
		uint32_t c;
		if(op == 0x10)
			c = execPage2(fetch());
		else if(op == 0x11)
			c = execPage3(fetch());
		else
			c = execPage0(op);

		c += m_extraCycles;
		m_cycles += c;
		return c;
	}

	uint32_t M6809::execPage0(const uint8_t _op)
	{
		const uint8_t hi = _op & 0xf0;
		const uint8_t lo = _op & 0x0f;

		// read-modify-write group
		if(hi == 0x00 || hi == 0x60 || hi == 0x70)
		{
			uint16_t ea;
			uint32_t cyc;
			if(hi == 0x00)      { ea = eaDirect();   cyc = 6; }
			else if(hi == 0x60) { ea = eaIndexed();  cyc = 6; }
			else                { ea = eaExtended(); cyc = 7; }

			if(lo == 0xe)
			{
				m_pc = ea;
				return cyc - 3;
			}
			bool write;
			const uint8_t r = rmw(_op, rd(ea), write);
			if(write)
				wr(ea, r);
			return cyc;
		}

		if(hi == 0x40 || hi == 0x50)
		{
			bool write;
			if(hi == 0x40) setA(rmw(_op, A(), write));
			else           setB(rmw(_op, B(), write));
			return 2;
		}

		if(hi == 0x20)
		{
			const int8_t off = s8(fetch());
			if(branchCond(_op))
				m_pc = static_cast<uint16_t>(m_pc + off);
			return 3;
		}

		if(hi == 0x10)
		{
			switch(_op)
			{
			case 0x12: return 2;
			case 0x13: m_sync = true; return 4;
			case 0x16: { const uint16_t o = fetch16(); m_pc = static_cast<uint16_t>(m_pc + o); return 5; }
			case 0x17: { const uint16_t o = fetch16(); push16(m_s, m_pc); m_pc = static_cast<uint16_t>(m_pc + o); return 9; }
			case 0x19:
			{
				const uint8_t a = A();
				const uint8_t msn = a & 0xf0, lsn = a & 0x0f;
				uint16_t cf = 0;
				if(lsn > 0x09 || (m_cc & CC_H)) cf |= 0x06;
				if(msn > 0x80 && lsn > 0x09) cf |= 0x60;
				if(msn > 0x90 || (m_cc & CC_C)) cf |= 0x60;
				const uint16_t t = static_cast<uint16_t>(cf + a);
				m_cc &= ~(CC_N | CC_Z | CC_V);
				if(t & 0x100) m_cc |= CC_C;
				setA(static_cast<uint8_t>(t));
				setNZ8(A());
				return 2;
			}
			case 0x1a: m_cc |= fetch(); return 3;
			case 0x1c: m_cc &= fetch(); return 3;
			case 0x1d:
				setA(B() & 0x80 ? 0xff : 0x00);
				setNZ16(m_d);
				m_cc &= ~CC_V;
				return 2;
			case 0x1e:
			{
				const uint8_t pb = fetch();
				const uint16_t a = readTfrReg(pb >> 4), b = readTfrReg(pb & 0x0f);
				writeTfrReg(pb >> 4, b);
				writeTfrReg(pb & 0x0f, a);
				return 8;
			}
			case 0x1f:
			{
				const uint8_t pb = fetch();
				writeTfrReg(pb & 0x0f, readTfrReg(pb >> 4));
				return 6;
			}
			default: return 2;
			}
		}

		if(hi == 0x30)
		{
			switch(_op)
			{
			case 0x30: m_x = eaIndexed(); setFlag(CC_Z, m_x == 0); return 4;
			case 0x31: m_y = eaIndexed(); setFlag(CC_Z, m_y == 0); return 4;
			case 0x32: m_s = eaIndexed(); m_nmiArmed = true; return 4;
			case 0x33: m_u = eaIndexed(); return 4;
			case 0x34: case 0x36:
			{
				const uint8_t pb = fetch();
				uint16_t& sp = _op == 0x34 ? m_s : m_u;
				const uint16_t other = _op == 0x34 ? m_u : m_s;
				uint32_t n = 0;
				if(pb & 0x80) { push16(sp, m_pc); n += 2; }
				if(pb & 0x40) { push16(sp, other); n += 2; }
				if(pb & 0x20) { push16(sp, m_y); n += 2; }
				if(pb & 0x10) { push16(sp, m_x); n += 2; }
				if(pb & 0x08) { push8(sp, m_dp); n += 1; }
				if(pb & 0x04) { push8(sp, B()); n += 1; }
				if(pb & 0x02) { push8(sp, A()); n += 1; }
				if(pb & 0x01) { push8(sp, m_cc); n += 1; }
				return 5 + n;
			}
			case 0x35: case 0x37:
			{
				const uint8_t pb = fetch();
				uint16_t& sp = _op == 0x35 ? m_s : m_u;
				uint16_t& other = _op == 0x35 ? m_u : m_s;
				uint32_t n = 0;
				if(pb & 0x01) { m_cc = pull8(sp); n += 1; }
				if(pb & 0x02) { setA(pull8(sp)); n += 1; }
				if(pb & 0x04) { setB(pull8(sp)); n += 1; }
				if(pb & 0x08) { m_dp = pull8(sp); n += 1; }
				if(pb & 0x10) { m_x = pull16(sp); n += 2; }
				if(pb & 0x20) { m_y = pull16(sp); n += 2; }
				if(pb & 0x40) { other = pull16(sp); n += 2; }
				if(pb & 0x80) { m_pc = pull16(sp); n += 2; }
				return 5 + n;
			}
			case 0x39: m_pc = pull16(m_s); return 5;
			case 0x3a: m_x = static_cast<uint16_t>(m_x + B()); return 3;
			case 0x3b:
				m_cc = pull8(m_s);
				if(m_cc & CC_E)
				{
					setA(pull8(m_s));
					setB(pull8(m_s));
					m_dp = pull8(m_s);
					m_x = pull16(m_s);
					m_y = pull16(m_s);
					m_u = pull16(m_s);
					m_pc = pull16(m_s);
					return 15;
				}
				m_pc = pull16(m_s);
				return 6;
			case 0x3c:
				m_cc &= fetch();
				m_cc |= CC_E;
				pushAll();
				m_cwai = true;
				return 20;
			case 0x3d:
				m_d = static_cast<uint16_t>(A() * B());
				setFlag(CC_Z, m_d == 0);
				setFlag(CC_C, m_d & 0x80);
				return 11;
			case 0x3f:
				m_cc |= CC_E;
				pushAll();
				m_cc |= CC_I | CC_F;
				m_pc = rd16(VEC_SWI);
				return 19;
			default: return 2;
			}
		}

		// 0x80..0xff: accumulator / 16-bit register group
		const bool isB = _op >= 0xc0;
		const uint8_t mode = (_op >> 4) & 3;   // 0 imm, 1 dir, 2 idx, 3 ext

		auto ea = [&]() -> uint16_t
		{
			switch(mode)
			{
			case 1:  return eaDirect();
			case 2:  return eaIndexed();
			default: return eaExtended();
			}
		};

		// 16-bit operations
		if(lo == 0x3 || lo >= 0xc)
		{
			static constexpr uint32_t cycArith[4] = {4, 6, 6, 7};
			static constexpr uint32_t cycLoad[4] = {3, 5, 5, 6};

			auto operand16 = [&]() -> uint16_t { return mode == 0 ? fetch16() : rd16(ea()); };

			switch(_op & 0x4f)
			{
			case 0x03: m_d = sub16(m_d, operand16()); return cycArith[mode];     // SUBD
			case 0x43: m_d = add16(m_d, operand16()); return cycArith[mode];     // ADDD
			case 0x0c: sub16(m_x, operand16()); return cycArith[mode];           // CMPX
			case 0x4c: m_d = operand16(); setNZ16(m_d); m_cc &= ~CC_V; return cycLoad[mode];  // LDD
			case 0x0d:                                                          // BSR / JSR
			{
				if(mode == 0)
				{
					const int8_t off = s8(fetch());
					push16(m_s, m_pc);
					m_pc = static_cast<uint16_t>(m_pc + off);
					return 7;
				}
				const uint16_t t = ea();
				push16(m_s, m_pc);
				m_pc = t;
				return mode == 3 ? 8 : 7;
			}
			case 0x4d:                                                          // STD
			{
				const uint16_t a = ea();
				wr16(a, m_d); setNZ16(m_d); m_cc &= ~CC_V;
				return cycLoad[mode];
			}
			case 0x0e: case 0x4e:                                               // LDX / LDU
			{
				uint16_t& r = _op & 0x40 ? m_u : m_x;
				r = operand16(); setNZ16(r); m_cc &= ~CC_V;
				return cycLoad[mode];
			}
			case 0x0f: case 0x4f:                                               // STX / STU
			{
				const uint16_t r = _op & 0x40 ? m_u : m_x;
				const uint16_t a = ea();
				wr16(a, r); setNZ16(r); m_cc &= ~CC_V;
				return cycLoad[mode];
			}
			default: return 2;
			}
		}

		static constexpr uint32_t cyc8[4] = {2, 4, 4, 5};

		if(lo == 0x7)
		{
			if(mode == 0)
				return 2;
			const uint8_t v = isB ? B() : A();
			wr(ea(), v);
			setNZ8(v); m_cc &= ~CC_V;
			return cyc8[mode];
		}

		const uint8_t m = mode == 0 ? fetch() : rd(ea());
		const uint8_t acc = isB ? B() : A();
		uint8_t r = acc;
		bool store = true;

		switch(lo)
		{
		case 0x0: r = sub8(acc, m, 0); break;
		case 0x1: sub8(acc, m, 0); store = false; break;
		case 0x2: r = sub8(acc, m, m_cc & CC_C ? 1 : 0); break;
		case 0x4: r = acc & m; setNZ8(r); m_cc &= ~CC_V; break;
		case 0x5: setNZ8(acc & m); m_cc &= ~CC_V; store = false; break;
		case 0x6: r = m; setNZ8(r); m_cc &= ~CC_V; break;
		case 0x8: r = acc ^ m; setNZ8(r); m_cc &= ~CC_V; break;
		case 0x9: r = add8(acc, m, m_cc & CC_C ? 1 : 0); break;
		case 0xa: r = acc | m; setNZ8(r); m_cc &= ~CC_V; break;
		case 0xb: r = add8(acc, m, 0); break;
		default: store = false; break;
		}

		if(store)
		{
			if(isB) setB(r);
			else    setA(r);
		}
		return cyc8[mode];
	}

	uint32_t M6809::execPage2(const uint8_t _op)
	{
		if(_op >= 0x21 && _op <= 0x2f)
		{
			const uint16_t off = fetch16();
			if(branchCond(_op))
			{
				m_pc = static_cast<uint16_t>(m_pc + off);
				return 6;
			}
			return 5;
		}

		if(_op == 0x3f)
		{
			m_cc |= CC_E;
			pushAll();
			m_pc = rd16(VEC_SWI2);
			return 20;
		}

		const uint8_t mode = (_op >> 4) & 3;
		auto ea = [&]() -> uint16_t
		{
			switch(mode)
			{
			case 1:  return eaDirect();
			case 2:  return eaIndexed();
			default: return eaExtended();
			}
		};
		auto operand16 = [&]() -> uint16_t { return mode == 0 ? fetch16() : rd16(ea()); };

		static constexpr uint32_t cycCmp[4] = {5, 7, 7, 8};
		static constexpr uint32_t cycLd[4] = {4, 6, 6, 7};

		switch(_op)
		{
		case 0x83: case 0x93: case 0xa3: case 0xb3: sub16(m_d, operand16()); return cycCmp[mode];
		case 0x8c: case 0x9c: case 0xac: case 0xbc: sub16(m_y, operand16()); return cycCmp[mode];
		case 0x8e: case 0x9e: case 0xae: case 0xbe: m_y = operand16(); setNZ16(m_y); m_cc &= ~CC_V; return cycLd[mode];
		case 0x9f: case 0xaf: case 0xbf: { const uint16_t a = ea(); wr16(a, m_y); setNZ16(m_y); m_cc &= ~CC_V; return cycLd[mode]; }
		case 0xce: case 0xde: case 0xee: case 0xfe: m_s = operand16(); m_nmiArmed = true; setNZ16(m_s); m_cc &= ~CC_V; return cycLd[mode];
		case 0xdf: case 0xef: case 0xff: { const uint16_t a = ea(); wr16(a, m_s); setNZ16(m_s); m_cc &= ~CC_V; return cycLd[mode]; }
		default: return 2;
		}
	}

	uint32_t M6809::execPage3(const uint8_t _op)
	{
		if(_op == 0x3f)
		{
			m_cc |= CC_E;
			pushAll();
			m_pc = rd16(VEC_SWI3);
			return 20;
		}

		const uint8_t mode = (_op >> 4) & 3;
		auto ea = [&]() -> uint16_t
		{
			switch(mode)
			{
			case 1:  return eaDirect();
			case 2:  return eaIndexed();
			default: return eaExtended();
			}
		};
		auto operand16 = [&]() -> uint16_t { return mode == 0 ? fetch16() : rd16(ea()); };

		static constexpr uint32_t cycCmp[4] = {5, 7, 7, 8};

		switch(_op)
		{
		case 0x83: case 0x93: case 0xa3: case 0xb3: sub16(m_u, operand16()); return cycCmp[mode];
		case 0x8c: case 0x9c: case 0xac: case 0xbc: sub16(m_s, operand16()); return cycCmp[mode];
		default: return 2;
		}
	}
}
