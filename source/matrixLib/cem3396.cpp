// FMA contraction is part of the bit exact contract between Cem3396 and Cem3396Bank. Clang:
// within one expression (its default), both paths spell each expression the same way. GCC (whose
// default is -ffp-contract=fast, across statements) and MSVC: off
#if defined(__clang__)
#pragma clang fp contract(on)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#include "cem3396.h"

#include <algorithm>
#include <cmath>

namespace matrixLib
{
	namespace
	{
		constexpr float ShaperPeak = 2.5f;      // 5/24 Vcc: sloped output at maximum
		constexpr float ShaperEnd = 5.0f;       // 5/12 Vcc: sloped output back at minimum
		constexpr float Pi = 3.14159265358979f;
		constexpr float MinPeriod = 4e-6f;      // seconds

		// filter input offset, in units of the saturation level: second harmonic near -35 dBc
		constexpr float InputBias = 0.3f;
		const float InputBiasOut = std::tanh(InputBias);
		const float InputBiasGain = 1.0f / (1.0f - InputBiasOut * InputBiasOut);

		// shaper breakpoints: the nominal chip as constants, a spread chip from its parameters
		struct NominalShape
		{
			static constexpr float peak = ShaperPeak;
			static constexpr float end = ShaperEnd;
			static constexpr float rise = 1.0f / ShaperPeak;
			static constexpr float fall = 1.0f / ShaperPeak;
			static constexpr float kinkPeak = -2.0f / ShaperPeak;
			static constexpr float kinkEnd = 1.0f / ShaperPeak;
		};

		struct SpreadShape
		{
			float peak, end, rise, fall, kinkPeak, kinkEnd;
			explicit SpreadShape(const float* _s) : peak(_s[0]), end(_s[1]), rise(_s[2]), fall(_s[3]), kinkPeak(_s[4]), kinkEnd(_s[5]) {}
		};

		// sloped waveform for a ramp voltage, 0..1
		template<typename S> float shaper(const S& _s, const float _v)
		{
			if(_v <= 0.0f) return 0.0f;
			if(_v <= _s.peak) return _v * _s.rise;
			if(_v <= _s.end) return (_s.end - _v) * _s.fall;
			return 0.0f;
		}

		template<typename S> float shaperSlope(const S& _s, const float _v)
		{
			if(_v < _s.peak) return _s.rise;
			if(_v < _s.end) return -_s.fall;
			return 0.0f;
		}

		// cubic B-spline BLEP and BLAMP residuals, _t = samples from the event, nonzero within +-2
		float blep(const float _t)
		{
			const float a = std::fabs(_t);
			if(a >= 2.0f) return 0.0f;
			float i;
			if(a < 1.0f)
				i = 0.5f + (4.0f * a - 2.0f * a * a * a + 0.75f * a * a * a * a) * (1.0f / 6.0f);
			else
			{
				const float u = 2.0f - a;
				i = 1.0f - u * u * u * u * (1.0f / 24.0f);
			}
			return _t < 0.0f ? 1.0f - i : i - 1.0f;
		}

		float blamp(const float _t)
		{
			const float a = std::fabs(_t);
			if(a >= 2.0f) return 0.0f;
			if(a < 1.0f)
				return 7.0f / 30.0f - 0.5f * a + (2.0f * a * a - 0.5f * a * a * a * a + 0.15f * a * a * a * a * a) * (1.0f / 6.0f);
			const float u = 2.0f - a;
			return u * u * u * u * u * (1.0f / 120.0f);
		}

		// corrections for the samples n-2 .. n+1 around the current sample n
		struct Corrections
		{
			float t[4] = {};

			void step(const float _h, const float _d)
			{
				for(int k = 0; k < 4; ++k)
					t[k] += _h * blep(_d + static_cast<float>(k - 2));
			}
			void kink(const float _slopeDelta, const float _d)
			{
				for(int k = 0; k < 4; ++k)
					t[k] += _slopeDelta * blamp(_d + static_cast<float>(k - 2));
			}
		};

		struct ConverterOut
		{
			float sloped = 0.0f;
			float pulse = 0.0f;
		};

		constexpr float RampMax = 10.0f;        // the current source runs out of headroom

		// advances one converter by one sample and returns band-limited sloped and pulse outputs.
		// The capacitor charges at _slope volts per sample and is emptied at each of the _count
		// discharge pulses. _peak is the ramp peak of the timer period, it centres the pulse
		template<typename S>
		ConverterOut runConverter(const S& _s, double& _volts, bool& _pulseHigh, const float _slope, const float _pw, const float _peak,
								  const float* _discharges, const uint32_t _count, Corrections& _sloped, Corrections& _pulse)
		{
			ConverterOut out;

			const bool pulseActive = _pw > 0.0f;
			auto isHigh = [&](const double _v) { return pulseActive && _v < _pw; };

			double v = _volts;

			// a moving width can pass the ramp between two samples
			if(isHigh(v) != _pulseHigh)
				_pulse.step(_pulseHigh ? -2.0f : 2.0f, 0.9999f);

			// charges for _len samples, the segment ends _after samples before the output sample
			auto charge = [&](const float _len, const float _after)
			{
				// thresholds are tested on the same float values the discharge sees
				const auto v0 = static_cast<float>(v);
				v = std::min<double>(RampMax, v + static_cast<double>(_slope) * _len);
				const auto v1 = static_cast<float>(v);
				if(_slope <= 1e-9f)
					return;
				auto at = [&](const float _v) { return std::min(0.9999f, (v1 - _v) / _slope + _after); };

				if(v0 < _s.peak && v1 >= _s.peak)
					_sloped.kink(_s.kinkPeak * _slope, at(_s.peak));
				if(v0 < _s.end && v1 >= _s.end)
					_sloped.kink(_s.kinkEnd * _slope, at(_s.end));
				if(pulseActive && v0 < _pw && v1 >= _pw)
					_pulse.step(-2.0f, at(_pw));
			};

			// ramp back to 0 V, _d samples before the output sample
			auto discharge = [&](const float _d)
			{
				const auto vf = static_cast<float>(v);
				_sloped.step(-shaper(_s, vf), _d);
				_sloped.kink((shaperSlope(_s, 0.0f) - shaperSlope(_s, vf)) * _slope, _d);
				if(pulseActive && vf >= _pw)
					_pulse.step(2.0f, _d);
				v = 0.0;
			};

			float pos = 0.0f;
			for(uint32_t i = 0; i < _count; ++i)
			{
				const float e = std::clamp(_discharges[i], pos, 0.9999f);
				charge(e - pos, 1.0f - e);
				discharge(1.0f - e);
				pos = e;
			}
			charge(1.0f - pos, 0.0f);

			_volts = v;
			_pulseHigh = isHigh(v);
			out.sloped = shaper(_s, static_cast<float>(v)) * 2.0f - 1.0f;
			// centred on its own duty cycle, so changing or muting the width does not step the DC level
			const float duty = pulseActive && _peak > 0.0f ? std::min(1.0f, _pw / _peak) : 0.0f;
			out.pulse = (_pulseHigh ? 2.0f : 0.0f) - 2.0f * duty;
			return out;
		}
	}

