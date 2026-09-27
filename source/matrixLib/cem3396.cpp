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

		// sloped waveform for a ramp voltage, 0..1
		float shaper(const float _v)
		{
			if(_v <= 0.0f) return 0.0f;
			if(_v <= ShaperPeak) return _v * (1.0f / ShaperPeak);
			if(_v <= ShaperEnd) return (ShaperEnd - _v) * (1.0f / ShaperPeak);
			return 0.0f;
		}

		float shaperSlope(const float _v)
		{
			if(_v < ShaperPeak) return 1.0f / ShaperPeak;
			if(_v < ShaperEnd) return -1.0f / ShaperPeak;
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
			bool wrapped = false;
			float wrapD = 0.0f;       // samples since the discharge
		};

		// advances one converter by one sample and returns band-limited sloped and pulse outputs
		ConverterOut runConverter(double& _phase, const float _period, const float _rtCt, const float _ws, const float _pw,
								  const float _invRate, Corrections& _sloped, Corrections& _pulse)
		{
			ConverterOut out;
			if(_period <= 0.0f || _rtCt <= 0.0f)
				return out;

			const double dphase = static_cast<double>(_invRate) / _period;
			if(dphase >= 0.5)
			{
				// discharged faster than the capacitor can charge: the ramp stays near 0 V
				_phase = 0.0;
				return out;
			}
			const float vp = std::max(0.0f, _ws) * _period / _rtCt;    // ramp peak for this period
			const float dvPerSample = vp * static_cast<float>(dphase);
			const bool pulseActive = _pw > 0.0f && _pw < vp;

			const double start = _phase;
			double end = start + dphase;

			// shaper corners and the pulse edge between two phases of the same ramp,
			// _after = samples from the segment end to the output sample
			auto crossings = [&](const double _p0, const double _p1, const float _after)
			{
				const float v0 = vp * static_cast<float>(_p0);
				const float v1 = vp * static_cast<float>(_p1);
				auto at = [&](const float _v) { return std::min(0.9999f, (v1 - _v) / std::max(1e-9f, dvPerSample) + _after); };

				if(v0 < ShaperPeak && v1 >= ShaperPeak)
					_sloped.kink(-2.0f / ShaperPeak * dvPerSample, at(ShaperPeak));
				if(v0 < ShaperEnd && v1 >= ShaperEnd)
					_sloped.kink(1.0f / ShaperPeak * dvPerSample, at(ShaperEnd));
				if(pulseActive && v0 < _pw && v1 >= _pw)
					_pulse.step(-2.0f, at(_pw));
			};

			if(end >= 1.0)
			{
				end -= 1.0;
				const float d = static_cast<float>(end / dphase);
				crossings(start, 1.0, d);

				const float vEnd = vp;

				_sloped.step(-shaper(vEnd), d);
				_sloped.kink((shaperSlope(0.0f) - shaperSlope(vEnd)) * dvPerSample, d);
				if(pulseActive)
					_pulse.step(2.0f, d);

				crossings(0.0, end, 0.0f);
				out.wrapped = true;
				out.wrapD = d;
			}
			else
			{
				crossings(start, end, 0.0f);
			}

			_phase = end;
			const float v = vp * static_cast<float>(end);
			out.sloped = shaper(v) * 2.0f - 1.0f;
			// centred on its own duty cycle, so changing or muting the width does not step the DC level
			const float duty = pulseActive ? _pw / vp : 0.0f;
			out.pulse = pulseActive ? (v < _pw ? 2.0f : 0.0f) - 2.0f * duty : 0.0f;
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
		m_dampInv = 1.0f / m_damp;
	}

	void Cem3396::reset()
	{
		m_a = {};
		m_b = {};
		for(int i = 0; i < 3; ++i)
			m_histA[i] = m_histB[i] = 0.0f;
		m_smoothInit = false;
		for(auto& s : m_s)
			s = 0.0f;
	}

	float Cem3396::filter(const float _in, const float _cutoff, const float _k)
	{
		const float g = std::tan(Pi * _cutoff * m_invRate);
		const float G = g / (1.0f + g);
		const float inv = 1.0f / (1.0f + g);

		const float S = G * G * G * m_s[0] * inv + G * G * m_s[1] * inv + G * m_s[2] * inv + m_s[3] * inv;
		const float G4 = G * G * G * G;

		// zero delay feedback solve with the input stage saturating, then OB-Xd style damping
		// of the first stage state inside the loop
		float x = std::tanh((_in - _k * S) / (1.0f + _k * G4));
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

	float Cem3396::process(const Controls& _in, const float _noise)
	{
		// the S&H outputs reach the chip through about 1 ms of RC (1 MOhm, 1 nF)
		if(!m_smoothInit)
		{
			m_smooth = _in;
			m_smoothInit = true;
		}
		auto& c = m_smooth;
		const float cvK = m_cvCoeff;
		auto slew = [cvK](float& _v, const float _target) { _v += (_target - _v) * cvK; };
		slew(c.wsA, _in.wsA);
		slew(c.wsB, _in.wsB);
		slew(c.pwA, _in.pwA);
		slew(c.pwB, _in.pwB);
		slew(c.balance, _in.balance);
		slew(c.freq, _in.freq);
		slew(c.resonance, _in.resonance);
		slew(c.mod, _in.mod);
		slew(c.logGain, _in.logGain);
		slew(c.linGain, _in.linGain);
		c.periodA = _in.periodA;
		c.periodB = _in.periodB;
		c.rtCtA = _in.rtCtA;
		c.rtCtB = _in.rtCtB;
		c.slopedA = _in.slopedA;
		c.slopedB = _in.slopedB;
		c.noiseB = _in.noiseB;
		c.sync = _in.sync;
		const Controls& _c = c;

		Corrections slopedA, pulseA, slopedB, pulseB;

		// converter B first: with sync it resets A
		ConverterOut b;
		if(_c.noiseB)
		{
			b.sloped = _noise;
			b.pulse = 0.0f;
		}
		else
		{
			b = runConverter(m_b.phase, _c.periodB, _c.rtCtB, _c.wsB, _c.pwB, m_invRate, slopedB, pulseB);
		}

		if(b.wrapped && _c.sync)
		{
			// soft and medium sync only catch A late in its cycle
			static constexpr double window[4] = {1.0, 0.8, 0.5, 0.0};
			const double dA = _c.periodA > 0.0f ? static_cast<double>(m_invRate) / _c.periodA : 0.0;
			const double phaseAtEvent = m_a.phase + dA * (1.0 - b.wrapD);
			if(phaseAtEvent >= window[_c.sync] && phaseAtEvent < 1.0)
			{
				// the forced discharge is band-limited like the converter's own wrap
				if(_c.periodA > 0.0f && _c.rtCtA > 0.0f && phaseAtEvent > 0.0)
				{
					const float vp = std::max(0.0f, _c.wsA) * _c.periodA / _c.rtCtA;
					const float v = vp * static_cast<float>(phaseAtEvent);
					slopedA.step(-shaper(v), b.wrapD);
					slopedA.kink((shaperSlope(0.0f) - shaperSlope(v)) * vp * static_cast<float>(dA), b.wrapD);
					if(_c.pwA > 0.0f && _c.pwA < vp && v >= _c.pwA)
						pulseA.step(2.0f, b.wrapD);
				}
				m_a.phase = -dA * (1.0 - b.wrapD);
			}
		}

		const ConverterOut a = runConverter(m_a.phase, _c.periodA, _c.rtCtA, _c.wsA, _c.pwA, m_invRate, slopedA, pulseA);

		auto delay = [](float* _h, const float _naive, const Corrections& _pulse, const Corrections& _sloped, const bool _useSloped)
		{
			float c[4];
			for(int k = 0; k < 4; ++k)
				c[k] = _pulse.t[k] + (_useSloped ? _sloped.t[k] * 2.0f : 0.0f);
			const float out = _h[0] + c[0];
			_h[0] = _h[1] + c[1];
			_h[1] = _h[2] + _naive + c[2];
			_h[2] = c[3];
			return out;
		};
		const float convA = delay(m_histA, a.pulse + (_c.slopedA ? a.sloped : 0.0f), pulseA, slopedA, _c.slopedA);
		const float convB = delay(m_histB, b.pulse + (_c.slopedB ? b.sloped : 0.0f), pulseB, slopedB, _c.slopedB);

		// balance: differential pair, 80 dB of the other converter at about +-2 V
		const float gA = 1.0f / (1.0f + std::exp(-4.6f * _c.balance));
		const float mix = convA * gA + convB * (1.0f - gA);

		// cutoff: -0.5 V per octave, FM from converter A up to 2x. The 0 V frequency is set so the
		// firmware's calibration table stays in range up to VCF 127 (about 30 kHz at DAC code 0)
		const float depth = std::clamp(_c.mod * (1.0f / 4.5f), 0.0f, 1.1f) * 2.0f;
		float fc = 1100.0f * std::exp2(-_c.freq * 2.0f) * std::max(0.0f, 1.0f + depth * convA * 0.5f);
		fc = std::clamp(fc, 5.0f, m_rate * 0.45f);

		const float k = std::clamp(_c.resonance, 0.0f, 5.0f);
		// the analog noise floor seeds self-oscillation near the resonance threshold
		const float y = filter(mix * 0.5f + _noise * 1e-4f, fc, k) * 2.0f * (1.0f + 0.25f * std::min(k, 4.0f));

		// audio taper VCA: exponential to -15 dB at 3.3 V, then linear to 0 dB at 5 V
		float log;
		if(_c.logGain <= 0.3f)
			log = 0.0f;
		else if(_c.logGain < 3.3f)
			log = std::pow(10.0f, (-90.0f + (_c.logGain - 0.3f) * 25.0f) * 0.05f);
		else
			log = 0.178f + (std::min(_c.logGain, 5.0f) - 3.3f) * (0.822f / 1.7f);

		const float lin = std::clamp(_c.linGain * (1.0f / 4.5f), 0.0f, 1.1f);

		return y * log * lin;
	}
}
