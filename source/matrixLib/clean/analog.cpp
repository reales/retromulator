#include "analog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cvDco.h"
#include "cvFilterAmp.h"

namespace matrixClean::analog
{
	namespace
	{
		// [S4] datasheet ranges, sigma = (max - typ) / 3 unless the design says otherwise
		const Spec g_conv[ConvParamCount] =
		{
			{"vi_gain",         Dist::Normal, 0.0, 0.03,   -0.15,  0.15},    // C1
			{"ct",              Dist::Normal, 0.0, 0.017,  -0.05,  0.05},    // C2
			{"rt",              Dist::Normal, 0.0, 0.0033, -0.01,  0.01},    // C3
			{"r36",             Dist::Normal, 0.0, 0.0033, -0.01,  0.01},    // C4
			{"ws_offset",       Dist::Normal, 0.0, 0.0023, -0.007, 0.007},   // C5, volts
			{"shaper_scale",    Dist::Normal, 0.0, 0.013,  -0.04,  0.04},    // C6
			{"shaper_mismatch", Dist::Normal, 0.0, 0.003,  -0.01,  0.01},    // C7
			{"pw_offset",       Dist::Normal, 0.0, 0.004,  -0.012, 0.012},   // C8, volts
		};
		const Spec g_voice[VoiceParamCount] =
		{
			{"f0_oct",          Dist::Normal,  0.0,    0.16,   -0.485,  0.485},    // V1, octaves
			{"scale",           Dist::Normal,  0.0,    0.02,   -0.06,   0.06},     // V2, V/oct error
			{"scale_tc",        Dist::Normal,  3300e-6, 100e-6, 3000e-6, 3600e-6}, // V3, per C
			{"res_threshold",   Dist::Normal,  4.0,    0.17,   3.5,     4.5},      // V5, volts
			{"res_level_db",    Dist::Uniform, 0.0,    0.0,    -2.0,    2.0},      // V6
			{"lin_threshold",   Dist::Uniform, 0.0,    0.0,    0.0,     0.15},     // V7, volts
			{"lin_full",        Dist::Normal,  4.5,    0.17,   4.0,     5.0},      // V8, volts
			{"log_slope",       Dist::Normal,  22.0,   1.0,    19.0,    25.0},     // V9, dB/V
			{"balance_slope",   Dist::Normal,  0.0,    0.033,  -0.1,    0.1},      // V10
			{"balance_offset",  Dist::Normal,  0.0,    0.010,  -0.03,   0.03},     // V11, volts
			{"out_gain",        Dist::Normal,  0.0,    0.0033, -0.01,   0.01},     // V12
			{"thermal",         Dist::Normal,  1.0,    0.033,  0.9,     1.1},      // V13
		};
		const Spec g_global[GlobalParamCount] =
		{
			{"rail",            Dist::Normal, 0.0, 0.015, -0.04, 0.04},      // G1
		};

		// draw stream ids, part of model version 1
		constexpr uint64_t IdConv = 1, IdVoice = 32, IdGlobal = 64, IdResidual = 96;
		constexpr uint64_t K1 = 0x9e3779b97f4a7c15ull, K2 = 0xc2b2ae3d27d4eb4full, K3 = 0x165667b19e3779f9ull;
		constexpr int MaxAttempts = 64;

		uint64_t splitMix(uint64_t _x)
		{
			_x += 0x9e3779b97f4a7c15ull;
			_x = (_x ^ (_x >> 30)) * 0xbf58476d1ce4e5b9ull;
			_x = (_x ^ (_x >> 27)) * 0x94d049bb133111ebull;
			return _x ^ (_x >> 31);
		}

		double unit(const uint64_t _h) { return (static_cast<double>(_h >> 11) + 0.5) * (1.0 / 9007199254740992.0); }

		uint64_t key(const uint32_t _seed, const uint64_t _id, const uint64_t _index, const uint64_t _attempt)
		{
			return static_cast<uint64_t>(_seed) ^ (_id * K1) ^ (_index * K2) ^ (_attempt * K3);
		}

		double gauss(const uint64_t _key)
		{
			const uint64_t h1 = splitMix(_key);
			const uint64_t h2 = splitMix(h1);
			return std::sqrt(-2.0 * std::log(unit(h1))) * std::cos(6.283185307179586 * unit(h2));
		}