	void Cem3396::setSamplerate(const float _rate)
	{
		m_rate = _rate;
		m_invRate = 1.0f / _rate;
		// OB-Xd style first stage damping; kept light, a strong limit on the state turns into a
		// limit cycle at Nyquist when the cutoff is near the top
		m_damp = 0.2f * std::sqrt(44000.0f / _rate);
		m_cvCoeff = 1.0f - std::exp(-1.0f / (0.001f * _rate));
		m_acCoeff = 1.0f - 1.0f / (0.047f * _rate);
		m_dampInv = 1.0f / m_damp;
	}

	void Cem3396::reset()
	{
		m_a = {};
		m_b = {};
		for(int i = 0; i < 3; ++i)
			m_histSlopedA[i] = m_histSlopedB[i] = m_histPulseA[i] = m_histPulseB[i] = 0.0f;
		m_acIn = m_acOut = 0.0f;
		m_smoothInit = false;
		for(auto& s : m_s)
			s = 0.0f;
	}

	void Cem3396::setParams(const Params& _p)
	{
		m_p = _p;
		for(int i = 0; i < 2; ++i)
		{
			auto* s = m_d.shape[i];
			s[0] = _p.shaperPeak[i];
			s[1] = _p.shaperEnd[i];
			s[2] = 1.0f / _p.shaperPeak[i];
			s[3] = 1.0f / (_p.shaperEnd[i] - _p.shaperPeak[i]);
			s[4] = -(s[2] + s[3]);
			s[5] = s[3];
		}
		m_d.resLevelLog2 = _p.resLevelDb * (3.32192809f / 20.0f);
		m_d.resLevelK = -1.0f;
		m_d.linInvSpan = 1.0f / (_p.linFull - _p.linThreshold);
		m_d.logK = _p.logSlope * 0.05f;
		// the nominal bias takes the nominal path's constants, so a runtime tanh cannot round differently
		m_d.biasOut = _p.inputBias == InputBias ? InputBiasOut : std::tanh(_p.inputBias);
		m_d.biasGain = _p.inputBias == InputBias ? InputBiasGain : 1.0f / (1.0f - m_d.biasOut * m_d.biasOut);
	}

	template<bool Spread> float Cem3396::filterT(const float _in, const float _cutoff, const float _k)
	{
		const float g = std::tan(Pi * _cutoff * m_invRate);
		const float G = g / (1.0f + g);
		const float inv = 1.0f / (1.0f + g);

		const float S = G * G * G * m_s[0] * inv + G * G * m_s[1] * inv + G * m_s[2] * inv + m_s[3] * inv;
		const float G4 = G * G * G * G;

		// zero delay feedback solve with the input stage saturating, then OB-Xd style damping
		// of the first stage state inside the loop. The input pair is slightly off balance:
		// the measured self-oscillation carries a second harmonic
		float x;
		if constexpr (Spread)
			x = (std::tanh((_in - _k * S) / (1.0f + _k * G4) + m_p.inputBias) - m_d.biasOut) * m_d.biasGain;
		else
			x = (std::tanh((_in - _k * S) / (1.0f + _k * G4) + InputBias) - InputBiasOut) * InputBiasGain;
		for(size_t i = 0; i < 4; ++i)
		{
			auto& s = m_s[i];
			const float v = (x - s) * G;
			const float y = v + s;
			s = y + v;
			if(i == 0)
				s = std::atan(s * m_damp) * m_dampInv;
			x = y;
		}
		return x;
	}

	float Cem3396::process(const Controls& _in, const Discharges& _discharges, const float _noise)
	{
		return processT<false>(_in, _discharges, _noise);
	}

	float Cem3396::processSpread(const Controls& _in, const Discharges& _discharges, const float _noise)
	{
		return processT<true>(_in, _discharges, _noise);
	}

