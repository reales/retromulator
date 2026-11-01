#pragma once
#include <array>
#include <cstdint>

#include "Global.h"
#include "Lfo.h"
#include "Patch.h"

// Pitch bend, mod wheel / breath / foot / aftertouch, EG bias and volume DAC, fitted from
// measurements/controllers (see fits/controllers/FIT.md)
namespace dx7clean
{
namespace ctl
{
// 0-99 range -> 0-255
constexpr int rangeScale(int range) { return (range * 660) >> 8; }

// depth share of one controller (0-253) for a 0-127 controller value
constexpr int depth(int range, int value) { return (2 * value * rangeScale(range)) >> 8; }

// amp mod register for a linear attenuation index 0-255 (bias + LFO share)
constexpr std::array<uint8_t, 256> kAmpMod = {
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12,
	13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19,
	20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28,
	28, 29, 29, 29, 30, 30, 30, 31, 31, 32, 32, 32, 33, 33, 33, 34, 34, 34, 35, 35, 36, 36, 36, 37, 37, 38, 38, 38,
	39, 39, 40, 40, 41, 41, 42, 42, 42, 43, 43, 44, 44, 45, 45, 46, 46, 47, 47, 48, 48, 49, 49, 50, 50, 51, 51, 52,
	53, 53, 54, 54, 55, 56, 56, 57, 57, 58, 59, 59, 60, 61, 61, 62, 63, 64, 64, 65, 66, 66, 67, 68, 69, 70, 70, 71,
	72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 88, 89, 90, 91, 93, 94, 96, 97, 98, 100, 102, 103, 105,
	107, 109, 111, 113, 115, 117, 120, 122, 125, 128, 130, 134, 137, 141, 145, 149, 154, 160, 166, 173, 181, 192, 205, 224,
	255, 255};

// pitch bend range 1-11 gain per bend unit; range 12 is 256 with its own rounding
constexpr std::array<uint16_t, 12> kBendGain = {0, 22, 43, 65, 86, 107, 129, 151, 172, 194, 215, 236};

// bend magnitude 0-64 from the MIDI MSB, dead zone 63-65, one unit skipped at 24 from the centre
constexpr int bendUnits(int msb)
{
	const int d = msb > 64 ? msb - 64 : 64 - msb;
	return d <= 1 ? 0 : d <= 23 ? d - 1 : d;
}

// floor(n * 4096 / 3), 12 semitones capped at 0x3fff
constexpr int semitones(int n)
{
	const int w = n * 0x4000 / 12;
	return w > 0x3fff ? 0x3fff : w;
}

// pitch mod register share of the bend wheel
constexpr int bend(int msb, int range, int step)
{
	const int u = bendUnits(msb);
	if (u == 0)
		return 0;
	const bool up = msb > 64;
	if (step > 0)
	{
		if (step > 12)
			step = 12;
		const int steps = 12 / step;
		const int k = (up ? 2 * u : 2 * u - 1) * (2 * steps + 1) / 256;
		const int w = k * semitones(step);
		return up || k == 0 ? w : ~w;
	}
	if (range >= 12)
		return up ? (msb == 127 ? 0x3fff : 256 * u) : -256 * u;
	const int g = kBendGain[size_t(range)];
	return up ? u * g : ~(u * g - ((g + 1) >> 1));
}
}

class Controllers
{
public:
	void wheel(uint8_t v) { set(Wheel, v); }
	void breath(uint8_t v) { set(Breath, v); }
	void foot(uint8_t v) { set(Foot, v); }
	void aftertouch(uint8_t v) { set(Aftertouch, v); }
	void pitchBend(uint16_t value14) { m_bend = value14; }

	// EG bias stays 0 from power-on until a function parameter (or controller) message arrives:
	// renders without function settings show 0xF0 = 0, the power-on assigns (range 99, assign 7) give 255 after
	void functionChanged() { m_live = true; }

	// combined LFO depth 0-255: lfoDepth is the patch PMD/AMD on the same scale (PMD/AMD 50 -> 127)
	int pitchDepth(int lfoDepth, const Global& g) const { return sum(lfoDepth, g, 1); }
	int ampDepth(int lfoDepth, const Global& g) const { return sum(lfoDepth, g, 2); }

	// EG bias share of the linear amp index, 0 when no controller has bias assigned
	int biasIndex(const Global& g) const
	{
		int r = 0, d = 0;
		bool any = false;
		for (int i = 0; i < Count; ++i)
		{
			const auto& c = ctrl(g, i);
			if (!(c.assign & 4))
				continue;
			any = true;
			r += ctl::rangeScale(c.range);
			d += ctl::depth(c.range, m_value[i]);
		}
		if (!any || !m_live)
			return 0;
		const int b = sat(r) - sat(d);
		return b < 0 ? 0 : b;
	}

	int bend(const Global& g) const { return ctl::bend(m_bend >> 7, g.bendRange, g.bendStep); }

	// lfoAmpIndex: LFO share of the linear index, (ampDepth() * unipolar wave 0-255) >> 8
	uint8_t ampMod(int lfoAmpIndex, const Global& g) const
	{
		const int i = biasIndex(g) + (lfoAmpIndex < 0 ? 0 : lfoAmpIndex);
		return ctl::kAmpMod[size_t(sat(i))];
	}

	// lfoPitch: LFO share already scaled with pitchDepth() and PMS. Sum with bend not measured
	static int16_t pitchMod(int lfoPitch, int bend)
	{
		const int w = lfoPitch + bend;
		return int16_t(w < -32768 ? -32768 : w > 32767 ? 32767 : w);
	}

	// Core entry points. Lfo::pitchMod()/ampMod() must return the shares described above
	int16_t pitchModReg(const Lfo& lfo, const Vced&, const Global& g) const { return pitchMod(lfo.pitchMod(), bend(g)); }
	uint8_t ampModReg(const Lfo& lfo, const Vced&, const Global& g) const { return ampMod(lfo.ampMod(), g); }

	// CC7 -> DAC step: top 3 bits reversed (measurements/controllers/volume_cc7.csv, 128/128)
	static uint8_t volumeDac(uint8_t cc7)
	{
		const int s = (cc7 >> 4) & 7;
		return uint8_t((s & 1) << 2 | (s & 2) | (s & 4) >> 2);
	}

private:
	enum { Wheel, Breath, Foot, Aftertouch, Count };

	static int sat(int x) { return x > 255 ? 255 : x; }

	void set(int i, uint8_t v)
	{
		m_value[size_t(i)] = v;
		m_live = true;
	}

	static const Global::Ctrl& ctrl(const Global& g, int i)
	{
		return i == Wheel ? g.wheel : i == Breath ? g.breath : i == Foot ? g.foot : g.aftertouch;
	}

	int sum(int base, const Global& g, int bit) const
	{
		int d = base;
		for (int i = 0; i < Count; ++i)
			if (ctrl(g, i).assign & bit)
				d += ctl::depth(ctrl(g, i).range, m_value[i]);
		return sat(d);
	}

	std::array<uint8_t, Count> m_value{};
	uint16_t m_bend = 0x2000;
	bool m_live = false;
};
}
