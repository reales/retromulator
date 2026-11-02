#include "cvFilterAmp.h"
#include "matrix.h"

namespace matrixClean::cvFilterAmp
{
	namespace
	{
		constexpr int32_t U16Max = 2 * matrix::AccMax;   // 1/8 code accumulator saturates at 32767

		// per voice VCF calibration: DAC code * 8 at u16 = 8192 k (every 16 semitones)
		constexpr int32_t VcfCal[VoiceCount][9] =
		{
			{35608, 31441, 27016, 22777, 18444, 14132, 9823, 5514, 1205},
			{35608, 31449, 27211, 22809, 18448, 14132, 9817, 5502, 1187},
			{35608, 31455, 27019, 22720, 18444, 14134, 9825, 5516, 1207},
			{35608, 31459, 27059, 22801, 18442, 14134, 9830, 5526, 1222},
			{35603, 31534, 27055, 22801, 18450, 14136, 9827, 5518, 1209},
			{35608, 31493, 27146, 22791, 18442, 14134, 9823, 5512, 1201},
		};

		int32_t clampU(const int64_t _v) { return _v < 0 ? 0 : (_v > U16Max ? U16Max : static_cast<int32_t>(_v)); }
		int32_t clampCode(const int32_t _v) { return _v < 0 ? 0 : (_v > 4095 ? 4095 : _v); }
		int abs6(const int _a) { return _a < 0 ? (_a < -63 ? 63 : -_a) : (_a > 63 ? 63 : _a); }

		// fixed route: source (full Mod precision) times the amount table, truncated to the 1/8 code
		// accumulator unit (u16 = 2 x that unit)
		int32_t route(const Mod _src, const int _amount)
		{
			if(!_amount)
				return 0;
			const int64_t p = static_cast<int64_t>(_src < 0 ? -_src : _src) * matrix::amountTable(matrix::Curve::Exp, _amount) / 16384;
			return 2 * static_cast<int32_t>((_src < 0) != (_amount < 0) ? -p : p);
		}

		int32_t addBuses(int32_t _u, const Buses& _b)
		{
			for(uint8_t i = 0; i < _b.n; ++i)
				_u = clampU(static_cast<int64_t>(_u) + _b.v[i]);
			return _u;
		}

		int32_t vca1Base(const int _a)
		{
			if(_a <= 0) return 0;
			if(_a >= 63) return 4000;
			return _a <= 32 ? 64 * _a - 32 : 64 * _a - 64;
		}

		// VCA1 by velocity, linear: 8 bit amount, 63 = 251
		int32_t vca1VelAmount(const int _a)
		{
			const int m = abs6(_a);
			const int v = m >= 63 ? 251 : 4 * m - (m + 15) / 16;
			return _a < 0 ? -v : v;
		}

		int32_t resonanceCode(const int _a)
		{
			if(_a <= 0) return 0;
			if(_a >= 63) return 4095;
			return 8 * (6 * _a - 1 + _a / 15);
		}
	}

	int32_t amountCurve(const int _amount)
	{
		// one table for buses and fixed routes: matrix/amount_sweep, also fits amt/ (amount 12 = 296)
		return 2 * matrix::amountTable(matrix::Curve::Exp, abs6(_amount));
	}

	uint8_t volume(const int _cc7, const uint8_t _invertByte)
	{
		if(_cc7 < 0)
			return 255;
		const int v = 2 * (_cc7 > 127 ? 127 : _cc7);
		return static_cast<uint8_t>(_invertByte ? 255 - v : v);
	}