	template<bool Spread> float Cem3396::processT(const Controls& _in, const Discharges& _discharges, const float _noise)
	{
		// only the two VCA pins have a further 1 ms of RC (1 MOhm, 1 nF) behind the S&H
		if(!m_smoothInit)
		{
			m_logGain = _in.logGain;
			m_linGain = _in.linGain;
			m_smoothInit = true;
		}
		m_logGain += (_in.logGain - m_logGain) * m_cvCoeff;
		m_linGain += (_in.linGain - m_linGain) * m_cvCoeff;
		const Controls& _c = _in;

		Corrections slopedA, pulseA, slopedB, pulseB;

		// volts per sample and the ramp peak of a whole timer period
		auto ramp = [&](float _ws, const float _rtCt, const float _period, const int _conv, float& _slope, float& _peak)
		{
			if constexpr (Spread)
				_ws += m_p.wsOffset[_conv];
			_slope = _rtCt > 0.0f ? std::max(0.0f, _ws) / _rtCt * m_invRate : 0.0f;
			// a parked timer runs far above the audio range
			_peak = _period > MinPeriod ? _slope * _period * m_rate : 0.0f;
		};
		float slopeA, peakA, slopeB, peakB;
		ramp(_c.wsA, _c.rtCtA, _c.periodA, 0, slopeA, peakA);
		ramp(_c.wsB, _c.rtCtB, _c.periodB, 1, slopeB, peakB);

		ConverterOut a, b;
		if constexpr (Spread)
		{
			a = runConverter(SpreadShape(m_d.shape[0]), m_a.volts, m_a.pulseHigh, slopeA, _c.pwA + m_p.pwOffset[0], peakA, _discharges.a, _discharges.countA, slopedA, pulseA);
			b = runConverter(SpreadShape(m_d.shape[1]), m_b.volts, m_b.pulseHigh, slopeB, _c.pwB + m_p.pwOffset[1], peakB, _discharges.b, _discharges.countB, slopedB, pulseB);
		}
		else
		{
			a = runConverter(NominalShape(), m_a.volts, m_a.pulseHigh, slopeA, _c.pwA, peakA, _discharges.a, _discharges.countA, slopedA, pulseA);
			b = runConverter(NominalShape(), m_b.volts, m_b.pulseHigh, slopeB, _c.pwB, peakB, _discharges.b, _discharges.countB, slopedB, pulseB);
		}
		if(_c.noiseB)
		{
			b.sloped = _noise;
			b.pulse = 0.0f;
			slopedB = Corrections();
			pulseB = Corrections();
		}

		auto delay = [](float* _h, const float _naive, const Corrections& _c, const float _scale)
		{
			const float out = _h[0] + _c.t[0] * _scale;
			_h[0] = _h[1] + _c.t[1] * _scale;
			_h[1] = _h[2] + _naive + _c.t[2] * _scale;
			_h[2] = _c.t[3] * _scale;
			return out;
		};
		const float slopedOutA = delay(m_histSlopedA, a.sloped, slopedA, 2.0f);
		const float slopedOutB = delay(m_histSlopedB, b.sloped, slopedB, 2.0f);
		const float convA = delay(m_histPulseA, a.pulse, pulseA, 1.0f) + (_c.slopedA ? slopedOutA : 0.0f);
		const float convB = delay(m_histPulseB, b.pulse, pulseB, 1.0f) + (_c.slopedB ? slopedOutB : 0.0f);

		// balance: differential pair, 80 dB of the other converter at about +-2 V
		float gA;
		if constexpr (Spread)
			gA = 1.0f / (1.0f + std::exp(-m_p.balanceSlope * (_c.balance - m_p.balanceOffset)));
		else
			gA = 1.0f / (1.0f + std::exp(-4.6f * _c.balance));
		const float mix = convA * gA + convB * (1.0f - gA);

		// Cutoff: 1008 Hz at 0 V, 1.721 oct/V. FM up to 2x at 4.5 V, taken from converter A's
		// waveform shaper ahead of the wave select switch
		const float depth = std::clamp(_c.mod * (1.0f / 4.5f), 0.0f, 1.0f) * 2.0f;
		float fc;
		if constexpr (Spread)
			fc = m_p.f0 * std::exp2(-_c.freq * m_p.octPerVolt) * std::max(0.0f, 1.0f + depth * slopedOutA * 0.5f);
		else
			fc = 1008.0f * std::exp2(-_c.freq * 1.721f) * std::max(0.0f, 1.0f + depth * slopedOutA * 0.5f);
		fc = std::clamp(fc, 5.0f, m_rate * 0.45f);

		const float k = std::clamp(Spread ? _c.resonance * m_p.resScale : _c.resonance, 0.0f, 5.0f);
		// the analog noise floor seeds self-oscillation near the resonance threshold
		float makeup = 1.0f + 0.25f * std::min(k, 4.0f);
		if constexpr (Spread)
		{
			if(k != m_d.resLevelK)
			{
				m_d.resLevelK = k;
				m_d.resLevelGain = std::exp2(m_d.resLevelLog2 * std::min(k, 4.0f) * 0.25f);
			}
			// folded into the makeup so both paths end in the same product (FP contraction)
			makeup *= m_d.resLevelGain;
		}
		const float f = filterT<Spread>(mix * 0.5f + _noise * 1e-4f, fc, k) * 2.0f * makeup;

		// gain set pin: 10 kOhm in series with 4.7 uF between the filter and the VCAs
		const float y = f - m_acIn + m_acCoeff * m_acOut;
		m_acIn = f;
		m_acOut = y;

		// audio taper VCA, datasheet typical: 22 dB/V up to 3.3 V, then 50 %/V to full gain at 4.5 V
		float log;
		if(m_logGain >= 3.3f)
			log = std::min(1.0f, 0.4f + (m_logGain - 3.3f) * 0.5f);
		else
			log = 0.4f * std::pow(10.0f, (m_logGain - 3.3f) * (Spread ? m_d.logK : 22.0f * 0.05f)) * std::clamp(m_logGain * (1.0f / 0.3f), 0.0f, 1.0f);

		const float lin = Spread ? std::clamp((m_linGain - m_p.linThreshold) * m_d.linInvSpan, 0.0f, 1.0f)
								 : std::clamp(m_linGain * (1.0f / 4.5f), 0.0f, 1.0f);

		return y * log * lin;
	}
}

