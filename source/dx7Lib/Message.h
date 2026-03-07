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

#include <cstdio>
#include <cstdint>
#include "LFQ.h"

namespace dx7Emu
{

struct Message {
	enum class CtrlID : uint8_t;
	Message(const uint8_t b1=0, const uint8_t b2=0) : byte1(b1), byte2(b2) {}
	Message(const Message::CtrlID id, const uint8_t data) : byte1(uint8_t(id)), byte2(data) {}

	operator int() const { return (byte1<<8)|byte2; }
	Message(const uint16_t v) : byte1((v>>8)&0xFF), byte2(v&0xFF) {}

	uint8_t byte1, byte2;

	enum class CtrlID : uint8_t {
		// Front panel buttons
		b_1, b_2, b_3, b_4, b_5, b_6, b_7, b_8,
		b_9, b_10, b_11, b_12, b_13, b_14, b_15, b_16,
		b_17, b_18, b_19, b_20, b_21, b_22, b_23, b_24,
		b_25, b_26, b_27, b_28, b_29, b_30, b_31, b_32,
		b_w, b_x, b_y, b_z,
		b_chr, b_dash, b_dot, b_sp,
		b_no, b_yes,

		sustain, porta,
		cartridge, protect,
		volume,
		send_state,
		cartridge_file,
		none,

		// Analog sources
		data=144, pitchbend=145, modulate=146,
		foot=147, breath=148, aftertouch=149, battery=150,

		// Button events
		buttondown=152, buttonup=153,

		// 159 - 219 : keys

		// Synth to GUI (not used in Retromulator but kept for protocol completeness)
		lcd_inst = 230,
		lcd_data = 231,
		led1_setval = 232,
		led2_setval = 233,
		cartridge_num = 234,
		cartridge_name = 235,
		lcd_state = 236,
	};
};

// Abstract base comm channel
struct Interface {
	Interface() {}
	virtual ~Interface() {}
	virtual void push(Message m) = 0;
	virtual int pop(Message &m) = 0;
	void sendBinary(Message::CtrlID id, const uint8_t *data, uint8_t len);
	int getBinary(uint8_t *data, uint8_t len);
};

// Comm channel with a Lock-Free Queue
struct AppInterface : public virtual Interface {
	virtual ~AppInterface() {}
	virtual void push(Message m) { lfq.push(m); }
	virtual int pop(Message &m) { return lfq.pop(m); }
	CircularFifo<Message, 1024> lfq;
};

// Synth-to-GUI: stubbed to a no-op sink in Retromulator (no DX7 front panel)
struct ToGui : public virtual Interface {
	virtual ~ToGui() {}
	void lcd_inst(uint8_t) { }
	void lcd_data(uint8_t) { }
	void led1_setval(uint8_t) { }
	void led2_setval(uint8_t) { }
	void cartridge_num(uint8_t) { }
	void cartridge_name(const uint8_t *, uint8_t) { }
	void lcd_state(const uint8_t *, uint8_t) { }
};

// GUI-to-Synth messages
struct ToSynth : public virtual Interface {
	virtual ~ToSynth() {}
	void key_on(uint8_t key, uint8_t vel) { if(key<61) push({uint8_t(159+key), vel}); }
	void key_off(uint8_t key) { if(key<61) push({uint8_t(159+key), 0}); }
	void buttondown(Message::CtrlID button) { push({Message::CtrlID::buttondown, uint8_t(uint8_t(button)+80)}); }
	void buttonup(Message::CtrlID button) { push({Message::CtrlID::buttonup, uint8_t(uint8_t(button)+80)}); }
	void analog(Message::CtrlID source, uint8_t val) { push({source, val}); }
	void sustain(bool down) { push({Message::CtrlID::sustain, down}); }
	void porta(bool down) { push({Message::CtrlID::porta, down}); }
	void cartridge(bool p) { push({Message::CtrlID::cartridge, p}); }
	void protect(bool p) { push({Message::CtrlID::protect, p}); }
	void cartridge_file(const uint8_t *data, uint8_t len) { sendBinary(Message::CtrlID::cartridge_file, data, len); }
	void load_cartridge_num(uint8_t v) { push({Message::CtrlID::cartridge_num, v}); }
	void requestState() { push({Message::CtrlID::send_state, 0}); }
};

// Concrete instances using lock-free queues
// ToGui is a no-op sink — push/pop do nothing
struct NullToGui : public ToGui, public AppInterface {
	virtual ~NullToGui() {}
	// Override push to discard (no GUI to receive)
	virtual void push(Message) override {}
	virtual int pop(Message &) override { return 0; }
};

struct App_ToSynth : public ToSynth, public AppInterface { virtual ~App_ToSynth() {} };

// Protocol for binary strings
inline void Interface::sendBinary(Message::CtrlID id, const uint8_t *data, uint8_t len) {
	Message msg{uint8_t(id), len};
	push(msg);
	if(len&1) {
		for(int i=0; i<len-1; i+=2) push({data[i], data[i+1]});
		push({data[len-1], 0});
	} else for(int i=0; i<len; i+=2) push({data[i], data[i+1]});
}

inline int Interface::getBinary(uint8_t *data, uint8_t len) {
	Message msg;
	if(len&1) {
		for(int i=0; i<len-1; i+=2) {
			if(!pop(msg)) return 1;
			*data++ = msg.byte1;
			*data++ = msg.byte2;
		}
		if(!pop(msg)) return 1;
		*data = msg.byte1;
	} else {
		for(int i=0; i<len; i+=2) {
			if(!pop(msg)) return 1;
			*data++ = msg.byte1;
			*data++ = msg.byte2;
		}
	}
	return 0;
}

} // namespace dx7Emu
