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

#pragma once

#include <cstring>
#include <string>
#include <vector>
#include "HD6303R.h"
#include "HD44780.h"
#include "EGS.h"
#include "Message.h"
#include "filter.h"

namespace dx7Emu
{

// Basic circular buffer, 2^N elements
template <class T, int N> struct Buffer {
	static const int size = 1<<N;
	T buffer[size];
	int readIdx=0, writeIdx=0;
	void write(const T& byte) {
		buffer[writeIdx++] = byte;
		writeIdx &= (size-1);
	}
	void flush() { readIdx = writeIdx; }
	bool empty() { return readIdx == writeIdx; }
	bool read(T& data) {
		if(empty()) return false;
		data = buffer[readIdx];
		++readIdx &= (size-1);
		return true;
	}
	T& read() {
		T& data = buffer[readIdx];
		if(!empty()) ++readIdx &= (size-1);
		return data;
	}
};

// DX7 hardware emulation
struct DX7: public HD6303R {
	DX7(ToSynth*& ts, ToGui*& tg);
	~DX7();

	// Load firmware ROM from memory buffer (16384 bytes expected)
	bool loadFirmware(const uint8_t* data, size_t size);

	// Load factory voices from memory buffer (32768 bytes = 8 banks of 4096)
	bool loadVoices(const uint8_t* data, size_t size);

	void start();
	void run(); // Execute one instruction and update peripherals

	// References set to refer back to communication pointers
	ToSynth*& toSynth;
	ToGui*& toGui;

	// Initialize internal or cartridge patch memory to factory cartridge 0-7
	void setBank(int n, bool cart = false);

	// The EGS (and OPS contained within)
	EGS egs{memory+0x3000};

	// Battery-backed RAM state save/restore (from/to memory buffers)
	bool restoreRAM(const std::vector<uint8_t>& ramData);
	bool saveRAM(std::vector<uint8_t>& ramData) const;

	void tune(int tuning); // Master tuning -256 to +255

	// Interrupt to transfer events from sub-cpu to main
	bool byte1Sent = false, haveMsg = false;

	Message msg; // Queue of messages from GUI

	// MIDI buffers size 2^13=8192 bytes
	Buffer<uint8_t, 13> midiSerialRx, midiSerialTx;
	uint8_t getMidiRxChannel() { return M_MIDI_RX_CH; }

	// MIDI volume control through DAC
	uint8_t midiVolume = 7;
	const float midiVolTab[8] = { 0, 710/4790.0f, 200/4790.0f, 2590/4790.0f,
		100/4790.0f, 1390/4790.0f, 380/4790.0f, 4790/4790.0f };
	LP1 midiFilter;

	// Load cartridge from sysex data in memory (4104 bytes)
	bool cartLoadSysex(const uint8_t* data, size_t size);
	// Get cartridge as sysex data
	bool cartSaveSysex(std::vector<uint8_t>& sysexData) const;

	bool saveCart = false;
	int cartNum = -1;
	void cartWriteProtect(bool protect);
	bool cartWriteProtect() { return P_CRT_PEDALS_LCD & 0b1000000; }
	void cartPresent(bool present);
	bool cartPresent() { return !(P_CRT_PEDALS_LCD & 0b100000); }

	// Pedal status
	void sustain(bool on) { if(on) P_CRT_PEDALS_LCD |= 1; else P_CRT_PEDALS_LCD &= 0xFE; }
	void porta(bool on) { if(on) P_CRT_PEDALS_LCD |= 2; else P_CRT_PEDALS_LCD &= 0xFD; }

	// Direct pitch/mod offsets (added to EGS pitch mod on each firmware write)
	// Used as fallback when patch has PB range or mod sensitivity set to 0
	int16_t pitchBendOffset = 0;
	int16_t modWheelOffset = 0;

	bool isRomLoaded() const { return m_firmwareLoaded; }

	// Display hardware
	HD44780 lcd;

	// Peripheral address space - memory mapped
	uint8_t  &P_CRT_PEDALS_LCD                    =  memory[0x2802];
	uint8_t  &P_OPS_MODE                          =  memory[0x2804];
	uint8_t  &P_OPS_ALG_FDBK                      =  memory[0x2805];
	uint8_t  &P_DAC                               =  memory[0x280A];
	uint8_t  &P_LED1                              =  memory[0x280E];
	uint8_t  &P_LED2                              =  memory[0x280F];

	// EGS address space
	uint8_t  *P_EGS_OP_DETUNE                     = &memory[0x3030];
	uint8_t  *P_EGS_OP_EG_RATES                   = &memory[0x3040];
	uint8_t  *P_EGS_OP_EG_LEVELS                  = &memory[0x3060];
	uint8_t  *P_EGS_OP_LEVELS                     = &memory[0x3080];
	uint8_t  *P_EGS_OP_SENS_SCALING               = &memory[0x30E0];
	uint8_t  &P_EGS_AMP_MOD                       =  memory[0x30F0];
	uint8_t  &P_EGS_VOICE_EVENTS                  =  memory[0x30F1];
	uint8_t  &P_EGS_PITCH_MOD_HIGH                =  memory[0x30F2];
	uint8_t  &P_EGS_PITCH_MOD_LOW                 =  memory[0x30F3];

	// RAM address space (firmware-specific locations)
	uint8_t  &M_MASTER_TUNE                       =  memory[0x2311];
	uint8_t  &M_MASTER_TUNE_LOW                   =  memory[0x2312];
	uint8_t  &M_MIDI_RX_CH                        =  memory[0x2573];

private:
	bool m_firmwareLoaded = false;
	const uint8_t* m_voicesData = nullptr; // pointer to factory voices (kept alive by caller)
	size_t m_voicesSize = 0;
};

} // namespace dx7Emu