// ---- six voices across SIMD lanes ----

#include <cstring>

#if !defined(CEM3396_LANES_WRAPPER) && (defined(__clang__) || defined(__GNUC__))
#define CEM3396_LANES_NATIVE 1
#elif defined(_M_X64) || defined(__x86_64__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || defined(__SSE2__)
#define CEM3396_LANES_SSE2 1
#include <emmintrin.h>
#endif

namespace matrixLib
{
	namespace
	{
		// Eight float lanes. GCC/Clang vector extensions (NEON or SSE2 underneath): the compiler
		// contracts a * b + c inside one expression exactly as it does for the scalar model, so
		// both paths keep the same FMA decisions. The SSE2 wrapper is for compilers without
		// vector extensions (MSVC), which do not contract scalar code either
#if CEM3396_LANES_NATIVE
		typedef float V __attribute__((vector_size(32)));
		typedef int32_t M __attribute__((vector_size(32)));

		inline V splat(const float _x) { return V{_x, _x, _x, _x, _x, _x, _x, _x}; }
		inline V load(const float* _p) { V v; std::memcpy(&v, _p, sizeof(v)); return v; }
		inline void store(float* _p, const V& _v) { std::memcpy(_p, &_v, sizeof(_v)); }
		inline M lt(const V& _a, const V& _b) { return _a < _b; }
		inline M ge(const V& _a, const V& _b) { return _a >= _b; }
		inline V sel(const M& _m, const V& _a, const V& _b) { return (V)((_m & (M)_a) | (~_m & (M)_b)); }
#elif CEM3396_LANES_SSE2
		struct V { __m128 l, h; };
		struct M { __m128 l, h; };

		inline V splat(const float _x) { return {_mm_set1_ps(_x), _mm_set1_ps(_x)}; }
		inline V load(const float* _p) { return {_mm_loadu_ps(_p), _mm_loadu_ps(_p + 4)}; }
		inline void store(float* _p, const V& _v) { _mm_storeu_ps(_p, _v.l); _mm_storeu_ps(_p + 4, _v.h); }
		inline V operator+(const V& _a, const V& _b) { return {_mm_add_ps(_a.l, _b.l), _mm_add_ps(_a.h, _b.h)}; }
		inline V operator-(const V& _a, const V& _b) { return {_mm_sub_ps(_a.l, _b.l), _mm_sub_ps(_a.h, _b.h)}; }
		inline V operator*(const V& _a, const V& _b) { return {_mm_mul_ps(_a.l, _b.l), _mm_mul_ps(_a.h, _b.h)}; }
		inline V operator/(const V& _a, const V& _b) { return {_mm_div_ps(_a.l, _b.l), _mm_div_ps(_a.h, _b.h)}; }
		inline V operator-(const V& _a) { const __m128 s = _mm_set1_ps(-0.0f); return {_mm_xor_ps(_a.l, s), _mm_xor_ps(_a.h, s)}; }
		inline M lt(const V& _a, const V& _b) { return {_mm_cmplt_ps(_a.l, _b.l), _mm_cmplt_ps(_a.h, _b.h)}; }
		inline M ge(const V& _a, const V& _b) { return {_mm_cmpge_ps(_a.l, _b.l), _mm_cmpge_ps(_a.h, _b.h)}; }
		inline V sel(const M& _m, const V& _a, const V& _b) { return {_mm_or_ps(_mm_and_ps(_m.l, _a.l), _mm_andnot_ps(_m.l, _b.l)), _mm_or_ps(_mm_and_ps(_m.h, _a.h), _mm_andnot_ps(_m.h, _b.h))}; }
#else
		struct V { float f[8]; };
		struct M { bool b[8]; };

		inline V splat(const float _x) { V r; for(auto& f : r.f) f = _x; return r; }
		inline V load(const float* _p) { V r; std::memcpy(r.f, _p, sizeof(r.f)); return r; }
		inline void store(float* _p, const V& _v) { std::memcpy(_p, _v.f, sizeof(_v.f)); }
#define CEM3396_LANE_OP(op) inline V operator op(const V& _a, const V& _b) { V r; for(int i = 0; i < 8; ++i) r.f[i] = _a.f[i] op _b.f[i]; return r; }
		CEM3396_LANE_OP(+) CEM3396_LANE_OP(-) CEM3396_LANE_OP(*) CEM3396_LANE_OP(/)
#undef CEM3396_LANE_OP
		inline V operator-(const V& _a) { V r; for(int i = 0; i < 8; ++i) r.f[i] = -_a.f[i]; return r; }
		inline M lt(const V& _a, const V& _b) { M r; for(int i = 0; i < 8; ++i) r.b[i] = _a.f[i] < _b.f[i]; return r; }
		inline M ge(const V& _a, const V& _b) { M r; for(int i = 0; i < 8; ++i) r.b[i] = _a.f[i] >= _b.f[i]; return r; }
		inline V sel(const M& _m, const V& _a, const V& _b) { V r; for(int i = 0; i < 8; ++i) r.f[i] = _m.b[i] ? _a.f[i] : _b.f[i]; return r; }
#endif
#if !CEM3396_LANES_NATIVE
		inline V operator+(const V& _a, const float _b) { return _a + splat(_b); }
		inline V operator+(const float _a, const V& _b) { return splat(_a) + _b; }
		inline V operator-(const V& _a, const float _b) { return _a - splat(_b); }
		inline V operator-(const float _a, const V& _b) { return splat(_a) - _b; }
		inline V operator*(const V& _a, const float _b) { return _a * splat(_b); }
		inline V operator*(const float _a, const V& _b) { return splat(_a) * _b; }
		inline V operator/(const float _a, const V& _b) { return splat(_a) / _b; }
		inline V& operator+=(V& _a, const V& _b) { _a = _a + _b; return _a; }
		inline V& operator*=(V& _a, const V& _b) { _a = _a * _b; return _a; }
#endif