		double drawSpec(const Spec& _s, const uint32_t _seed, const uint64_t _id, const uint64_t _index)
		{
			if(_s.dist == Dist::Uniform)
				return _s.lo + (_s.hi - _s.lo) * unit(splitMix(key(_seed, _id, _index, 0)));
			for(int a = 0; a < MaxAttempts; ++a)
			{
				const double x = _s.nominal + _s.sigma * gauss(key(_seed, _id, _index, static_cast<uint64_t>(a)));
				if(x >= _s.lo && x <= _s.hi)
					return x;
			}
			return _s.nominal;
		}

		double eff(const Spec& _s, const float _draw, const float _amount)
		{
			const double v = _s.nominal + static_cast<double>(_amount) * (static_cast<double>(_draw) - _s.nominal);
			return v < _s.lo ? _s.lo : (v > _s.hi ? _s.hi : v);
		}

		// voice board DAC scaling (voiceBoard.cpp cvWrite)
		constexpr double DacFullScale = static_cast<double>(5.42f * 145.0f / 159.6f);
		constexpr double DacReference = DacFullScale / 0.996;
		constexpr double Lsb0 = 8.0 * DacFullScale / 32768.0 * (12.0 * 133.0 / 145.0) / 10.0;   // range 0, volts per code
		constexpr double LsbA8 = 8.0 * DacFullScale / 32768.0 * 12.0 / 10.0;
		constexpr double OffsetA8 = DacReference * 12.0 / 20.0;

		double rPar(const double _a, const double _b) { return _a * _b / (_a + _b); }

		// VCF calibration of one chip: "measures" the cutoff at two codes as a count of 16 periods
		// in 2 MHz ticks, fits f0 and scale, returns each point's corrected pin voltage in 1/8 code
		void vcfCal(const double _f0, const double _octPerVolt, const double _nomF0, const double _nomOctPerVolt, const int32_t (&_base)[9], int64_t (&_out)[9])
		{
			auto pin = [](const double _code) { return _code * LsbA8 - OffsetA8; };
			auto measure = [&](const double _v)
			{
				const double f = _f0 * std::exp2(-_octPerVolt * _v);
				const double ticks = std::max(1.0, std::round(16.0 * 2e6 / f));
				return 16.0 * 2e6 / ticks;
			};
			const double v1 = pin(3000.0), v2 = pin(1000.0);
			const double f1 = measure(v1), f2 = measure(v2);
			const double s = std::log2(f2 / f1) / (v1 - v2);
			const double lf0 = std::log2(f1) + s * v1;
			for(int k = 0; k < 9; ++k)
			{
				const double vk = pin(static_cast<double>(_base[k]) / 8.0);
				const double lfk = std::log2(_nomF0) - _nomOctPerVolt * vk;
				const double v = (lf0 - lfk) / s;
				_out[k] = std::llround((v + OffsetA8) / LsbA8 * 8.0);
			}
		}

		void put8(std::vector<uint8_t>& _s, const uint8_t _v) { _s.push_back(_v); }
		void put32(std::vector<uint8_t>& _s, const uint32_t _v) { for(int i = 0; i < 4; ++i) _s.push_back(static_cast<uint8_t>(_v >> (8 * i))); }
		void putF(std::vector<uint8_t>& _s, const float _v) { uint32_t u; std::memcpy(&u, &_v, 4); put32(_s, u); }

		struct Reader
		{
			const uint8_t* p;
			size_t n;
			size_t i = 0;
			bool ok = true;
			uint64_t get(const int _bytes)
			{
				if(i + static_cast<size_t>(_bytes) > n) { ok = false; return 0; }
				uint64_t v = 0;
				for(int b = 0; b < _bytes; ++b) v |= static_cast<uint64_t>(p[i++]) << (8 * b);
				return v;
			}
			float getF() { const auto u = static_cast<uint32_t>(get(4)); float f; std::memcpy(&f, &u, 4); return f; }
		};

		constexpr uint8_t Magic[4] = {'M', 'X', 'A', 'N'};
	}

