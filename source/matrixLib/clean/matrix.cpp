#include "matrix.h"

namespace matrixClean
{
	namespace matrix
	{
		namespace
		{
			// 1/8 code at full source; fitted to measurements/matrix amount_sweep, grid
			constexpr int16_t g_exp[64] = {
				0, 8, 16, 24, 32, 40, 48, 64, 80, 96, 112, 128, 148, 168, 192, 216,
				240, 288, 320, 352, 384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704, 736,
				800, 928, 1056, 1216, 1376, 1568, 1792, 2048, 2336, 2656, 3008, 3456, 3936, 4096, 4608, 5408,
				6144, 6816, 8192, 8832, 10240, 12288, 14336, 16384, 18432, 20480, 22528, 24576, 26624, 28672, 30720, 32640};

			// 1/256 semitone at full source (DCO frequency destinations); exact against the dco
			// count table (matrix/ amount_sweep, grid, base_minmax)
			constexpr int16_t g_pitch[64] = {
				0, 0, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 18, 21, 24, 27,
				30, 33, 36, 40, 44, 48, 52, 56, 60, 65, 70, 75, 80, 85, 90, 97,
				104, 114, 124, 134, 144, 156, 168, 181, 206, 230, 255, 320, 384, 448, 512, 544,
				608, 704, 768, 832, 960, 1056, 1152, 1344, 1568, 1792, 2080, 2240, 2592, 3104, 3616, 4160};

			// knee table: envelope amplitude (multiplier steps) and stage rates/delay steps (matrix2/), a up to 44, then this
			constexpr uint8_t g_envAmp[19] = {48, 52, 57, 62, 68, 74, 81, 88, 96, 105, 125, 136, 148, 161, 176, 192, 209, 228, 255};

			int32_t linear(const int _a)
			{
				// VCA1: 64 codes per step less 16 per started group of 16, amount 63 = 4016
				if(_a >= 63)
					return 4016 * 8;
				return (64 * _a - 16 * ((_a + 15) / 16)) * 8;
			}
		}

		int32_t amountTable(const Curve _c, int _amount)
		{
			if(_amount < 0) _amount = -_amount;
			if(_amount > 63) _amount = 63;
			switch(_c)
			{
			case Curve::Exp: return g_exp[_amount];
			case Curve::Linear: return linear(_amount);
			case Curve::Pitch: return g_pitch[_amount];
			case Curve::EnvAmp: return _amount <= 44 ? _amount : g_envAmp[_amount - 45];
			case Curve::Unit: return _amount;   // LFO speed: one step per amount (matrix2/ lfo1_speed)
			}
			return 0;
		}

		int sourceLevel(const Mod _m)
		{
			const int s = _m >> 6;
			return s < -256 ? -256 : (s > 255 ? 255 : s);
		}

		int32_t route(const int _s, const int _amount, const Curve _c)
		{
			if(!_s || !_amount)
				return 0;
			const int32_t mag = static_cast<int32_t>((static_cast<int64_t>(_s < 0 ? -_s : _s) * amountTable(_c, _amount)) >> 8);
			return (_s < 0) != (_amount < 0) ? -mag : mag;
		}

		int32_t routeFine(const Mod _m, const int _amount, const Curve _c)
		{
			if(!_m || !_amount)
				return 0;
			const int64_t m = _m < -256 * 64 ? 256 * 64 : (_m < 0 ? -_m : (_m > 255 * 64 ? 255 * 64 : _m));
			const int32_t mag = static_cast<int32_t>((m * amountTable(_c, _amount)) >> 14);
			return (_m < 0) != (_amount < 0) ? -mag : mag;
		}

		Mod keyboard(const uint8_t _note)
		{
			const int k = (static_cast<int>(_note) - 36) * 4;
			return static_cast<Mod>(k < 0 ? 0 : (k > 255 ? 255 : k)) * 64;
		}

		int32_t bendPitch(const uint8_t _b8, const int _range)
		{
			// the top value reaches the full range, every other step is range / 128
			if(_b8 == 255)
				return _range * 256;
			return _range * 2 * (static_cast<int32_t>(_b8) - 128);
		}