		// std::min, std::max and std::clamp with their exact comparisons
		inline V vmin(const V& _a, const V& _b) { return sel(lt(_b, _a), _b, _a); }
		inline V vmax(const V& _a, const V& _b) { return sel(lt(_a, _b), _b, _a); }
		inline V vclamp(const V& _v, const V& _lo, const V& _hi) { return sel(lt(_v, _lo), _lo, sel(lt(_hi, _v), _hi, _v)); }

		constexpr uint32_t BankVoices = Cem3396Bank::Voices;
		constexpr uint32_t BankLanes = Cem3396Bank::Lanes;

		inline uint32_t bitsOf(const float _f) { uint32_t u; std::memcpy(&u, &_f, sizeof(u)); return u; }

		// libm per voice lane; padding lanes return 0
		template<typename F> V perLane(const V& _x, F _f)
		{
			alignas(32) float x[BankLanes];
			alignas(32) float y[BankLanes] = {};
			store(x, _x);
			for(uint32_t i = 0; i < BankVoices; ++i)
				y[i] = _f(x[i]);
			return load(y);
		}

		// same, reusing the last result of a lane while its input bits stay the same
		template<typename Memo, typename F> V perLaneMemo(const V& _x, Memo& _m, F _f)
		{
			alignas(32) float x[BankLanes];
			store(x, _x);
			if(std::memcmp(x, _m.in, BankVoices * sizeof(float)) == 0)
				return load(_m.out);
			for(uint32_t i = 0; i < BankVoices; ++i)
			{
				const uint32_t b = bitsOf(x[i]);
				if(b != _m.in[i])
				{
					_m.in[i] = b;
					_m.out[i] = _f(x[i]);
				}
			}
			return load(_m.out);
		}

		template<typename Memo, typename F> void initMemo(Memo& _m, F _f)
		{
			for(uint32_t i = 0; i < BankLanes; ++i)
			{
				_m.in[i] = 0;
				_m.out[i] = i < BankVoices ? _f(0.0f) : 0.0f;
			}
		}

		// runConverter() for a sample without discharge pulses and without a threshold crossing,
		// the common case: no corrections. Returns false (state untouched) for any other sample
		template<typename S>
		bool runConverterQuiet(const S& _s, double& _volts, bool& _pulseHigh, const float _slope, const float _pw, const float _peak,
							   const uint32_t _count, ConverterOut& _out)
		{
			if(_count)
				return false;
			const bool pulseActive = _pw > 0.0f;
			const double v = _volts;
			if((pulseActive && v < _pw) != _pulseHigh)
				return false;

			// charge(1.0f - 0.0f, 0.0f)
			const float len = 1.0f - 0.0f;
			const auto v0 = static_cast<float>(v);
			const double vn = std::min<double>(RampMax, v + static_cast<double>(_slope) * len);
			const auto v1 = static_cast<float>(vn);
			if(!(_slope <= 1e-9f))
			{
				if(v0 < _s.peak && v1 >= _s.peak)
					return false;
				if(v0 < _s.end && v1 >= _s.end)
					return false;
				if(pulseActive && v0 < _pw && v1 >= _pw)
					return false;
			}

			_volts = vn;
			_pulseHigh = pulseActive && vn < _pw;
			_out.sloped = shaper(_s, static_cast<float>(vn)) * 2.0f - 1.0f;
			const float duty = pulseActive && _peak > 0.0f ? std::min(1.0f, _pw / _peak) : 0.0f;
			_out.pulse = (_pulseHigh ? 2.0f : 0.0f) - 2.0f * duty;
			return true;
		}

#if CEM3396_LANES_NATIVE
		typedef double D __attribute__((vector_size(64)));
		typedef int64_t L __attribute__((vector_size(64)));

		inline D selD(const L& _m, const D& _a, const D& _b) { return (D)((_m & (L)_a) | (~_m & (L)_b)); }

		struct LaneShape { V peak, end, rise, fall; };

		// runConverterQuiet() for all lanes at once. Returns the lanes it handled (their state and
		// outputs are written); the others are left for runConverter()
		M runConvertersQuiet(const LaneShape& _s, double* _volts, int32_t* _pulseHigh, const V& _slope, const V& _pw, const V& _peak,
							 const uint32_t* _counts, float* _sloped, float* _pulse)
		{
			const V zero = splat(0.0f);
			M counts, high;
			std::memcpy(&counts, _counts, sizeof(counts));
			std::memcpy(&high, _pulseHigh, sizeof(high));
			const M countZero = counts == M{};
			const M pulseActive = lt(zero, _pw);
			D v;
			std::memcpy(&v, _volts, sizeof(v));
			const D pwD = __builtin_convertvector(_pw, D);
			const M highIn = pulseActive & __builtin_convertvector((L)(v < pwD), M);
			M ok = countZero & ~(highIn ^ high);

			// charge(1.0f, 0.0f): the slope times 1.0 is exact, contracted or not
			const V v0 = __builtin_convertvector(v, V);
			const D sum = v + __builtin_convertvector(_slope, D);
			const D vn = selD((L)(sum < static_cast<double>(RampMax)), sum, D{} + static_cast<double>(RampMax));
			const V v1 = __builtin_convertvector(vn, V);
			const M slopeOn = ~(M)(_slope <= splat(1e-9f));
			const M cross = (lt(v0, _s.peak) & ge(v1, _s.peak)) | (lt(v0, _s.end) & ge(v1, _s.end)) | (pulseActive & lt(v0, _pw) & ge(v1, _pw));
			ok &= ~(slopeOn & cross);

			const M highOut = pulseActive & __builtin_convertvector((L)(vn < pwD), M);
			const V shaped = sel((M)(v1 <= zero), zero, sel((M)(v1 <= _s.peak), v1 * _s.rise, sel((M)(v1 <= _s.end), (_s.end - v1) * _s.fall, zero)));
			const V sloped = shaped * 2.0f - 1.0f;
			const V duty = sel(pulseActive & lt(zero, _peak), vmin(splat(1.0f), _pw / _peak), zero);
			const V pulse = sel(highOut, splat(2.0f), zero) - 2.0f * duty;

			const L okD = __builtin_convertvector(ok, L);
			const D volts = selD(okD, vn, v);
			std::memcpy(_volts, &volts, sizeof(volts));
			store(_sloped, sloped);
			store(_pulse, pulse);
			const M highNew = (ok & highOut) | (~ok & high);
			std::memcpy(_pulseHigh, &highNew, sizeof(highNew));
			return ok;
		}
#endif

