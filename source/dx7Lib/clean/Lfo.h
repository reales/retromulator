#pragma once
#include <array>
#include <cstdint>

#include "Global.h"
#include "Patch.h"

// LFO, fitted from measurements/lfo (see fits/lfo/FIT.md). One step per control pass
namespace dx7clean
{
namespace lfo
{
// 0-99 -> 0-254 for speed, PMD and AMD
constexpr int scale99(int v) { return v <= 0 ? 0 : (165 * v - 64) >> 6; }

// 16-bit phase increment per pass
constexpr int speedInc(int speed)
{
	const int x = scale99(speed);
	const int k = (x - 115) >> 2;
	return (x + 1) * (k > 11 ? k : 11);
}

// 16-bit delay counter increment per pass, delay 1-99
constexpr int delayInc(int delay)
{
	const int j = 99 - delay;
	return (16 + (j & 15)) << ((j >> 4) + 2);
}

constexpr std::array<uint8_t, 8> kPms = {0, 10, 20, 33, 55, 92, 153, 255};

// half sine, round(127 sin(pi (i + 0.5) / 128))
constexpr std::array<uint8_t, 128> kSine = {
	2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 58, 61, 64, 67, 69, 72, 74, 77, 79, 82, 84, 86, 89,
	91, 93, 95, 97, 99, 101, 103, 105, 106, 108, 110, 111, 113, 114, 115, 117, 118, 119, 120, 121, 122, 123, 124, 124, 125, 125, 126, 126, 127, 127,
	127, 127, 127, 127, 127, 127, 126, 126, 125, 125, 124, 124, 123, 122, 121, 120, 119, 118, 117, 115, 114, 113, 111, 110, 108, 106, 105, 103, 101, 99,
	97, 95, 93, 91, 89, 86, 84, 82, 79, 77, 74, 72, 69, 67, 64, 61, 58, 56, 53, 50, 47, 44, 41, 38, 35, 32, 29, 26, 23, 20, 17, 14, 11, 8, 5, 2};

// sign-magnitude 8x8 multiply, truncates toward zero
constexpr int smul(int a, int s)
{
	const int q = ((a < 0 ? -a : a) * s) >> 8;
	return a < 0 ? -q : q;
}

constexpr int floorHalf(int x) { return x >= 0 ? x >> 1 : -((1 - x) >> 1); }
}

class Lfo
{
public:
	// program change keeps phase, S&H and delay state (lfo_extra/program_change_in_delay)
	void setPatch(const Vced& v)
	{
		m_inc = uint32_t(lfo::speedInc(v[vced::LFS] > 99 ? 99 : v[vced::LFS]));
		m_delay = v[vced::LFD] > 99 ? 99 : v[vced::LFD];
		m_delayInc = m_delay ? uint32_t(lfo::delayInc(m_delay)) : 0;
		m_rampStep = m_delayInc >> 8 ? int(m_delayInc >> 8) : 1;
		m_pmd = lfo::scale99(v[vced::LPMD] > 99 ? 99 : v[vced::LPMD]);
		m_amd = lfo::scale99(v[vced::LAMD] > 99 ? 99 : v[vced::LAMD]);
		m_sync = v[vced::LFKS] != 0;
		m_wave = v[vced::LFW] > 5 ? 5 : v[vced::LFW];
		m_pms = lfo::kPms[v[vced::PMS] & 7];
		if (!m_delay)
		{
			m_delayDone = true;
			m_ramp = 255;
		}
	}

	// every key on event restarts the delay, held keys or not; a mono legato note has none. Key sync: next pass wraps
	void keyOn(bool /*firstKey*/)
	{
		if (m_sync)
			m_phase = 0xffff;
		if (m_delay)
		{
			m_delayDone = false;
			m_delayCount = 0;
			m_ramp = 0;
		}
	}

	// once per control pass; controller depths are added after the delay ramp (lfo_extra/wheel_with_delay)
	template <class Ctrl>
	void tick(const Ctrl& ctrl, const Global& g)
	{
		const uint32_t p = m_phase + m_inc;
		if (p > 0xffff)
			m_sh = uint8_t(179 * m_sh + 17);
		m_phase = p & 0xffff;

		if (!m_delayDone)
		{
			m_delayCount += m_delayInc;
			m_delayDone = m_delayCount > 0xffff;
		}
		if (m_delayDone && m_ramp < 255)
			m_ramp = m_ramp + m_rampStep > 255 ? 255 : m_ramp + m_rampStep;

		const int w = value();
		const int pd = ctrl.pitchDepth(ramped(m_pmd), g);
		const int ad = ctrl.ampDepth(ramped(m_amd), g);
		m_pitchMod = lfo::floorHalf(lfo::smul(w, m_pms) * pd);
		m_ampMod = ((127 - w) * ad) >> 8;
	}

	// LFO share of the pitch mod register (depth and PMS applied)
	int pitchMod() const { return m_pitchMod; }
	// LFO share of the linear amp mod index, 0-255
	int ampMod() const { return m_ampMod; }

	// wave value -128..127 at the current phase
	int value() const
	{
		const uint32_t p = m_phase;
		switch (m_wave)
		{
		case 0: { const int t = int(p >> 7); return 127 - (t < 256 ? t : 511 - t); }
		case 1: return 127 - int(p >> 8);
		case 2: return int(p >> 8) - 128;
		case 3: return p < 0x8000 ? -128 : 127;
		case 4: { const int s = lfo::kSine[(p >> 8) & 127]; return p < 0x8000 ? ~s : s; }
		default: return int(int8_t(m_sh));
		}
	}

private:
	int ramped(int depth) const { return int((uint32_t(depth) * uint32_t(m_ramp) * 1029) >> 18); }

	uint32_t m_phase = 0xf055; // power-on phase fitted to the first run of the speed_wave timelines (FIT.md)
	uint32_t m_inc = 11;
	uint8_t m_sh = 0;
	int m_wave = 0;
	bool m_sync = false;
	int m_pmd = 0;
	int m_amd = 0;
	int m_pms = 0;

	int m_delay = 0;
	uint32_t m_delayInc = 0;
	uint32_t m_delayCount = 0;
	bool m_delayDone = true;
	int m_rampStep = 1;
	int m_ramp = 255;

	int m_pitchMod = 0;
	int m_ampMod = 0;
};
}