		int32_t baseMix(const uint8_t _p) { return (64 * (_p & 63) + 32) * 8; }
		int32_t baseFm(const uint8_t _p) { return 64 * (_p & 63) * 8; }
		int32_t baseShape(const uint8_t _p) { return (64 * (_p & 63) + 32) * 8; }

		int32_t baseRes(uint8_t _p)
		{
			_p &= 63;
			if(_p == 63)
				return AccMax;
			const int32_t c = 48 * _p + 8 * (_p / 15) - 8;
			return (c < 0 ? 0 : c) * 8;
		}

		int32_t baseVca1(uint8_t _p)
		{
			_p &= 63;
			if(_p == 63)
				return 4000 * 8;
			return (64 * _p - 32 * ((_p + 31) / 32)) * 8;
		}

		int32_t basePw(uint8_t _p)
		{
			_p &= 63;
			return (_p == 63 ? 4064 : 64 * _p + 64) * 8;
		}

		int32_t applyBuses(const Patch& _p, const Sources& _src, const Dest _d, int32_t _acc, const Curve _c)
		{
			for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			{
				const auto bus = _p.bus(b);
				if(bus.dest != _d || bus.source == Source::None)
					continue;
				_acc = sat(_acc, route(sourceLevel(_src[static_cast<size_t>(bus.source)]), bus.amount, _c));
			}
			return _acc;
		}

		int32_t busSum(const Patch& _p, const Sources& _src, const Dest _d, const Curve _c)
		{
			int32_t sum = 0;
			for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			{
				const auto bus = _p.bus(b);
				if(bus.dest != _d || bus.source == Source::None)
					continue;
				sum += _c == Curve::Pitch ? routeFine(_src[static_cast<size_t>(bus.source)], bus.amount, _c)
					: route(sourceLevel(_src[static_cast<size_t>(bus.source)]), bus.amount, _c);
			}
			return sum;
		}

		Active activeSources(const Patch& _p)
		{
			Active a;
			bool used[static_cast<size_t>(Source::Count)] = {};
			for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			{
				const auto bus = _p.bus(b);
				if(bus.source != Source::None && bus.dest != Dest::None)
					used[static_cast<size_t>(bus.source)] = true;
			}
			auto mark = [&](const Source _s, const int _amount) { if(_amount) used[static_cast<size_t>(_s)] = true; };
			mark(Source::Lfo1, _p.s(86));
			mark(Source::Lfo2, _p.s(87));
			mark(Source::Lfo1, _p.s(88));
			mark(Source::Lfo2, _p.s(89));
			mark(Source::Env1, _p.s(90));
			mark(Source::Env2, _p.s(93));
			mark(Source::Ramp1, _p.s(97));
			mark(Source::Ramp2, _p.s(98));
			mark(Source::Env3, _p.s(100));

			if(used[static_cast<size_t>(Source::Tracking)])
				used[static_cast<size_t>(_p.trackInput())] = true;
			for(uint32_t i = 0; i < 2; ++i)
			{
				const auto l = _p.lfo(i);
				if(used[static_cast<size_t>(Source::Lfo1) + i] && l.wave == LfoWave::Sampled)
					used[static_cast<size_t>(l.sampleSource)] = true;
			}
			for(uint32_t i = 0; i < 3; ++i)
			{
				if(used[static_cast<size_t>(Source::Env1) + i] && _p.env(i).lfoTrigger)   // 1 and 3 behave alike (env/lfotrig1)
					used[static_cast<size_t>(Source::Lfo1)] = true;
			}

			for(uint32_t i = 0; i < 3; ++i)
				a.env[i] = used[static_cast<size_t>(Source::Env1) + i];
			for(uint32_t i = 0; i < 2; ++i)
			{
				a.lfo[i] = used[static_cast<size_t>(Source::Lfo1) + i];
				a.ramp[i] = used[static_cast<size_t>(Source::Ramp1) + i];
			}
			return a;
		}
	}
}
