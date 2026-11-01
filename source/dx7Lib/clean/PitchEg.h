#pragma once
#include <array>
#include <cstdint>

#include "Patch.h"
#include "Regs.h"

// Pitch EG, fitted from measurements/pitch_eg (see fits/pitch_eg/FIT.md)
namespace dx7clean::peg
{
// level 17-85 is linear; the ends bend, no compact exact form found (see FIT.md)
constexpr std::array<int8_t, 17> kLevelLow = {-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35};
constexpr std::array<int8_t, 14> kLevelHigh = {38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};

// level 0-99 -> voice word offset / 128
constexpr int levelOffset(uint8_t level)
{
	if (level > 99)
		level = 99;
	if (level < kLevelLow.size())
		return kLevelLow[level];
	if (level > 85)
		return kLevelHigh[level - 86];
	return int(level) - 50;
}

// rates 27-99, no compact exact form found (see FIT.md)
constexpr std::array<uint8_t, 73> kStepHigh = {
	16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42,
	44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98,
	102, 106, 110, 115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

// rate 0-99 -> voice word units per update
constexpr uint8_t stepOf(uint8_t rate)
{
	if (rate > 99)
		rate = 99;
	if (rate < 27)
		return rate ? uint8_t((rate + 4) >> 1) : uint8_t(1);
	return kStepHigh[rate - 27];
}

constexpr uint16_t kCenter = 0x8000;
constexpr uint16_t kPowerOn = 0x4000;

// timer half-tick: voices 0-7 and 8-15 alternate, so each voice is updated every 2 half-ticks
constexpr double kHalfTickCycles = 3152.8;

constexpr uint16_t levelValue(uint8_t level)
{
	return uint16_t(kCenter + levelOffset(level) * 128);
}

// value 0x8000 = no shift; the stage ends once the value reaches the target's high byte
constexpr uint16_t advance(uint16_t value, uint16_t target, uint8_t step)
{
	if (value < target)
	{
		const unsigned n = unsigned(value) + step;
		return (n >= target || (n >> 8) == unsigned(target >> 8)) ? target : uint16_t(n);
	}
	if (value > target)
	{
		const int n = int(value) - step;
		return (n <= int(target) || (n >> 8) == (target >> 8)) ? target : uint16_t(n);
	}
	return value;
}

// voice word plus pitch EG, floored like the pre-key-on word in Pitch.h
constexpr uint16_t applyOffset(uint16_t word, uint16_t value)
{
	const int w = int(word) + int(value) - kCenter;
	return uint16_t(w < 256 ? 256 : w > 0xffff ? 0xffff : w);
}

class Voice
{
public:
	// L4 jumps in at once; the caller writes applyOffset(word, value()) before and after this call
	void keyOn(const Vced& v)
	{
		m_value = levelValue(v[vced::PL4]);
		m_stage = 0;
		m_done = false;
	}

	// legato in mono mode leaves the envelope running: call neither keyOn nor keyOff

	void keyOff()
	{
		m_stage = 3;
		m_done = false;
	}

	// one update per voice per 2 half-ticks, half a tick before the voice word is written
	void update(const Vced& v)
	{
		if (m_done)
			return;
		const uint16_t t = levelValue(v[size_t(vced::PL1 + m_stage)]);
		m_value = advance(m_value, t, stepOf(v[size_t(vced::PR1 + m_stage)]));
		if (m_value != t)
			return;
		if (m_stage < 2)
			++m_stage;
		else
			m_done = true;
	}

	uint16_t value() const { return m_value; }
	int offset() const { return int(m_value) - kCenter; }
	int stage() const { return m_stage + 1; }
	bool done() const { return m_done; }

	void reset()
	{
		m_value = kPowerOn;
		m_stage = 3;
		m_done = true;
	}

private:
	uint16_t m_value = kPowerOn;
	uint8_t m_stage = 3;
	bool m_done = true;
};

// half-tick h writes the voice words of group (h & 1) (0 = voices 0-7) and updates the other group
class Bank
{
public:
	template<typename Word, typename Write> void halfTick(const Vced& v, Word&& word, Write&& write)
	{
		const int g = m_group * 8;
		for (int i = g; i < g + 8; ++i)
		{
			const uint16_t w = applyOffset(word(i), m_voice[size_t(i)].value());
			write(uint16_t(regs::kVoicePitch + 2 * i), uint8_t(w >> 8));
			write(uint16_t(regs::kVoicePitch + 2 * i + 1), uint8_t(w & 0xff));
		}
		const int o = (m_group ^ 1) * 8;
		for (int i = o; i < o + 8; ++i)
			m_voice[size_t(i)].update(v);
		m_group ^= 1;
	}

	Voice& operator[](int voice) { return m_voice[size_t(voice)]; }
	const Voice& operator[](int voice) const { return m_voice[size_t(voice)]; }

	void reset()
	{
		for (auto& p : m_voice)
			p.reset();
		m_group = 1;
	}

private:
	std::array<Voice, regs::kVoices> m_voice{};
	int m_group = 1;
};
}