	void compute(const Patch& _p, const Inputs& _in, VoiceControls& _c)
	{
		// VCF: base and keyboard, then each modulation added with the sum held in range
		int32_t key = 0;
		if(_p.vcfKeyb() & 1) key = _in.portaPitch;
		else if(_p.vcfKeyb() & 2) key = _in.keyPitch;
		key = key * 2 - 12 * 512;
		if(key < 0) key = 0;

		int32_t u = clampU(512 * static_cast<int64_t>(_p.vcfFreq()) - 12 * 512 + key + _in.fineTune);   // RPN 1: half its pitch offset (ctrl/rpn)
		u = addBuses(u, _in.busVcf);
		u = clampU(u + static_cast<int64_t>(route(_in.env1, _in.vcfByEnv1)));
		u = clampU(u + static_cast<int64_t>(route(_in.pressure, _p.vcfByPressure())));
		if(_p.vcfLevers() & 1)
			u = clampU(u + 2 * static_cast<int64_t>(_in.bend8 == 255 ? 128 : _in.bend8 - 128) * 2 * (_in.bendRange & 0x7f));   // range byte read as 7 bits: 128 = no bend (sysex/glob_b164)
		if(_p.vcfLevers() & 2)
			u = clampU(u + static_cast<int64_t>(_in.vibrato) * 512 / ModFull);   // [R-VIB] scale
		_c.vcf = u;
		_c.vcfFast = (_p.vcfKeyb() & 1) != 0;

		int32_t fm = clampU(1024 * static_cast<int64_t>(_p.fmAmount()));
		fm = addBuses(fm, _in.busFm);
		fm = clampU(fm + static_cast<int64_t>(route(_in.env3 & ~63, _in.fmByEnv3)));   // FM reads Env3 as an 8 bit level (matrix/ env3_amp sweep)
		fm = clampU(fm + static_cast<int64_t>(route(_in.pressure, _p.fmByPressure())));
		_c.fm = fm;

		const int32_t res = (_p.vcfRes() & 63) >= 63 ? U16Max : 16 * resonanceCode(_p.vcfRes());
		_c.res = addBuses(res, _in.busRes) >> 4;

		int32_t vca1 = addBuses(16 * vca1Base(_p.vca1()), _in.busVca1);
		vca1 = clampU(vca1 + static_cast<int64_t>(2 * _in.velocity * vca1VelAmount(_p.vca1ByVelocity())));
		_c.vca1 = vca1;

		int32_t vca2 = addBuses(0, _in.busVca2);
		vca2 = clampU(vca2 + static_cast<int64_t>(route(_in.env2, _p.vca2ByEnv2())));
		_c.vca2 = static_cast<int32_t>(static_cast<int64_t>(vca2) * _in.volume / 256);
	}

	uint32_t vcfValue(const uint32_t _voice, const VoiceControls& _c)
	{
		// segment drop times a 16-bit fraction as byte products, low x low dropped, cross terms truncated
		const auto& t = VcfCal[_voice % VoiceCount];
		const int32_t u = _c.vcf;
		const int32_t k = u >> 13 > 7 ? 7 : u >> 13;
		const int32_t d = t[k] - t[k + 1];
		const int32_t f = ((u - 8192 * k) << 3) & 0xffff;
		const int32_t dh = d >> 8, dl = d & 255, fh = f >> 8, fl = f & 255;
		const int32_t code = (t[k] - (dh * fh + ((dh * fl) >> 8) + ((dl * fh) >> 8))) >> 3;
		return static_cast<uint32_t>(clampCode(code)) | (_c.vcfFast ? 0 : CvSlow) | (RangeA8 << 13);
	}

	uint32_t vcfValue(const int32_t (&_table)[9], const VoiceControls& _c)
	{
		const auto& t = _table;
		const int32_t u = _c.vcf;
		const int32_t k = u >> 13 > 7 ? 7 : u >> 13;
		const int32_t d = t[k] - t[k + 1];
		const int32_t f = ((u - 8192 * k) << 3) & 0xffff;
		const int32_t dh = d >> 8, dl = d & 255, fh = f >> 8, fl = f & 255;
		const int32_t code = (t[k] - (dh * fh + ((dh * fl) >> 8) + ((dl * fh) >> 8))) >> 3;
		return static_cast<uint32_t>(clampCode(code)) | (_c.vcfFast ? 0 : CvSlow) | (RangeA8 << 13);
	}

	const int32_t (&vcfCal(const uint32_t _voice))[9]
	{
		return VcfCal[_voice % VoiceCount];
	}

	uint32_t fmValue(const VoiceControls& _c)
	{
		return static_cast<uint32_t>(_c.fm >> 4) | CvSlow | (RangeA7 << 13);
	}

	uint32_t vca1Value(const VoiceControls& _c)
	{
		return static_cast<uint32_t>(_c.vca1 >> 4) | CvSlow;
	}

	uint32_t vca2Value(const VoiceControls& _c)
	{
		return static_cast<uint32_t>(_c.vca2 >> 4) | CvSlow;
	}

	uint32_t resonanceValue(const VoiceControls& _c)
	{
		return static_cast<uint32_t>(_c.res) | CvSlow;
	}
}