		float bankExp(const float _x) { return std::exp(_x); }
		float bankExp2(const float _x) { return std::exp2(_x); }
		float bankTan(const float _x) { return std::tan(_x); }
		float bankTanh(const float _x) { return std::tanh(_x); }
		float bankAtan(const float _x) { return std::atan(_x); }
	}

	Cem3396Bank::Cem3396Bank()
	{
		for(uint32_t v = 0; v < Lanes; ++v)
			setParams(v, Cem3396::Params());
		setSamplerate(48000.0f);
		reset();
	}

	void Cem3396Bank::setSamplerate(const float _rate)
	{
		m_rate = _rate;
		m_invRate = 1.0f / _rate;
		m_damp = 0.2f * std::sqrt(44000.0f / _rate);
		m_cvCoeff = 1.0f - std::exp(-1.0f / (0.001f * _rate));
		m_acCoeff = 1.0f - 1.0f / (0.047f * _rate);
		m_dampInv = 1.0f / m_damp;
	}

	void Cem3396Bank::reset()
	{
		for(uint32_t c = 0; c < 2; ++c)
		{
			for(uint32_t i = 0; i < Lanes; ++i)
			{
				m_volts[c][i] = 0.0;
				m_pulseHigh[c][i] = 0;
			}
		}
		std::memset(m_hist, 0, sizeof(m_hist));
		std::memset(m_s, 0, sizeof(m_s));
		std::memset(m_acIn, 0, sizeof(m_acIn));
		std::memset(m_acOut, 0, sizeof(m_acOut));
		std::memset(m_logGain, 0, sizeof(m_logGain));
		std::memset(m_linGain, 0, sizeof(m_linGain));
		m_smoothInit = false;

		initMemo(m_memoBalance, bankExp);
		initMemo(m_memoCutoff, bankExp2);
		initMemo(m_memoTan, bankTan);
		initMemo(m_memoLog, [](const float _x) { return std::pow(10.0f, _x); });
	}

	void Cem3396Bank::setParams(const uint32_t _voice, const Cem3396::Params& _p)
	{
		if(_voice >= Lanes)
			return;
		const uint32_t l = _voice;
		for(int i = 0; i < 2; ++i)
		{
			float s[6];
			s[0] = _p.shaperPeak[i];
			s[1] = _p.shaperEnd[i];
			s[2] = 1.0f / _p.shaperPeak[i];
			s[3] = 1.0f / (_p.shaperEnd[i] - _p.shaperPeak[i]);
			s[4] = -(s[2] + s[3]);
			s[5] = s[3];
			for(int k = 0; k < 6; ++k)
				m_shape[i][k][l] = s[k];
			m_wsOffset[i][l] = _p.wsOffset[i];
			m_pwOffset[i][l] = _p.pwOffset[i];
		}
		m_f0[l] = _p.f0;
		m_octPerVolt[l] = _p.octPerVolt;
		m_resScale[l] = _p.resScale;
		m_balanceSlope[l] = _p.balanceSlope;
		m_balanceOffset[l] = _p.balanceOffset;
		m_inputBias[l] = _p.inputBias;
		m_resLevelLog2[l] = _p.resLevelDb * (3.32192809f / 20.0f);
		m_resLevelK[l] = -1.0f;
		m_resLevelGain[l] = 1.0f;
		m_linThreshold[l] = _p.linThreshold;
		m_linInvSpan[l] = 1.0f / (_p.linFull - _p.linThreshold);
		m_logK[l] = _p.logSlope * 0.05f;
		m_biasOut[l] = _p.inputBias == InputBias ? InputBiasOut : std::tanh(_p.inputBias);
		m_biasGain[l] = _p.inputBias == InputBias ? InputBiasGain : 1.0f / (1.0f - m_biasOut[l] * m_biasOut[l]);
	}

	void Cem3396Bank::process(const Controls& _in, const Cem3396::Discharges* _discharges, const float _noise, float* _out)
	{
		processT<false>(_in, _discharges, _noise, _out);
	}

	void Cem3396Bank::processSpread(const Controls& _in, const Cem3396::Discharges* _discharges, const float _noise, float* _out)
	{
		processT<true>(_in, _discharges, _noise, _out);
	}

