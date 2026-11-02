#pragma once

#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// INTERFACE.md 4.4 and 4.10, measurements lfo/ and vib/.
	// One generator for LFO 1/2 (per voice) and the vibrato (global): a 16 bit level running up and
	// down between 0 and 65535 by step(speed) per voice refresh block, clamped at both ends (the
	// remainder is lost, so each turn costs part of a step), a 16 bit shift register for random
	// and noise, and a sample and hold. Waves read the level as an 8 bit value t (0-255).
	// Time is counted in blocks: Vintage advances 1.0 per block, Smooth by the fraction of a
	// block that elapsed.
	namespace lfoCurve
	{
		// level step per block for speed 0-63 (clamped). Measured table, no formula found:
		// 39-63 are close to 65535 / N for N = 32, 30, 28, 26, 24, 22, 20, 19 ... 3, 2
		uint16_t step(int _speed);

		// speed offset from a 7 bit source and a signed amount: source * amount / 128 (lfo/speedby*, vibamt/smod1_*)
		int modOffset(int _source7, int _amount);

		// output level: trunc(b * U / 256), b = t - 128, U = amplitude multiplier 0-255.
		// U = 255 gives -127..126 with a three value dead zone at 0 (lfo/, lfoamp/)
		int scale(int _t, int _mul);

		// patch amplitude 0-63 to the multiplier: 4 * amp, 63 counts as 255 (lfoamp/amp*_sq40)
		int ampBase(int _amp);
		// modulation of the multiplier by an 8 bit source level s (0-255) and a signed amount:
		// sign * ((s * T(|amount|)) >> 8), T = the envelope amplitude curve of the matrix
		// (amount up to 44, then a table to 255; lfoamp/ampbyramp_*, vibamt/amp0_amod1_*)
		int ampMod(int _s, int _amount);
		// ramp as seen by amp by ramp: (level * 13) >> 11, max 255, i.e. 1.625 x the matrix level.
		// Fitted on lfoamp/ampbyramp_*_rate30 with the envelope module's ramp step (841 per block
		// at rate 30); the matrix bus reads level >> 8 (ramp/). Saturates at 65% of the ramp.
		inline int rampLevel(const int32_t _level) { const int s = static_cast<int>((static_cast<int64_t>(_level) * 13) >> 11); return s > 255 ? 255 : s; }
		inline int clampMul(const int _u) { return _u < 0 ? 0 : (_u > 255 ? 255 : _u); }

		// LFO output to the Mod scale used by the matrix: matrix level s = output level
		// (matrix/ grid lfo1 -> mix +-63: swing 2016 at amplitude 63, fixed_routes P01 +-8 semitones)
		constexpr Mod toMod(const int _level) { return _level * 64; }
	}

	class LfoGen
	{
	public:
		struct State
		{
			uint16_t level = 0;
			bool up = true;
			uint16_t lfsr = 1;
			uint8_t held = 0;
		};

		void reset(const State& _s);

		// returns true when the level reached the top in this advance
		bool advance(int _speed, double _blocks);
		void retrigger(uint8_t _point);
		uint8_t shift();
		// true when _blocks completes a whole block (noise steps once per block in Smooth mode too)
		bool countBlocks(double _blocks);

		uint8_t triangle() const { return static_cast<uint8_t>(level() >> 8); }
		uint8_t upSaw() const;
		uint8_t square() const { return m_up ? 255 : 128; }
		uint8_t random() const { return static_cast<uint8_t>(m_lfsr >> 8); }

		uint16_t level() const { return static_cast<uint16_t>(m_level); }
		bool up() const { return m_up; }

		uint8_t held = 0;

	private:
		double m_level = 0.0;
		bool m_up = true;
		uint16_t m_lfsr = 1;
		double m_blocks = 0.0;
	};

	// wave code 0-7 to t, stepping the shift register and the sample and hold as the wave needs.
	// _sample: t of the sampled source (wave 6/7), -1 = the vibrato's constants
	uint8_t lfoWave(LfoGen& _g, uint8_t _wave, bool _top, int _sample, bool _newBlock);

	// INTERFACE.md 4.4. Output: level -127..126 as Mod (lfoCurve::toMod), after amplitude.
	class Lfo
	{
	public:
		struct Params
		{
			int speed = 0;                 // incl. modulation, clamped to 0-63
			uint8_t wave = 0;              // 0-7, 7 behaves like 6
			LfoTrigger trigger = LfoTrigger::None;
			uint8_t retrigger = 0;
			bool lag = false;
			int sampleT = 0;               // sampled source as t (0-255)
			int amp = 255;                 // multiplier 0-255 (lfoCurve::ampBase + ampMod)
		};

		void reset(const LfoGen::State& _boot);
		// boot program (before the first 0D load): the level runs at speed 0 by time, one step per
		// BootTickUs from BootStartUs (env4/speed30_load_at_500ms, _1000ms: 128 and 285-291 steps)
		static constexpr double BootStartUs = 100000.0;
		static constexpr double BootTickUs = 3125.0;
		void bootProgram(int64_t _steps);
		void keyTrigger(const Params& _p, bool _retarget);
		void externalTrigger(const Params& _p);
		Mod tick(const Params& _p, double _blocks);
		bool wrapped() const { return m_top; }   // reached the top in the last tick (env LFO trigger)
		bool rising() const { return m_gen.up(); }   // env LFO trigger: bottom = half cycle
		uint8_t t() const { return m_t; }
		const LfoGen& generator() const { return m_gen; }

		// measured state at time 0 of a cold boot, voice 0-5, lfo 0-1
		static LfoGen::State bootState(uint32_t _voice, uint32_t _lfo);
		// what wave 6 samples with sample source 0 (unused): a constant per voice (lfo/wave6_*)
		static int sourceZeroT(uint32_t _voice);

	private:
		LfoGen m_gen;
		bool m_pending = false;
		bool m_top = false;
		uint8_t m_t = 128;
	};

	// INTERFACE.md 4.10. Global. Stepped once per voice block (six times per refresh cycle).
	class Vibrato
	{
	public:
		void reset();
		// _amp: multiplier 0-255
		Mod tick(int _speed, uint8_t _wave, int _amp, double _blocks);
		Mod value() const { return m_value; }

		// mod source code 0-3: 0 off, 1 lever 2, 2 and 3 pedal 1 (measured)
		static int sourceIndex(uint8_t _code) { return _code == 0 ? 0 : (_code == 1 ? 1 : 2); }

	private:
		LfoGen m_gen;
		Mod m_value = 0;
	};
}
