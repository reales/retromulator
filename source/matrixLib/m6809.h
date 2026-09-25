#pragma once

#include <cstdint>

namespace matrixLib
{
	class M6809Bus
	{
	public:
		virtual ~M6809Bus() = default;
		virtual uint8_t read(uint16_t _addr) = 0;
		virtual void write(uint16_t _addr, uint8_t _val) = 0;
	};

	// Instruction-accurate MC6809 with per-instruction cycle counts
	class M6809
	{
	public:
		enum CCFlag : uint8_t
		{
			CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08,
			CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80
		};

		explicit M6809(M6809Bus& _bus) : m_bus(_bus) {}

		void reset();

		// executes one instruction or interrupt entry, returns cycles used
		uint32_t step();

		void setIrq(bool _active) { m_irqLine = _active; }
		void setFirq(bool _active) { m_firqLine = _active; }
		void setNmi(bool _active);

		uint16_t getPC() const { return m_pc; }
		uint64_t getCycles() const { return m_cycles; }

		struct Regs
		{
			uint8_t a, b, dp, cc;
			uint16_t x, y, u, s, pc;
		};
		Regs getRegs() const { return {A(), B(), m_dp, m_cc, m_x, m_y, m_u, m_s, m_pc}; }
		void setRegs(const Regs& _r);

	private:
		uint8_t A() const { return static_cast<uint8_t>(m_d >> 8); }
		uint8_t B() const { return static_cast<uint8_t>(m_d); }
		void setA(uint8_t _v) { m_d = static_cast<uint16_t>((m_d & 0x00ff) | (_v << 8)); }
		void setB(uint8_t _v) { m_d = static_cast<uint16_t>((m_d & 0xff00) | _v); }

		uint8_t rd(uint16_t _a) { return m_bus.read(_a); }
		void wr(uint16_t _a, uint8_t _v) { m_bus.write(_a, _v); }
		uint16_t rd16(uint16_t _a) { return static_cast<uint16_t>((rd(_a) << 8) | rd(static_cast<uint16_t>(_a + 1))); }
		void wr16(uint16_t _a, uint16_t _v) { wr(_a, static_cast<uint8_t>(_v >> 8)); wr(static_cast<uint16_t>(_a + 1), static_cast<uint8_t>(_v)); }

		uint8_t fetch() { return rd(m_pc++); }
		uint16_t fetch16() { const uint16_t v = rd16(m_pc); m_pc += 2; return v; }

		void push8(uint16_t& _sp, uint8_t _v) { wr(--_sp, _v); }
		void push16(uint16_t& _sp, uint16_t _v) { wr(--_sp, static_cast<uint8_t>(_v)); wr(--_sp, static_cast<uint8_t>(_v >> 8)); }
		uint8_t pull8(uint16_t& _sp) { return rd(_sp++); }
		uint16_t pull16(uint16_t& _sp) { const uint8_t h = rd(_sp++); return static_cast<uint16_t>((h << 8) | rd(_sp++)); }

		uint16_t eaDirect() { return static_cast<uint16_t>((m_dp << 8) | fetch()); }
		uint16_t eaExtended() { return fetch16(); }
		uint16_t eaIndexed();
		uint16_t& indexReg(uint8_t _postbyte);

		void pushAll();
		bool checkInterrupts();

		void setNZ8(uint8_t _v) { m_cc = static_cast<uint8_t>((m_cc & ~(CC_N | CC_Z)) | (_v & 0x80 ? CC_N : 0) | (_v ? 0 : CC_Z)); }
		void setNZ16(uint16_t _v) { m_cc = static_cast<uint8_t>((m_cc & ~(CC_N | CC_Z)) | (_v & 0x8000 ? CC_N : 0) | (_v ? 0 : CC_Z)); }
		void setFlag(uint8_t _f, bool _on) { m_cc = _on ? (m_cc | _f) : (m_cc & ~_f); }

		uint8_t add8(uint8_t _a, uint8_t _b, uint8_t _carry);
		uint8_t sub8(uint8_t _a, uint8_t _b, uint8_t _carry);
		uint16_t add16(uint16_t _a, uint16_t _b);
		uint16_t sub16(uint16_t _a, uint16_t _b);

		// read-modify-write group (NEG..CLR), returns true if the result is written
		uint8_t rmw(uint8_t _op, uint8_t _v, bool& _write);

		bool branchCond(uint8_t _op) const;

		uint16_t readTfrReg(uint8_t _code) const;
		void writeTfrReg(uint8_t _code, uint16_t _v);

		uint32_t execPage0(uint8_t _op);
		uint32_t execPage2(uint8_t _op);
		uint32_t execPage3(uint8_t _op);

		M6809Bus& m_bus;

		uint16_t m_d = 0, m_x = 0, m_y = 0, m_u = 0, m_s = 0, m_pc = 0;
		uint8_t m_dp = 0, m_cc = 0;

		bool m_irqLine = false;
		bool m_firqLine = false;
		bool m_nmiLine = false;
		bool m_nmiPending = false;
		bool m_nmiArmed = false;

		bool m_cwai = false;
		bool m_sync = false;

		uint32_t m_extraCycles = 0;
		uint64_t m_cycles = 0;
	};
}
