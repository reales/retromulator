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

#include "HD6303R.h"
#include <cstring>

namespace dx7Emu {

void HD6303R::step() {
	if(halt) return;

	opcode = memory[PC++];
	inst = instructions+opcode;

	OP = OP2 = ADDR = 0;

	if(inst->mode == 0) {
		fprintf(stderr, "Illegal opcode %02X PC=%04X\n", opcode, PC);
		trap();
		return;
	}

	uint8_t saveTCSR = TCSR, saveTRCSR = TRCSR;

	inst->action();

	if(saveTCSR != TCSR) TCSR = (TCSR&0x1F) | (saveTCSR&0xE0);
	if(saveTRCSR != TRCSR) TRCSR = (TRCSR&0x1F) | (saveTRCSR&0xE0);

	cycle += inst->cycles;
	sci_tx_counter += inst->cycles;
	sci_rx_counter += inst->cycles;

	if(!irqpin) irq();

	uint32_t p_timer1 = getm16(0x09);
	uint32_t timer1 = p_timer1 + inst->cycles;
	stom16(0x09, timer1&0xFFFF);

	uint32_t ocr = getm16(0x0B);
	if(timer1>=ocr && p_timer1<ocr) {
		TCSR = set(TCSR, OCF);
		readTCSR = wroteOCR = false;
	}
	if(ADDR==0x08 && inst->r && bit(TCSR,OCF)) readTCSR = true;
	else if((ADDR==0x0B || ADDR==0x0C) && readTCSR && inst->w) wroteOCR = true;
	if(readTCSR && wroteOCR) TCSR = clear(TCSR, OCF);

	if(bit(TCSR, OCF) && bit(TCSR, EOCI)) oci();

	// SCI handling
	if(ADDR == 0x13 && readTRCSR && inst->w) {
		clear(TRCSR, TDRE);
		sci_tx_counter = 0;
	}

	if(ADDR == 0x12 && readTRCSR && inst->r) {
		clear(TRCSR, RDRF);
		clear(TRCSR, ORFE);
		sci_rx_counter = 0;
	}

	if(ADDR==0x11 && inst->r) readTRCSR = true;

	constexpr const uint8_t mask1 = 1<<TDRE | 1<<TIE | 1<<TE;
	constexpr const uint8_t mask2 = 1<<RDRF | 1<<RIE | 1<<RE;
	if((TRCSR & mask1) == mask1 || (TRCSR & mask2) == mask2) sci();
}

void HD6303R::clockInData(uint8_t byte) {
	if(!bit(TRCSR,RE)) return;
	RDR = byte;
	if(bit(TRCSR, RDRF)) set(TRCSR, ORFE);
	set(TRCSR, RDRF);
	readTRCSR = false;
}

bool HD6303R::clockOutData(uint8_t& byte) {
	if(!bit(TRCSR,TE)) return false;
	if(!bit(TRCSR, TDRE)) {
		set(TRCSR, TDRE);
		readTRCSR = false;
		byte = TDR;
		return true;
	} else return false;
}

void HD6303R::reset() {
	P1DDR  = 0xFE;
	P2DDR  = 0x00;
	PORT1  = 0x00;
	PORT2  = 0x00;
	TCSR   = 0x00;
	FRCH   = 0x00;
	FRCL   = 0x00;
	OCRH   = 0xFF;
	OCRL   = 0xFF;
	ICRH   = 0x00;
	ICRL   = 0x00;
	RMCR   = 0xC0;
	TRCSR  = 0x20;
	RDR    = 0x00;
	TDR    = 0x00;
	RAMCR  = 0x14;

	PC = getm16(0xFFFE);
	I = true;
}

void HD6303R::nmi()  { interrupt(0xFFFC); }
void HD6303R::trap() { interrupt(0xFFEE); }
bool HD6303R::swi()  { return maskable_interrupt(0xFFFA); }
bool HD6303R::irq()  { return maskable_interrupt(0xFFF8); }
bool HD6303R::ici()  { return maskable_interrupt(0xFFF6); }
bool HD6303R::oci()  { return maskable_interrupt(0xFFF4); }
bool HD6303R::toi()  { return maskable_interrupt(0xFFF2); }
bool HD6303R::cmi()  { return maskable_interrupt(0xFFEC); }
bool HD6303R::irq2() { return maskable_interrupt(0xFFEA); }
bool HD6303R::sci()  { return maskable_interrupt(0xFFF0); }

bool HD6303R::maskable_interrupt(uint16_t vector) {
	if(I) return false;
	interrupt(vector);
	return true;
}

void HD6303R::interrupt(uint16_t vector) {
	push(PC);
	push(IX);
	memory[SP--] = A;
	memory[SP--] = B;
	memory[SP--] = getCCR();
	I = 1;
	PC = memory[vector] << 8;
	PC |= memory[vector+1];
}

// Load a program from memory buffer (replaces file-based pgmload)
int HD6303R::pgmload(const uint8_t *data, size_t size) {
	if(!data || size == 0 || size > 63000) return -1;
	int start = 0x10000 - static_cast<int>(size);
	memcpy(memory+start, data, size);
	reset();
	return 0;
}

// Load a memory segment from buffer (replaces file-based memload)
int HD6303R::memload(const uint8_t *data, size_t size, uint16_t addr) {
	if(!data || size == 0 || size+addr > 63000) return -1;
	memcpy(memory+addr, data, size);
	return 0;
}

void HD6303R::trace() {
	printf("%016llX inst=%02X %4s %2s OP=%02X OP2=%04X ADDR=%04X "
		"A=%02X B=%02X CCR=%d%d%d%d%d%d SP=%04X IX=%04X PC=%04X ",
		(unsigned long long)cycle, opcode, instructions[opcode].op, instructions[opcode].mode, OP, OP2, ADDR,
		A, B, H, I, N, Z, V, C, SP, IX, PC);
}

// CCR utilities
void HD6303R::A8(uint8_t op1, uint8_t op2, uint8_t r) {
	uint8_t c = (op1 & op2) | (op2 & ~r) | (~r & op1);
	H = c & (1<<3);
	N = r & (1<<7);
	Z = !r;
	V = ( (op1 & op2 & ~r) | (~op1 & ~op2 & r ) ) & (1<<7);
	C = c & (1<<7);
}
void HD6303R::A16(uint16_t op1, uint16_t op2, uint16_t r) {
	N = r & (1<<15);
	Z = !r;
	V = ( (op1 & op2 & ~r) | (~op1 & ~op2 & r) ) & (1<<15);
	C = ( (op1 & op2) | (op2 & ~r) | (~r & op1) ) & (1<<15);
}
void HD6303R::S8(uint8_t op1, uint8_t op2, uint8_t r) {
	N = r & (1<<7);
	Z = !r;
	V = ( (op1 & ~op2 & ~r) | (~op1 & op2 & r) ) & (1<<7);
	C = ( (~op1 & op2) | (op2 & r) | (r & ~op1) ) & (1<<7);
}
void HD6303R::S16(uint16_t op1, uint16_t op2, uint16_t r) {
	N = r & (1<<15);
	Z = !r;
	V = ( (op1 & ~op2 & ~r) | (~op1 & op2 & r) ) & (1<<15);
	C = ( (~op1 & op2) | (op2 & r) | (r & ~op1) ) & (1<<15);
}
void HD6303R::L8(uint8_t r) {
	N = r & (1<<7);
	Z = !r;
	V = 0;
}
void HD6303R::L16(uint16_t r) {
	N = r & (1<<15);
	Z = !r;
	V = 0;
}
void HD6303R::D8(uint8_t r) {
	N = r & (1<<7);
	Z = !r;
	V = (r==0x7F);
}
void HD6303R::I8(uint8_t r) {
	N = r & (1<<7);
	Z = !r;
	V = (r==0x80);
}

void HD6303R::isleep() { halt = true; }
void HD6303R::wait() {
	halt = true;
	push(PC);
	push(IX);
	memory[SP--] = A;
	memory[SP--] = B;
	memory[SP--] = getCCR();
}

void HD6303R::bra(bool c) { if(c) PC += extend8(OP); }
void HD6303R::bsr() { push(PC); PC += extend8(OP); }

void HD6303R::asl(uint8_t &x) {
	C = bit(x,7);
	x <<= 1;
	N = bit(x,7);
	Z = !x;
	V = N^C;
}

void HD6303R::asld(uint16_t &x) {
	C = bit(x,15);
	x <<= 1;
	N = bit(x,15);
	Z = !x;
	V = N^C;
}

void HD6303R::asr(uint8_t &x) {
	C = bit(x,0);
	x = int8_t(x)>>1;
	N = bit(x,7);
	Z = !x;
	V = N^C;
}

void HD6303R::lsr(uint8_t &x) {
	C = bit(x,0);
	x >>= 1;
	N = 0;
	Z = !x;
	V = N^C;
}

void HD6303R::lsrd(uint16_t &x) {
	C = bit(x,0);
	x >>= 1;
	N = 0;
	Z = !x;
	V = N^C;
}

void HD6303R::rol(uint8_t &x) {
	bool c = bit(x,7);
	x <<= 1;
	x |= C;
	C = c;
	N = 0;
	Z = !x;
	V = N^C;
}

void HD6303R::ror(uint8_t &x) {
	bool c = bit(x,0);
	x >>= 1;
	x |= (C<<7);
	C = c;
	N = 0;
	Z = !x;
	V = N^C;
}

void HD6303R::pull(uint16_t &x) {
	x = memory[++SP] << 8;
	x |= memory[++SP];
}

void HD6303R::rti() {
	setCCR(memory[++SP]);
	B = memory[++SP];
	A = memory[++SP];
	pull(IX);
	pull(PC);
}

void HD6303R::daa() {
	uint8_t c = 0;
	uint8_t lsn = (A & 0x0F);
	uint8_t msn = (A & 0xF0) >> 4;
	if (H || (lsn > 9)) c |= 0x06;
	if (C || (msn > 9) || ((msn > 8) && (lsn > 9))) c |= 0x60;
	uint16_t t = uint16_t(A) + c;
	C |= bit(t, 8);
	A = uint8_t(t);
	N = bit(A, 7);
	Z = !A;
}


// Addressing modes
void HD6303R::direct2() {
	ADDR = memory[PC++];
	OP = memory[ADDR];
}
void HD6303R::direct16() {
	ADDR = memory[PC++];
	OP2 = getm16(ADDR);
}
void HD6303R::direct3() {
	OP = memory[PC++];
	ADDR = memory[PC++];
}
void HD6303R::extend() {
	ADDR = memory[PC++]<<8;
	ADDR |= memory[PC++];
	OP = memory[ADDR];
}
void HD6303R::extend16() {
	ADDR = memory[PC++]<<8;
	ADDR |= memory[PC++];
	OP2 = getm16(ADDR);
}
void HD6303R::immed2() {
	OP = memory[PC++];
}
void HD6303R::immed3() {
	OP2 = memory[PC++]<<8;
	OP2 |= memory[PC++];
}
void HD6303R::implied() { }
void HD6303R::index2() {
	ADDR = IX + memory[PC++];
	OP = memory[ADDR];
}
void HD6303R::index16() {
	ADDR = IX + memory[PC++];
	OP2 = getm16(ADDR);
}
void HD6303R::index3() {
	OP = memory[ PC++ ];
	ADDR = IX + memory[PC++];
}

} // namespace dx7Emu
