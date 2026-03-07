/**
 *  VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once
#include <cstdio>
#include <cstdint>
#include <functional>

// Portable endian detection
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
  // GCC/Clang built-in
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define DX7_LITTLE_ENDIAN 1
  #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    #define DX7_BIG_ENDIAN 1
  #endif
#elif defined(_WIN32) || defined(__x86_64__) || defined(__i386__) || defined(__aarch64__) || defined(__arm__)
  #define DX7_LITTLE_ENDIAN 1
#else
  #error "Cannot determine byte order"
#endif

namespace dx7Emu {

struct HD6303R {
	HD6303R() { initInst(); }
	virtual ~HD6303R() = default;

	// Registers
	union {
		uint16_t D=0;
#if defined(DX7_LITTLE_ENDIAN)
		struct { uint8_t B, A; };
#elif defined(DX7_BIG_ENDIAN)
		struct { uint8_t A, B; };
#endif
	};

	bool H, I, N, Z, V, C;
	uint16_t IX=0, SP=0;
	uint16_t PC=0x0000;

	// Memory
	uint8_t memory[0x10000] = {0};
	int pgmload(const uint8_t *data, size_t size);
	int memload(const uint8_t *data, size_t size, uint16_t addr);

	// Registers
	enum { WU, TE, TIE, RE, RIE, TDRE, ORFE, RDRF };
	enum { OLVL, IEDG, ETOI, EOCI, EICI, TOF, OCF, ICF };

	uint8_t &P1DDR  = memory[0x00];
	uint8_t &P2DDR  = memory[0x01];
	uint8_t &PORT1  = memory[0x02];
	uint8_t &PORT2  = memory[0x03];
	uint8_t &TCSR   = memory[0x08];
	uint8_t &FRCH   = memory[0x09];
	uint8_t &FRCL   = memory[0x0A];
	uint8_t &OCRH   = memory[0x0B];
	uint8_t &OCRL   = memory[0x0C];
	uint8_t &ICRH   = memory[0x0D];
	uint8_t &ICRL   = memory[0x0E];
	uint8_t &RMCR   = memory[0x10];
	uint8_t &TRCSR  = memory[0x11];
	uint8_t &RDR    = memory[0x12];
	uint8_t &TDR    = memory[0x13];
	uint8_t &RAMCR  = memory[0x14];

	// Operation
	void step();
	virtual void trace();
	void crash() { halt = true; }
	bool halt = false;
	uint64_t cycle = 0;
	bool readTCSR=false;
	bool wroteOCR=false;
	bool readTRCSR=false;

	// Serial Interface
	void clockInData(uint8_t byte);
	bool clockOutData(uint8_t& byte);
	uint32_t sci_tx_counter = 0, sci_rx_counter = 0;

	// Interrupts
	void nmi();
	bool swi();
	bool irq();
	bool ici();
	bool oci();
	bool toi();
	bool cmi();
	bool irq2();
	bool sci();
	void trap();
	bool maskable_interrupt(uint16_t vector);
	void interrupt(uint16_t vector);
	void reset();
	bool irqpin = true;

	// Instructions
	struct Instruction {
		Instruction() = default;
		Instruction( uint8_t opcode, const char *group, const char *op,
				const char* mode, bool r, bool w, int bytes, int cycles,
				std::function<void()> action
			) :
				opcode(opcode), group(group), op(op), mode(mode),
				r(r), w(w), bytes(bytes), cycles(cycles), action(action)
			{ }

		uint8_t opcode=0;
		const char *group="", *op="";
		const char *mode = 0;
		bool r=0, w=0;
		int bytes=0, cycles=0;
		std::function<void()> action=0;
	};
	Instruction instructions[256];
	void initInst();
	Instruction* inst=0;

	// Decoding temps
	uint8_t opcode;
	uint8_t *M = memory;
	uint8_t R, OP;
	uint16_t R2, OP2, ADDR;

	// CCR utilities
	void A8(uint8_t op1, uint8_t op2, uint8_t r);
	void A16(uint16_t op1, uint16_t op2, uint16_t r);
	void S8(uint8_t op1, uint8_t op2, uint8_t r);
	void S16(uint16_t op1, uint16_t op2, uint16_t r);
	void L8(uint8_t r);
	void L16(uint16_t r);
	void I8(uint8_t r);
	void D8(uint8_t r);

	// Addressing modes
	void direct2();
	void direct16();
	void direct3();
	void extend();
	void extend16();
	void immed2();
	void immed3();
	void implied();
	void index2();
	void index16();
	void index3();

	// Instruction helpers
	void isleep();
	void wait();
	void bra(bool c);
	void bsr();
	void asl(uint8_t &x);
	void asld(uint16_t &x);
	void asr(uint8_t &x);
	void lsr(uint8_t &x);
	void lsrd(uint16_t &x);
	void rol(uint8_t &x);
	void ror(uint8_t &x);
	inline void push(uint16_t x);
	void pull(uint16_t &x);
	void rti();
	void daa();

	// Utilities
	inline void stom16(uint16_t addr, uint16_t x);
	inline uint16_t getm16(uint16_t addr);
	inline uint8_t getCCR();
	inline void setCCR(uint8_t ccr);
	inline bool bit(uint16_t x, uint8_t n) { return (x>>n)&1; }
	inline uint8_t set(uint8_t &x, uint8_t n) { return x |= (1<<n); }
	inline uint8_t clear(uint8_t &x, uint8_t n) { return x &= ~(1<<n); }
	inline uint16_t extend8(uint8_t x) { return (x & 0x80) ? uint16_t(x)|0xFF00 : uint16_t(x); }
};

inline void HD6303R::stom16(uint16_t addr, uint16_t x) {
	memory[addr] = x>>8;
	memory[++addr] = x&0xFF;
}

inline uint16_t HD6303R::getm16(uint16_t addr) {
	uint16_t x = memory[addr++]<<8;
	x |= memory[addr];
	return x;
}

inline void HD6303R::push(uint16_t x) {
	memory[SP--] = x&0xFF;
	memory[SP--] = x>>8;
}

inline uint8_t HD6303R::getCCR() {
	uint8_t ccr = 0xC0;
	if(H) ccr |= (1<<5);
	if(I) ccr |= (1<<4);
	if(N) ccr |= (1<<3);
	if(Z) ccr |= (1<<2);
	if(V) ccr |= (1<<1);
	if(C) ccr |= 1;
	return ccr;
}

inline void HD6303R::setCCR(uint8_t ccr) {
	H = ccr&(1<<5);
	I = ccr&(1<<4);
	N = ccr&(1<<3);
	Z = ccr&(1<<2);
	V = ccr&(1<<1);
	C = ccr&1;
}

} // namespace dx7Emu
