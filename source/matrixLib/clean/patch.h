#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "types.h"

namespace matrixClean
{
	// 134 byte single patch, layout per INTERFACE.md 5.1
	struct Patch
	{
		static constexpr size_t Size = 134;
		static constexpr size_t NameLength = 8;
		static constexpr size_t MatrixBuses = 10;

		std::array<uint8_t, Size> data{};

		uint8_t u(size_t _byte) const { return data[_byte]; }
		int s(size_t _byte) const { return static_cast<int8_t>(data[_byte]); }

		std::string name() const;
		void setName(const std::string& _name);

		KeyboardMode keyboardMode() const { return static_cast<KeyboardMode>(data[8] & 3); }

		struct Dco
		{
			uint8_t freq, shape, pw, levers, wave, portaKeyb, click;
			int lfo1Freq, lfo2Pw;
		};
		Dco dco(uint32_t _index) const;
		int detune() const { return s(19); }
		uint8_t mix() const { return u(20); }
		uint8_t sync() const { return u(25) & 3; }

		uint8_t vcfFreq() const { return u(26) & 0x7f; }
		uint8_t vcfRes() const { return u(27); }
		uint8_t vcfLevers() const { return u(28) & 3; }
		uint8_t vcfKeyb() const { return u(29) & 3; }   // bit 0 porta, bit 1 keyboard
		uint8_t fmAmount() const { return u(30); }
		uint8_t vca1() const { return u(31); }
		int vcfByEnv1() const { return s(90); }
		int vcfByPressure() const { return s(91); }
		int vca1ByVelocity() const { return s(92); }
		int vca2ByEnv2() const { return s(93); }
		int fmByEnv3() const { return s(100); }
		int fmByPressure() const { return s(101); }

		uint8_t portaRate() const { return u(32); }
		LagMode lagMode() const { return static_cast<LagMode>(u(33) & 3); }
		bool legato() const { return (u(34) & 1) != 0; }
		int portaByVelocity() const { return s(99); }

		struct Lfo
		{
			uint8_t speed; LfoTrigger trigger; bool lag; LfoWave wave; uint8_t retrigger;
			Source sampleSource; uint8_t amp; int speedMod; int ampByRamp;
		};
		Lfo lfo(uint32_t _index) const;

		struct Env
		{
			uint8_t trigger, delay, attack, decay, sustain, release, amp, lfoTrigger, mode;
			int ampByVelocity;
		};
		Env env(uint32_t _index) const;

		Source trackInput() const { return static_cast<Source>(u(76) % 21); }
		uint8_t trackPoint(uint32_t _i) const { return u(77 + _i); }

		uint8_t rampRate(uint32_t _i) const { return u(82 + _i * 2); }
		RampTrigger rampTrigger(uint32_t _i) const { return static_cast<RampTrigger>(u(83 + _i * 2) & 3); }

		struct Bus { Source source; int amount; Dest dest; };
		Bus bus(uint32_t _i) const;
		void setBus(uint32_t _i, uint8_t _source, int _amount, uint8_t _dest);
	};

	// opcode 06 / NRPN parameter numbers (INTERFACE.md 5.1 P column)
	struct ParamInfo
	{
		uint8_t number;
		uint8_t byte;
		int8_t min;
		int8_t max;
	};
	const ParamInfo* findParam(uint8_t _number);
	// applies a remote edit; _raw is the 7-bit MIDI value, sign extended unless the param is unsigned 7 bit
	bool applyParam(Patch& _p, uint8_t _number, uint8_t _raw);
	bool setParam(Patch& _p, uint8_t _number, int _value);

	Patch makeInitPatch();

	// nibble codec, checksum over packed bytes
	void packNibbles(const uint8_t* _data, size_t _size, std::vector<uint8_t>& _out);
	bool unpackNibbles(const uint8_t* _nibbles, size_t _size, uint8_t* _out, uint8_t _checksum);
	std::vector<uint8_t> encodePatch(const Patch& _p, uint8_t _opcode, uint8_t _number);
	// opcode 01 reply as sent by the unit: the name is replaced by "BNKb: nn" (sysex/replies.csv)
	std::vector<uint8_t> encodePatchReply(const Patch& _p, uint8_t _bank, uint8_t _number);
	// the same, appended to _out
	void encodePatchReply(const Patch& _p, uint8_t _bank, uint8_t _number, std::vector<uint8_t>& _out);
}
