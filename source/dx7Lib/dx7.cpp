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
 *
 *  Adapted for Retromulator integration.
**/

#include "dx7.h"
#include <cstdio>
#include <cstring>

namespace dx7Emu
{

DX7::DX7(ToSynth*& ts, ToGui*& tg)
	: toSynth(ts), toGui(tg) {
}

DX7::~DX7() {
}

bool DX7::loadFirmware(const uint8_t* data, size_t size) {
	if(!data || size != 16384) {
		return false;
	}
	for(int addr = 0xC000; addr < 0x10000; addr++)
		memory[addr] = data[addr - 0xC000];
	m_firmwareLoaded = true;
	return true;
}

bool DX7::loadVoices(const uint8_t* data, size_t size) {
	if(!data || size < 4096) {
		return false;
	}
	m_voicesData = data;
	m_voicesSize = size;
	return true;
}

void DX7::start() {
	if(!m_firmwareLoaded) return;

	// Battery voltage "LOW" until RAM is restored
	toSynth->analog(Message::CtrlID::battery, 49);

	// Clear RAM (no file-based restore in plugin mode)
	for(int i=0; i<6144; i++) memory[0x1000+i] = 0;
	tune(0); // Default master tuning to A440

	P_CRT_PEDALS_LCD = 0x00;
	porta(true);
	cartPresent(false);
	cartWriteProtect(true);
	P_CRT_PEDALS_LCD &= ~0b10000000;

	reset(); // Start processor
}

void DX7::tune(int tuning) {
	if(tuning < 256 && tuning >= -256) {
		uint16_t t = static_cast<uint16_t>(tuning + 256);
		M_MASTER_TUNE = static_cast<uint8_t>(t >> 8);
		M_MASTER_TUNE_LOW = static_cast<uint8_t>(t & 0xFF);
	}
}

void DX7::run() {
	step();

	// Serial baud rate timer
	if(sci_tx_counter >= 377) {
		uint8_t byte;
		if(clockOutData(byte)) midiSerialTx.write(byte);
	}
	if(sci_rx_counter >= 377) {
		if(!midiSerialRx.empty() && bit(TRCSR,RDRF)==0) clockInData(midiSerialRx.read());
	}

	// Sub-CPU Event Handshake
	if(bit(PORT2,0) && haveMsg && !byte1Sent) {
		PORT1 = msg.byte1;
		clear(PORT2,1);
		irqpin = false;
		byte1Sent = true;
	}

	if(inst->w == false) return;

	// Writing to peripheral controller 0x280*
	if((ADDR&0xFFF0)==0x2800) {
		if(ADDR==0x2800) { // P_LCD_DATA
			if(memory[0x2801]==4) {
				toGui->lcd_inst(memory[ADDR]);
				lcd.inst(memory[ADDR]);
			}
			else if(memory[0x2801]==5) {
				toGui->lcd_data(memory[ADDR]);
				lcd.data(memory[ADDR]);
			}
		}
		else if(ADDR==0x280C) { // P_ACEPT
			if(byte1Sent) {
				PORT1 = msg.byte2;
				clear(PORT2,1);
				irqpin = false;
				byte1Sent = false;
			} else {
				irqpin = true;
				haveMsg = false;
			}
		}
		else if(ADDR==0x280E) { // P_LED1 & P_LED2
			toGui->led1_setval(P_LED1);
			toGui->led2_setval(P_LED2);
		}
		else if(ADDR==0x2805) { // P_OPS_MODE & P_OPS_ALG_FDBK
			egs.setAlgorithm(P_OPS_MODE, P_OPS_ALG_FDBK);
		}
		else if(ADDR==0x280A) { // P_DAC - MIDI volume control
			midiVolume = P_DAC&7;
		}
	}

	// Writing to EGS address space 0x30**
	if((ADDR&0xFF00)==0x3000) {
		// Add pitch bend and mod wheel offsets to firmware's pitch mod value
		if(ADDR == 0x30F3 && (pitchBendOffset != 0 || modWheelOffset != 0)) {
			int16_t fwVal = static_cast<int16_t>((memory[0x30F2] << 8) | memory[0x30F3]);
			int32_t combined = fwVal + pitchBendOffset + modWheelOffset;
			if(combined > 32767) combined = 32767;
			if(combined < -32768) combined = -32768;
			memory[0x30F2] = static_cast<uint8_t>((combined >> 8) & 0xFF);
			memory[0x30F3] = static_cast<uint8_t>(combined & 0xFF);
		}
		egs.update(uint8_t(ADDR));
	}

	// Writing to Cartridge 0x4***
	if((ADDR&0xF000)==0x4000) saveCart = true;
}

bool DX7::cartLoadSysex(const uint8_t* data, size_t size) {
	if(!data || size != 4104) return false;

	// Verify sysex header: F0 43 00 09 20 00
	if(memcmp(data, "\xf0\x43\x00\x09\x20\x00", 6) != 0)
		return false;

	// Copy voice data to cartridge memory
	memcpy(memory + 0x4000, data + 6, 4096);

	// Verify checksum
	int checksum = data[4102]; // byte after 4096 data bytes
	for(int i=0; i<4096; i++) checksum += memory[0x4000 + i];
	if(checksum & 0x7F)
		return false;

	cartNum = -1;
	cartPresent(true);
	cartWriteProtect(true);
	return true;
}

bool DX7::cartSaveSysex(std::vector<uint8_t>& sysexData) const {
	sysexData.resize(4104);
	sysexData[0] = 0xF0;
	sysexData[1] = 0x43;
	sysexData[2] = 0x00;
	sysexData[3] = 0x09;
	sysexData[4] = 0x20;
	sysexData[5] = 0x00;

	memcpy(sysexData.data() + 6, memory + 0x4000, 4096);

	int8_t checksum = 0;
	for(int i=0; i<4096; i++) checksum += static_cast<int8_t>(memory[0x4000 + i]);
	sysexData[4102] = static_cast<uint8_t>((-checksum) & 0x7F);
	sysexData[4103] = 0xF7;
	return true;
}

void DX7::cartPresent(bool present) {
	if(present) P_CRT_PEDALS_LCD &= ~0b100000;
		else P_CRT_PEDALS_LCD |= 0b100000;
}

void DX7::cartWriteProtect(bool protect) {
	if(protect) P_CRT_PEDALS_LCD |= 0b1000000;
		else P_CRT_PEDALS_LCD &= ~0b1000000;
}

void DX7::setBank(int n, bool cart) {
	if(!m_voicesData || m_voicesSize < 4096) return;

	int bankIdx = n & 0x7;
	size_t offset = 4096 * static_cast<size_t>(bankIdx);
	if(offset + 4096 > m_voicesSize) return;

	const uint8_t *ram = m_voicesData + offset;

	if(cart) {
		cartPresent(true);
		cartNum = bankIdx;
		for(int addr = 0x4000; addr < 0x5000; addr++)
			memory[addr] = *ram++;
		toGui->cartridge_num(static_cast<uint8_t>(cartNum));
	} else {
		for(int addr = 0x1000; addr < 0x2000; addr++)
			memory[addr] = *ram++;
	}
}

bool DX7::restoreRAM(const std::vector<uint8_t>& ramData) {
	if(ramData.size() != 6144) return false;
	memcpy(memory + 0x1000, ramData.data(), 6144);
	toSynth->analog(Message::CtrlID::battery, 82);
	return true;
}

bool DX7::saveRAM(std::vector<uint8_t>& ramData) const {
	ramData.resize(6144);
	memcpy(ramData.data(), memory + 0x1000, 6144);
	return true;
}

} // namespace dx7Emu