	template<bool Spread> void Cem3396Bank::processT(const Controls& _c, const Cem3396::Discharges* _discharges, const float _noise, float* _out)
	{
		// expressions keep the scalar model's text (Cem3396::processT): same rounding, same contractions
		if(!m_smoothInit)
		{
			std::memcpy(m_logGain, _c.logGain, sizeof(m_logGain));
			std::memcpy(m_linGain, _c.linGain, sizeof(m_linGain));
			m_smoothInit = true;
		}
		V logGain = load(m_logGain);
		V linGain = load(m_linGain);
		logGain += (load(_c.logGain) - logGain) * m_cvCoeff;
		linGain += (load(_c.linGain) - linGain) * m_cvCoeff;
		store(m_logGain, logGain);
		store(m_linGain, linGain);

		const V zero = splat(0.0f);

		auto ramp = [&](V _ws, const V _rtCt, const V _period, const int _conv, V& _slope, V& _peak)
		{
			if constexpr (Spread)
				_ws += load(m_wsOffset[_conv]);
			_slope = sel(lt(zero, _rtCt), vmax(zero, _ws) / _rtCt * m_invRate, zero);
			_peak = sel(lt(splat(MinPeriod), _period), _slope * _period * m_rate, zero);
		};
		V slopeA, peakA, slopeB, peakB;
		ramp(load(_c.wsA), load(_c.rtCtA), load(_c.periodA), 0, slopeA, peakA);
		ramp(load(_c.wsB), load(_c.rtCtB), load(_c.periodB), 1, slopeB, peakB);

		// converters per lane: discharges and threshold crossings branch per voice
		alignas(32) float slope[2][Lanes], peak[2][Lanes];
		store(slope[0], slopeA); store(slope[1], slopeB);
		store(peak[0], peakA); store(peak[1], peakB);

		// corrections only exist for lanes that took runConverter(); corr[i] is zeroed on first use
		alignas(32) float naive[4][Lanes] = {};
		alignas(32) float corr[4][4][Lanes];
		bool hasCorr[2] = {false, false};
		for(uint32_t conv = 0; conv < 2; ++conv)
		{
			const float* pw = conv ? _c.pwB : _c.pwA;
			bool quiet[Lanes] = {};
#if CEM3396_LANES_NATIVE
			{
				uint32_t counts[Lanes] = {};
				for(uint32_t v = 0; v < Voices; ++v)
					counts[v] = conv ? _discharges[v].countB : _discharges[v].countA;
				LaneShape shape;
				V p = load(pw);
				if constexpr (Spread)
				{
					shape = {load(m_shape[conv][0]), load(m_shape[conv][1]), load(m_shape[conv][2]), load(m_shape[conv][3])};
					p = p + load(m_pwOffset[conv]);
				}
				else
					shape = {splat(NominalShape::peak), splat(NominalShape::end), splat(NominalShape::rise), splat(NominalShape::fall)};
				const M ok = runConvertersQuiet(shape, m_volts[conv], m_pulseHigh[conv], conv ? slopeB : slopeA, p, conv ? peakB : peakA, counts, naive[conv], naive[conv + 2]);
				for(uint32_t v = 0; v < Voices; ++v)
					quiet[v] = ok[v] != 0;
			}
#endif
			for(uint32_t v = 0; v < Voices; ++v)
			{
				if(quiet[v])
					continue;
				const auto& d = _discharges[v];
				const uint32_t count = conv ? d.countB : d.countA;
				const float* events = conv ? d.b : d.a;
				Corrections sloped, pulse;
				ConverterOut o;
				bool high = m_pulseHigh[conv][v] != 0;
				if constexpr (Spread)
				{
					float sh[6];
					for(int k = 0; k < 6; ++k)
						sh[k] = m_shape[conv][k][v];
					const SpreadShape shape(sh);
					const float p = pw[v] + m_pwOffset[conv][v];
					if(!runConverterQuiet(shape, m_volts[conv][v], high, slope[conv][v], p, peak[conv][v], count, o))
					{
						o = runConverter(shape, m_volts[conv][v], high, slope[conv][v], p, peak[conv][v], events, count, sloped, pulse);
						if(!hasCorr[conv])
						{
							std::memset(corr[conv], 0, sizeof(corr[conv]));
							std::memset(corr[conv + 2], 0, sizeof(corr[conv + 2]));
							hasCorr[conv] = true;
						}
						for(int k = 0; k < 4; ++k)
						{
							corr[conv][k][v] = sloped.t[k];
							corr[conv + 2][k][v] = pulse.t[k];
						}
					}
				}
				else
				{
					if(!runConverterQuiet(NominalShape(), m_volts[conv][v], high, slope[conv][v], pw[v], peak[conv][v], count, o))
					{
						o = runConverter(NominalShape(), m_volts[conv][v], high, slope[conv][v], pw[v], peak[conv][v], events, count, sloped, pulse);
						if(!hasCorr[conv])
						{
							std::memset(corr[conv], 0, sizeof(corr[conv]));
							std::memset(corr[conv + 2], 0, sizeof(corr[conv + 2]));
							hasCorr[conv] = true;
						}
						for(int k = 0; k < 4; ++k)
						{
							corr[conv][k][v] = sloped.t[k];
							corr[conv + 2][k][v] = pulse.t[k];
						}
					}
				}
				m_pulseHigh[conv][v] = high ? -1 : 0;
				naive[conv][v] = o.sloped;
				naive[conv + 2][v] = o.pulse;
			}
		}
		if(_c.noiseB)
		{
			for(uint32_t v = 0; v < Voices; ++v)
			{
				naive[1][v] = _noise;
				naive[3][v] = 0.0f;
			}
			hasCorr[1] = false;
		}

		auto delay = [&](const int _i, const float _scale)
		{
			auto& h = m_hist[_i];
			const auto& c = corr[_i];
			const bool has = hasCorr[_i & 1];
			const V c0 = has ? load(c[0]) : zero, c1 = has ? load(c[1]) : zero, c2 = has ? load(c[2]) : zero, c3 = has ? load(c[3]) : zero;
			const V h0 = load(h[0]), h1 = load(h[1]), h2 = load(h[2]);
			const V out = h0 + c0 * _scale;
			store(h[0], h1 + c1 * _scale);
			store(h[1], h2 + load(naive[_i]) + c2 * _scale);
			store(h[2], c3 * _scale);
			return out;
		};
		const V slopedOutA = delay(0, 2.0f);
		const V slopedOutB = delay(1, 2.0f);
		const V convA = delay(2, 1.0f) + (_c.slopedA ? slopedOutA : zero);
		const V convB = delay(3, 1.0f) + (_c.slopedB ? slopedOutB : zero);

		const V balance = load(_c.balance);
		V gA;
		if constexpr (Spread)
			gA = 1.0f / (1.0f + perLaneMemo(-load(m_balanceSlope) * (balance - load(m_balanceOffset)), m_memoBalance, bankExp));
		else
			gA = 1.0f / (1.0f + perLaneMemo(-4.6f * balance, m_memoBalance, bankExp));
		const V mix = convA * gA + convB * (1.0f - gA);

		const V depth = vclamp(load(_c.mod) * (1.0f / 4.5f), zero, splat(1.0f)) * 2.0f;
		const V freq = load(_c.freq);
		V fc;
		if constexpr (Spread)
			fc = load(m_f0) * perLaneMemo(-freq * load(m_octPerVolt), m_memoCutoff, bankExp2) * vmax(zero, 1.0f + depth * slopedOutA * 0.5f);
		else
			fc = 1008.0f * perLaneMemo(-freq * 1.721f, m_memoCutoff, bankExp2) * vmax(zero, 1.0f + depth * slopedOutA * 0.5f);
		fc = vclamp(fc, splat(5.0f), splat(m_rate * 0.45f));

		const V res = load(_c.resonance);
		const V k = vclamp(Spread ? res * load(m_resScale) : res, zero, splat(5.0f));

		// filterT
		const V in = mix * 0.5f + _noise * 1e-4f;
		const V g = perLaneMemo(Pi * fc * m_invRate, m_memoTan, bankTan);
		const V G = g / (1.0f + g);
		const V inv = 1.0f / (1.0f + g);

		V s0 = load(m_s[0]), s1 = load(m_s[1]), s2 = load(m_s[2]), s3 = load(m_s[3]);
		const V S = G * G * G * s0 * inv + G * G * s1 * inv + G * s2 * inv + s3 * inv;
		const V G4 = G * G * G * G;

		V x;
		if constexpr (Spread)
			x = (perLane((in - k * S) / (1.0f + k * G4) + load(m_inputBias), bankTanh) - load(m_biasOut)) * load(m_biasGain);
		else
			x = (perLane((in - k * S) / (1.0f + k * G4) + InputBias, bankTanh) - InputBiasOut) * InputBiasGain;
		V* st[4] = {&s0, &s1, &s2, &s3};
		for(size_t i = 0; i < 4; ++i)
		{
			auto& s = *st[i];
			const V v = (x - s) * G;
			const V y = v + s;
			s = y + v;
			if(i == 0)
				s = perLane(s * m_damp, bankAtan) * m_dampInv;
			x = y;
		}
		store(m_s[0], s0); store(m_s[1], s1); store(m_s[2], s2); store(m_s[3], s3);

		V makeup = 1.0f + 0.25f * vmin(k, splat(4.0f));
		if constexpr (Spread)
		{
			alignas(32) float kk[Lanes];
			store(kk, k);
			for(uint32_t v = 0; v < Voices; ++v)
			{
				if(kk[v] != m_resLevelK[v])
				{
					m_resLevelK[v] = kk[v];
					m_resLevelGain[v] = std::exp2(m_resLevelLog2[v] * std::min(kk[v], 4.0f) * 0.25f);
				}
			}
			makeup *= load(m_resLevelGain);
		}
		const V f = x * 2.0f * makeup;

		const V acIn = load(m_acIn), acOut = load(m_acOut);
		const V y = f - acIn + m_acCoeff * acOut;
		store(m_acIn, f);
		store(m_acOut, y);

		const M high = ge(logGain, splat(3.3f));
		const V logHigh = vmin(splat(1.0f), 0.4f + (logGain - 3.3f) * 0.5f);
		const V logArg = (logGain - 3.3f) * (Spread ? load(m_logK) : splat(22.0f * 0.05f));
		// pow only where the scalar model calls it; other lanes keep their memo
		alignas(32) float arg[Lanes], hi[Lanes];
		store(arg, logArg);
		store(hi, sel(high, splat(1.0f), zero));
		for(uint32_t v = 0; v < Voices; ++v)
		{
			if(hi[v] == 0.0f)
			{
				const uint32_t b = bitsOf(arg[v]);
				if(b != m_memoLog.in[v])
				{
					m_memoLog.in[v] = b;
					m_memoLog.out[v] = std::pow(10.0f, arg[v]);
				}
			}
		}
		const V logLow = 0.4f * load(m_memoLog.out) * vclamp(logGain * (1.0f / 0.3f), zero, splat(1.0f));
		const V log = sel(high, logHigh, logLow);

		const V lin = Spread ? vclamp((linGain - load(m_linThreshold)) * load(m_linInvSpan), zero, splat(1.0f))
							 : vclamp(linGain * (1.0f / 4.5f), zero, splat(1.0f));

		alignas(32) float out[Lanes];
		store(out, y * log * lin);
		for(uint32_t v = 0; v < Voices; ++v)
			_out[v] = out[v];
	}
}