	const Spec& convSpec(const ConvParam _p) { return g_conv[_p]; }
	const Spec& voiceSpec(const VoiceParam _p) { return g_voice[_p]; }
	const Spec& globalSpec(const GlobalParam _p) { return g_global[_p]; }

	Unit::Unit()
	{
		m_draws = draw(m_seed);
		update();
	}

	void Unit::setSeed(const uint32_t _seed)
	{
		m_seed = _seed;
		m_draws = draw(_seed);
		update();
	}

	void Unit::setAmount(const float _amount)
	{
		m_amount = _amount < 0.0f ? 0.0f : (_amount > 2.0f ? 2.0f : _amount);
		update();
	}

	void Unit::setResidual(const Residual _r)
	{
		m_residual = _r;
		update();
	}

	void Unit::update()
	{
		m_cal = calibrate(m_draws, m_amount, m_residual);
		for(uint32_t v = 0; v < VoiceCount; ++v)
			m_chips[v] = effective(m_draws, v, m_amount);
	}

	Draws Unit::draw(const uint32_t _seed)
	{
		Draws d;
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			for(uint32_t c = 0; c < 2; ++c)
				for(uint32_t p = 0; p < ConvParamCount; ++p)
					d.conv[v][c][p] = static_cast<float>(drawSpec(g_conv[p], _seed, IdConv + p, v * 2 + c));
			for(uint32_t p = 0; p < VoiceParamCount; ++p)
				d.voice[v][p] = static_cast<float>(drawSpec(g_voice[p], _seed, IdVoice + p, v));
		}
		for(uint32_t p = 0; p < GlobalParamCount; ++p)
			d.global[p] = static_cast<float>(drawSpec(g_global[p], _seed, IdGlobal + p, 0));

