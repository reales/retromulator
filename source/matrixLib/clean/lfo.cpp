#include "lfo.h"

#include <cmath>

#include "matrix.h"

namespace matrixClean
{
	namespace lfoCurve
	{
		namespace
		{
			// lfo/speed_tri_*: unique fits for 0-38 and 40, 42, 44; elsewhere the lowest step that
			// reproduces every measured block (the data allows up to +255 at speed 63)
			constexpr uint16_t g_step[64] =
			{
				44, 62, 74, 88, 102, 116, 132, 148, 168, 172, 192, 214, 242, 276, 312, 356,
				410, 462, 520, 586, 656, 720, 780, 830, 886, 936, 992, 1058, 1130, 1192, 1262, 1338,
				1424, 1490, 1560, 1638, 1724, 1820, 1928, 2048, 2184, 2339, 2520, 2730, 2978, 3275, 3450, 3639,
				3856, 4096, 4369, 4682, 5042, 5462, 5958, 6554, 7282, 8192, 9363, 10923, 13107, 16384, 21845, 32768
			};
		}

		uint16_t step(const int _speed)
		{
			return g_step[_speed < 0 ? 0 : (_speed > 63 ? 63 : _speed)];
		}

		int modOffset(const int _source7, const int _amount)
		{
			return _source7 * _amount / 128;
		}

		int scale(const int _t, const int _mul)
		{
			return (_t - 128) * _mul / 256;
		}

		int ampBase(const int _amp)
		{
			return _amp >= 63 ? 255 : 4 * (_amp < 0 ? 0 : _amp);
		}

		int ampMod(const int _s, const int _amount)
		{
			const int m = (_s * matrix::amountTable(matrix::Curve::EnvAmp, _amount)) >> 8;
			return _amount < 0 ? -m : m;
		}
	}

	// LfoGen

	void LfoGen::reset(const State& _s)
	{
		m_level = _s.level;
		m_up = _s.up;
		m_lfsr = _s.lfsr;
		held = _s.held;
	}

	bool LfoGen::advance(const int _speed, const double _blocks)
	{
		const double d = lfoCurve::step(_speed) * _blocks;
		if(m_up)
		{
			m_level += d;
			if(m_level >= 65535.0)
			{
				m_level = 65535.0;
				m_up = false;
				return true;
			}
			return false;
		}
		m_level -= d;
		if(m_level <= 0.0)
		{
			m_level = 0.0;
			m_up = true;
		}
		return false;
	}

	bool LfoGen::countBlocks(const double _blocks)
	{
		const double before = std::floor(m_blocks);
		m_blocks += _blocks;
		return std::floor(m_blocks) > before;
	}

	void LfoGen::retrigger(const uint8_t _point)
	{
		// saw restarts at 0x8000 + p * 0x400, plus 0x80 for p >= 16 (lfo/retrig*), on the way down
		const int p = _point & 31;
		m_level = 2.0 * (32767 - (p << 10) - ((p & 16) << 3));
		m_up = false;
	}

	void Lfo::bootProgram(const int64_t _steps)
	{
		// speed 0 bounces between 0 and 65535: from the first turn on the state repeats every period
		const int64_t s = lfoCurve::step(0);
		const int64_t period = 2 * ((65535 + s - 1) / s);
		int64_t i = 0;
		while(i < _steps)
		{
			const bool up = m_gen.up();
			m_gen.advance(0, 1.0);
			++i;
			if(m_gen.up() != up)
				break;
		}
		for(int64_t n = i < _steps ? (_steps - i) % period : 0; n > 0; --n)
			m_gen.advance(0, 1.0);
	}

	uint8_t LfoGen::shift()
	{
		const uint16_t bit = (m_lfsr ^ (m_lfsr >> 3)) & 1;
		m_lfsr = static_cast<uint16_t>((m_lfsr >> 1) | (bit << 15));
		return random();
	}

	uint8_t LfoGen::upSaw() const
	{
		const uint16_t l = level();
		return static_cast<uint8_t>((m_up ? (l >> 1) : (65535 - (l >> 1))) >> 8);
	}

