#pragma once
#include <array>
#include <cstdint>

#include "Global.h"
#include "Pitch.h"
#include "Regs.h"

// Portamento and glissando, fitted from measurements/controllers portamento_* (see fits/controllers/FIT.md).
// update() is one portamento tick: the voice word refresh, every second control pass
namespace dx7clean
{
namespace porta
{
// word units per tick for portamento time 1-99
constexpr std::array<uint8_t, 100> kRate = {
	0, 254, 243, 232, 211, 202, 193, 185, 178, 171, 165, 159, 153, 147, 141, 135, 130, 125, 120, 115,
	110, 106, 102, 98, 94, 91, 88, 85, 82, 79, 76, 74, 72, 70, 68, 66, 64, 62, 60, 58,
	56, 54, 53, 51, 49, 47, 46, 44, 42, 41, 39, 38, 37, 36, 34, 33, 31, 30, 28, 27,
	26, 25, 24, 23, 22, 21, 20, 19, 18, 18, 17, 16, 16, 15, 14, 14, 13, 13, 12, 12,
	11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 1};

// glide step: rate times 1 + (high byte distance >> 2), clamped at the target
constexpr int glideStep(int cur, int target, int rate)
{
	const int dh = (cur >> 8) - (target >> 8);
	const int d = rate * (((dh < 0 ? -dh : dh) >> 2) + 1);
	return target > cur ? (cur + d > target ? target : cur + d) : (cur - d < target ? target : cur - d);
}

// glissando: position lead over the current key that moves the output one key
constexpr int kGlissStep = 340;

// key words sit at 9216 + 1024 * q + {0, 344, 684} (plus master tune)
constexpr int kKeyOffset[4] = {0, 344, 684, 1024};

constexpr int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

constexpr int keyAbove(int w)
{
	const int q = floorDiv(w - 9216, 1024), r = w - 9216 - 1024 * q;
	int i = 0;
	while (kKeyOffset[i] <= r)
		++i;
	return 9216 + 1024 * q + kKeyOffset[i];
}

constexpr int keyBelow(int w)
{
	const int q = floorDiv(w - 9216, 1024), r = w - 9216 - 1024 * q;
	if (r == 0)
		return 9216 + 1024 * (q - 1) + kKeyOffset[2];
	int i = 2;
	while (kKeyOffset[i] >= r)
		--i;
	return 9216 + 1024 * q + kKeyOffset[i];
}
}

class Portamento
{
public:
	Portamento()
	{
		m_target.fill(pitch::kPowerOnKeyWord);
		m_out.fill(pitch::kPowerOnKeyWord);
		m_pos.fill(pitch::kPowerOnKeyWord);
	}

	void setPedal(bool on) { m_pedal = on; }

	// poly/mono and portamento function changes: every word back to the power-on key word, the
	// previous note (retain start) and the pedal stay
	void resetVoices()
	{
		m_target.fill(pitch::kPowerOnKeyWord);
		m_out.fill(pitch::kPowerOnKeyWord);
		m_pos.fill(pitch::kPowerOnKeyWord);
		m_keyed.fill(false);
	}

	// new target word for a voice; legato = mono note change without retrigger
	void noteOn(int voice, uint16_t target, bool legato, const Global& g)
	{
		const size_t v = size_t(voice);
		if (!glides(g))
			jump(v, target);
		else if (g.mono)
			g.portaMode == 0 && !legato ? jump(v, target) : retarget(v, target);
		else if (g.portaMode == 0)
		{
			// retain: the new voice starts at the previous note's key word
			m_out[v] = m_pos[v] = m_lastTarget;
			retarget(v, target);
		}
		else
		{
			// follow: every voice not held by a key glides to the new note
			for (size_t i = 0; i < m_out.size(); ++i)
				if (i != v && !m_keyed[i])
					retarget(i, target);
			retarget(v, target);
		}
		m_keyed[v] = true;
		m_lastTarget = target;
	}

	void keyOff(int voice) { m_keyed[size_t(voice)] = false; }

	// called each time this voice's pitch is refreshed; returns the current word
	uint16_t update(int voice, const Global& g)
	{
		const size_t v = size_t(voice);
		const int t = m_target[v];
		if (!glides(g))
			jump(v, uint16_t(t));
		else if (!g.glissando)
			m_out[v] = m_pos[v] = uint16_t(porta::glideStep(m_out[v], t, rate(g)));
		else
			glissTick(v, t, 3 * rate(g), g.masterTune);
		return m_out[v];
	}

	uint16_t current(int voice) const { return m_out[size_t(voice)]; }

private:
	bool glides(const Global& g) const { return m_pedal && g.portaTime > 0; }
	static int rate(const Global& g) { return porta::kRate[g.portaTime > 99 ? 99 : g.portaTime]; }

	void jump(size_t v, uint16_t target) { m_target[v] = m_out[v] = m_pos[v] = target; }

	// the glissando position keeps its lead over the output across a new target
	void retarget(size_t v, uint16_t target) { m_target[v] = target; }

	// position moves 3 * rate per tick; the output steps one key per tick while the position is 340 or more past it
	void glissTick(size_t v, int t, int speed, int tune)
	{
		const int cur = m_pos[v];
		const int pos = t > cur ? (cur + speed > t ? t : cur + speed) : (cur - speed < t ? t : cur - speed);
		m_pos[v] = uint16_t(pos);
		const int out = m_out[v];
		if (out == t)
			return;
		const bool up = t > out;
		int next = (up ? porta::keyAbove(out - tune) : porta::keyBelow(out - tune)) + tune;
		if (up ? next > t : next < t)
			next = t;
		if ((up ? pos - out : out - pos) >= porta::kGlissStep || pos == t)
			m_out[v] = uint16_t(next);
	}

	bool m_pedal = true;
	// power-on previous note: key 0 (pitch::keyWord clamps it to key 1, 9560)
	uint16_t m_lastTarget = pitch::keyWord(0);
	std::array<uint16_t, regs::kVoices> m_target{};
	std::array<uint16_t, regs::kVoices> m_out{};
	std::array<uint16_t, regs::kVoices> m_pos{};
	std::array<bool, regs::kVoices> m_keyed{};
};
}