		// VCF base residual: sd per point across the measured voices, redrawn beyond 3 sd
		for(int k = 0; k < 9; ++k)
		{
			double mean = 0.0, sq = 0.0;
			for(uint32_t v = 0; v < VoiceCount; ++v) mean += static_cast<double>(cvFilterAmp::vcfCal(v)[k]);
			mean /= VoiceCount;
			for(uint32_t v = 0; v < VoiceCount; ++v) { const double e = static_cast<double>(cvFilterAmp::vcfCal(v)[k]) - mean; sq += e * e; }
			const Spec s{"vcf_residual", Dist::Normal, 0.0, std::sqrt(sq / (VoiceCount - 1)), 0.0, 0.0};
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				const Spec t{s.name, s.dist, 0.0, s.sigma, -3.0 * s.sigma, 3.0 * s.sigma};
				d.vcfResidual[v][k] = static_cast<int32_t>(std::llround(drawSpec(t, _seed, IdResidual, v * 9 + static_cast<uint32_t>(k))));
			}
		}
		return d;
	}

	Chip Unit::effective(const Draws& _d, const uint32_t _v, const float _a)
	{
		Chip c;
		const auto& vd = _d.voice[_v];
		const double rail = eff(g_global[GRail], _d.global[GRail], _a);
		for(int i = 0; i < 2; ++i)
		{
			const auto& cd = _d.conv[_v][i];
			const double gain = eff(g_conv[CViGain], cd[CViGain], _a);
			const double ct = eff(g_conv[CCt], cd[CCt], _a);
			const double rt = eff(g_conv[CRt], cd[CRt], _a);
			const double r36 = eff(g_conv[CR36], cd[CR36], _a);
			const double scale = eff(g_conv[CShaperScale], cd[CShaperScale], _a);
			const double low = (1.0 + rt) * (1.0 + ct) / (1.0 + gain);
			const double high = rPar(510e3 * (1.0 + rt), 36e3 * (1.0 + r36)) / rPar(510e3, 36e3) * (1.0 + ct) / (1.0 + gain);
			c.rtCt[i][0] = RtCtLow * static_cast<float>(low);
			c.rtCt[i][1] = RtCtHigh * static_cast<float>(high);
			c.shaperPeak[i] = 2.5f * static_cast<float>((1.0 + scale) * (1.0 + rail));
			c.shaperEnd[i] = 5.0f * static_cast<float>((1.0 + scale) * (1.0 + eff(g_conv[CShaperMismatch], cd[CShaperMismatch], _a)) * (1.0 + rail));
			c.wsOffset[i] = static_cast<float>(eff(g_conv[CWsOffset], cd[CWsOffset], _a));
			c.pwOffset[i] = static_cast<float>(eff(g_conv[CPwOffset], cd[CPwOffset], _a));
		}
		c.f0Scale = static_cast<float>(std::exp2(eff(g_voice[VF0Oct], vd[VF0Oct], _a)));
		c.voltsPerOctScale = static_cast<float>(1.0 + eff(g_voice[VScale], vd[VScale], _a));
		c.resScale = static_cast<float>(4.0 / eff(g_voice[VResThreshold], vd[VResThreshold], _a));
		c.resLevelDb = static_cast<float>(eff(g_voice[VResLevel], vd[VResLevel], _a));
		c.linThreshold = static_cast<float>(eff(g_voice[VLinThreshold], vd[VLinThreshold], _a));
		c.linFull = static_cast<float>(eff(g_voice[VLinFull], vd[VLinFull], _a));
		c.logSlope = static_cast<float>(eff(g_voice[VLogSlope], vd[VLogSlope], _a));
		c.balanceSlope = 4.6f * static_cast<float>(1.0 + eff(g_voice[VBalanceSlope], vd[VBalanceSlope], _a));
		c.balanceOffset = static_cast<float>(eff(g_voice[VBalanceOffset], vd[VBalanceOffset], _a));
		c.gain = static_cast<float>(1.0 + eff(g_voice[VOutGain], vd[VOutGain], _a));
		return c;
	}

	uint32_t Unit::trimCode(const float _rtCt, const float _wsOffset, const float _pwOffset, const uint32_t _count)
	{
		// smallest waveshape code whose ramp peak over one period reaches the PW comparator
		// threshold ([S4] p.9 trim), closed form of an upward search in one code steps
		const double threshold = TrimPwCode * Lsb0 + static_cast<double>(_pwOffset);
		const double period = static_cast<double>(_count) / 2e6;
		const double c = std::ceil((threshold * static_cast<double>(_rtCt) / period - static_cast<double>(_wsOffset)) / Lsb0);
		return c < 1.0 ? 1u : static_cast<uint32_t>(c);
	}

	Calibration Unit::calibrate(const Draws& _d, const float _amount, const Residual _residual, const double _f0, const double _octPerVolt)
	{
		Calibration cal;
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			const Chip chip = effective(_d, v, _amount);
			for(int i = 0; i < 2; ++i)
			{
				for(int r = 0; r < 2; ++r)
				{
					const uint32_t count = r ? TrimCountHigh : TrimCountLow;
					cal.wsNum[v][i][r] = static_cast<int32_t>(trimCode(chip.rtCt[i][r], chip.wsOffset[i], chip.pwOffset[i], count));
					cal.wsDen[v][i][r] = static_cast<int32_t>(trimCode(r ? RtCtHigh : RtCtLow, 0.0f, 0.0f, count));
				}
				cal.pwOffset[v][i] = static_cast<int32_t>(std::lround(-static_cast<double>(chip.pwOffset[i]) / Lsb0));
			}

			const auto& ref = cvFilterAmp::vcfCal(v);
			int32_t base[9];
			for(int k = 0; k < 9; ++k)
			{
				base[k] = ref[k];
				if(_residual == Residual::Drawn)
				{
					int64_t sum = 0;
					for(uint32_t u = 0; u < VoiceCount; ++u) sum += cvFilterAmp::vcfCal(u)[k];
					// residual against the voice mean: the measured one fades out up to amount 1, the
					// drawn one scales with the amount; amount 0 is the measured table exactly
					const int64_t mean = (sum + VoiceCount / 2) / VoiceCount;
					const double a = static_cast<double>(_amount);
					const double r = (1.0 - std::min(a, 1.0)) * static_cast<double>(ref[k] - mean) + a * static_cast<double>(_d.vcfResidual[v][k]);
					base[k] = static_cast<int32_t>(mean + std::llround(r));
				}
			}
			int64_t chipV[9], nomV[9];
			vcfCal(_f0 * static_cast<double>(chip.f0Scale), _octPerVolt / static_cast<double>(chip.voltsPerOctScale), _f0, _octPerVolt, base, chipV);
			vcfCal(_f0, _octPerVolt, _f0, _octPerVolt, base, nomV);
			for(int k = 0; k < 9; ++k)
				cal.vcf[v][k] = static_cast<int32_t>(base[k] + chipV[k] - nomV[k]);
		}
		return cal;
	}

	uint32_t Unit::cvValue(const uint32_t _voice, const CvIndex _index, const VoiceControls& _c) const
	{
		const uint32_t v = _voice % VoiceCount;
		switch(_index)
		{
		case CvWsA:
		case CvWsB:
		{
			const int d = _index == CvWsA ? 0 : 1;
			const uint32_t code = dco::waveshape(_c.pitch[d], _c.shape[d]);
			const int r = dco::rangeHigh(_c.pitch[d]) ? 1 : 0;
			const int64_t num = m_cal.wsNum[v][d][r], den = m_cal.wsDen[v][d][r];
			const int64_t t = (static_cast<int64_t>(code & 0xfff) * num + den / 2) / den;
			return (code & ~0xfffu) | static_cast<uint32_t>(t > 4095 ? 4095 : t);
		}
		case CvPwA:
		case CvPwB:
		{
			const int d = _index == CvPwA ? 0 : 1;
			const uint32_t code = dco::pulseWidth(_c.pw[d], _c.shape[d], _c.pulse[d]);
			const int32_t dac = static_cast<int32_t>(code & 0xfff);
			// the pulse off code and code 0 keep their DAC range
			if(!_c.pulse[d] || dac == 0)
				return code;
			const int32_t t = dac + m_cal.pwOffset[v][d];
			return (code & ~0xfffu) | static_cast<uint32_t>(t < 1 ? 1 : (t > 4095 ? 4095 : t));
		}
		case CvFreq:
			return cvFilterAmp::vcfValue(m_cal.vcf[v], _c);
		default:
			return cvMap::cvValue(_voice, _index, _c);
		}
	}

	std::vector<uint8_t> Unit::getState() const
	{
		std::vector<uint8_t> s;
		put8(s, FormatVersion);
		put8(s, ModelVersion);
		put8(s, m_enabled ? 1 : 0);
		put8(s, static_cast<uint8_t>(m_residual));
		put8(s, static_cast<uint8_t>(m_thermal));
		put32(s, m_seed);
		putF(s, m_amount);
		put8(s, ConvParamCount);
		put8(s, VoiceParamCount);
		put8(s, GlobalParamCount);
		put8(s, 0);
		for(const auto& v : m_draws.conv) for(const auto& c : v) for(const float x : c) putF(s, x);
		for(const auto& v : m_draws.voice) for(const float x : v) putF(s, x);
		for(const float x : m_draws.global) putF(s, x);
		for(const auto& v : m_draws.vcfResidual) for(const int32_t x : v) put32(s, static_cast<uint32_t>(x));
		for(const auto& v : m_cal.wsNum) for(const auto& c : v) for(const int32_t x : c) put32(s, static_cast<uint32_t>(x));
		for(const auto& v : m_cal.wsDen) for(const auto& c : v) for(const int32_t x : c) put32(s, static_cast<uint32_t>(x));
		for(const auto& v : m_cal.pwOffset) for(const int32_t x : v) put32(s, static_cast<uint32_t>(x));
		for(const auto& v : m_cal.vcf) for(const int32_t x : v) put32(s, static_cast<uint32_t>(x));
		const auto size = static_cast<uint32_t>(s.size());
		put32(s, size);
		s.insert(s.end(), std::begin(Magic), std::end(Magic));
		return s;
	}

	bool Unit::findState(const std::vector<uint8_t>& _blob, const size_t _coreSize, size_t& _offset, size_t& _size)
	{
		if(_blob.size() < 8 || std::memcmp(&_blob[_blob.size() - 4], Magic, 4) != 0)
			return false;
		const size_t n = _blob.size();
		const size_t size = _blob[n - 8] | (_blob[n - 7] << 8) | (_blob[n - 6] << 16) | (static_cast<size_t>(_blob[n - 5]) << 24);
		if(size + 8 > n || n - 8 - size != _coreSize)
			return false;
		_offset = n - 8 - size;
		_size = size;
		return true;
	}

	namespace
	{
		// draws inside their spec limits (as drawn, float rounding included), calibration values in
		// the ranges calibrate() produces with margin: a corrupt blob must not reach integer overflow
		bool inSpec(const Spec& _s, const float _x) { return _x >= static_cast<float>(_s.lo) && _x <= static_cast<float>(_s.hi); }

		bool plausible(const Draws& _d, const Calibration& _c)
		{
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				for(uint32_t i = 0; i < 2; ++i)
					for(uint32_t p = 0; p < ConvParamCount; ++p)
						if(!inSpec(g_conv[p], _d.conv[v][i][p])) return false;
				for(uint32_t p = 0; p < VoiceParamCount; ++p)
					if(!inSpec(g_voice[p], _d.voice[v][p])) return false;
				for(const int32_t x : _d.vcfResidual[v])
					if(x < -32768 || x > 32768) return false;
				for(uint32_t i = 0; i < 2; ++i)
				{
					for(uint32_t r = 0; r < 2; ++r)
						if(_c.wsNum[v][i][r] < 1 || _c.wsNum[v][i][r] > (1 << 20) || _c.wsDen[v][i][r] > (1 << 20)) return false;
					if(_c.pwOffset[v][i] < -4095 || _c.pwOffset[v][i] > 4095) return false;
				}
				for(int k = 0; k < 9; ++k)
				{
					const int32_t x = _c.vcf[v][k];
					if(x < -(1 << 17) || x > (1 << 17) || (k > 0 && x > _c.vcf[v][k - 1])) return false;
				}
			}
			for(uint32_t p = 0; p < GlobalParamCount; ++p)
				if(!inSpec(g_global[p], _d.global[p])) return false;
			return true;
		}
	}

	bool Unit::setState(const uint8_t* _data, const size_t _size)
	{
		Reader r{_data, _size};
		const auto format = r.get(1);
		if(format < 1 || r.get(1) != ModelVersion)
			return false;
		const bool enabled = r.get(1) != 0;
		const auto residual = static_cast<Residual>(r.get(1));
		// a thermal mode this build does not know (a later build's Cold start) loads as Settled
		// with the stored calibration snapshot
		r.get(1);
		const auto thermal = Thermal::Settled;
		const auto seed = static_cast<uint32_t>(r.get(4));
		const float amount = r.getF();
		if(r.get(1) != ConvParamCount || r.get(1) != VoiceParamCount || r.get(1) != GlobalParamCount)
			return false;
		r.get(1);
		Draws d;
		for(auto& v : d.conv) for(auto& c : v) for(float& x : c) x = r.getF();
		for(auto& v : d.voice) for(float& x : v) x = r.getF();
		for(float& x : d.global) x = r.getF();
		for(auto& v : d.vcfResidual) for(int32_t& x : v) x = static_cast<int32_t>(r.get(4));
		Calibration cal;
		for(auto& v : cal.wsNum) for(auto& c : v) for(int32_t& x : c) x = static_cast<int32_t>(r.get(4));
		for(auto& v : cal.wsDen) for(auto& c : v) for(int32_t& x : c) { x = static_cast<int32_t>(r.get(4)); if(x <= 0) r.ok = false; }
		for(auto& v : cal.pwOffset) for(int32_t& x : v) x = static_cast<int32_t>(r.get(4));
		for(auto& v : cal.vcf) for(int32_t& x : v) x = static_cast<int32_t>(r.get(4));
		if(format == FormatVersion && r.i != _size)
			r.ok = false;
		if(!r.ok || (residual != Residual::Drawn && residual != Residual::Reference) || !(amount >= 0.0f && amount <= 2.0f))
			return false;
		if(!plausible(d, cal))
			return false;

		m_enabled = enabled;
		m_residual = residual;
		m_thermal = thermal;
		m_seed = seed;
		m_amount = amount;
		m_draws = d;
		m_cal = cal;
		for(uint32_t v = 0; v < VoiceCount; ++v)
			m_chips[v] = effective(m_draws, v, m_amount);
		return true;
	}
}