	uint8_t lfoWave(LfoGen& _g, const uint8_t _wave, const bool _top, const int _sample, const bool _newBlock)
	{
		switch(_wave & 7)
		{
		case 0: return _g.triangle();
		case 1: return _g.upSaw();
		case 2: return static_cast<uint8_t>(255 - _g.upSaw());
		case 3: return _g.square();
		case 4: return _top ? _g.shift() : _g.random();
		case 5: return _newBlock ? _g.shift() : _g.random();
		default:
			if(_top)
				_g.held = static_cast<uint8_t>(_sample >= 0 ? _sample : 190);
			// vibrato code 7 reads the held value without bit 7 (vib/wave_7)
			return _sample < 0 && (_wave & 7) == 7 ? (_g.held & 0x7f) : _g.held;
		}
	}

	// Lfo

	LfoGen::State Lfo::bootState(const uint32_t _voice, const uint32_t _lfo)
	{
		// lfo/: state before the first block after the 0D patch. The init patch does not use the
		// LFOs, so they are not computed (not stepped) until a patch uses them. Derivation:
		// ab/lfoderive.py
		static constexpr LfoGen::State s[2][6] =
		{
			{{36182, false, 0x014f, 138}, {56478, false, 0x01af, 42}, {53158, false, 0x018c, 214},
			 {25986, false, 0x01e4, 142}, {41266, false, 0x01b8, 82}, {32418, false, 0x0109, 34}},
			{{35092, true, 0x8801, 50}, {16183, false, 0x6e03, 210}, {26150, true, 0xe002, 206},
			 {34945, false, 0x3802, 39}, {62420, true, 0x1a02, 219}, {56786, true, 0x0401, 235}}
		};
		return s[_lfo & 1][_voice % 6];
	}

	int Lfo::sourceZeroT(const uint32_t _voice)
	{
		static constexpr int t[6] = {0, 0, 232, 92, 0, 136};   // ab/lfoderive.py
		return t[_voice % 6];
	}

	void Lfo::reset(const LfoGen::State& _boot)
	{
		*this = Lfo();
		m_gen.reset(_boot);
	}

	void Lfo::keyTrigger(const Params& _p, const bool _retarget)
	{
		// measured in poly mode: single and multi both restart on every new note
		if(_p.trigger == LfoTrigger::Multi || (_p.trigger == LfoTrigger::Single && !_retarget))
			m_pending = true;
	}

	void Lfo::externalTrigger(const Params& _p)
	{
		// pedal 2 restarts every voice, keys held or not
		if(_p.trigger == LfoTrigger::External)
			m_pending = true;
	}

	Mod Lfo::tick(const Params& _p, const double _blocks)
	{
		// a restart replaces that block's step
		if(m_pending)
		{
			m_gen.retrigger(_p.retrigger);
			m_pending = false;
			m_top = false;
		}
		else
		{
			m_top = m_gen.advance(_p.speed, _blocks);
		}
		m_t = lfoWave(m_gen, _p.wave, m_top, _p.sampleT, m_gen.countBlocks(_blocks));
		// lag 1 measured identical to lag 0 on the fixed routes (lag*_porta*)
		return lfoCurve::toMod(lfoCurve::scale(m_t, _p.amp));
	}

	// Vibrato

	void Vibrato::reset()
	{
		*this = Vibrato();
		// vib/: level 0 two blocks before time 0, then boot speed 40. Derivation: ab/lfoderive.py
		m_gen.reset({4368, true, 0x01cb, 231});
	}

	Mod Vibrato::tick(const int _speed, const uint8_t _wave, const int _amp, const double _blocks)
	{
		const bool top = m_gen.advance(_speed, _blocks);
		const uint8_t t = lfoWave(m_gen, _wave, top, -1, m_gen.countBlocks(_blocks));
		m_value = lfoCurve::toMod(lfoCurve::scale(t, _amp));
		return m_value;
	}
}
