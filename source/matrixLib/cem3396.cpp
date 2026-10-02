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
		};

		constexpr float RampMax = 10.0f;        // the current source runs out of headroom

		// advances one converter by one sample and returns band-limited sloped and pulse outputs.
		// The capacitor charges at _slope volts per sample and is emptied at each of the _count
		// discharge pulses. _peak is the ramp peak of the timer period, it centres the pulse
		ConverterOut runConverter(double& _volts, bool& _pulseHigh, const float _slope, const float _pw, const float _peak,
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

				if(v0 < ShaperPeak && v1 >= ShaperPeak)
					_sloped.kink(-2.0f / ShaperPeak * _slope, at(ShaperPeak));
				if(v0 < ShaperEnd && v1 >= ShaperEnd)
					_sloped.kink(1.0f / ShaperPeak * _slope, at(ShaperEnd));
				if(pulseActive && v0 < _pw && v1 >= _pw)
					_pulse.step(-2.0f, at(_pw));
			};

			// ramp back to 0 V, _d samples before the output sample
			auto discharge = [&](const float _d)
			{
				const auto vf = static_cast<float>(v);
				_sloped.step(-shaper(vf), _d);
				_sloped.kink((shaperSlope(0.0f) - shaperSlope(vf)) * _slope, _d);
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
			out.sloped = shaper(static_cast<float>(v)) * 2.0f - 1.0f;
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

	float Cem3396::filter(const float _in, const float _cutoff, const float _k)
	{
		const float g = std::tan(Pi * _cutoff * m_invRate);
		const float G = g / (1.0f + g);
		const float inv = 1.0f / (1.0f + g);

		const float S = G * G * G * m_s[0] * inv + G * G * m_s[1] * inv + G * m_s[2] * inv + m_s[3] * inv;
		const float G4 = G * G * G * G;

		// zero delay feedback solve with the input stage saturating, then OB-Xd style damping
		// of the first stage state inside the loop. The input pair is slightly off balance:
		// the measured self-oscillation carries a second harmonic
		float x = (std::tanh((_in - _k * S) / (1.0f + _k * G4) + InputBias) - InputBiasOut) * InputBiasGain;
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
		auto ramp = [&](const float _ws, const float _rtCt, const float _period, float& _slope, float& _peak)
		{
			_slope = _rtCt > 0.0f ? std::max(0.0f, _ws) / _rtCt * m_invRate : 0.0f;
			// a parked timer runs far above the audio range
			_peak = _period > MinPeriod ? _slope * _period * m_rate : 0.0f;
		};
		float slopeA, peakA, slopeB, peakB;
		ramp(_c.wsA, _c.rtCtA, _c.periodA, slopeA, peakA);
		ramp(_c.wsB, _c.rtCtB, _c.periodB, slopeB, peakB);

		const ConverterOut a = runConverter(m_a.volts, m_a.pulseHigh, slopeA, _c.pwA, peakA, _discharges.a, _discharges.countA, slopedA, pulseA);
		ConverterOut b = runConverter(m_b.volts, m_b.pulseHigh, slopeB, _c.pwB, peakB, _discharges.b, _discharges.countB, slopedB, pulseB);
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
		const float gA = 1.0f / (1.0f + std::exp(-4.6f * _c.balance));
		const float mix = convA * gA + convB * (1.0f - gA);

		// Cutoff: scale and 0 V frequency follow the firmware's default tuning table ($DD49),
		// so the calibrated codes stay above 0 up to VCF 127. FM up to 2x at 4.5 V, taken from
		// converter A's waveform shaper ahead of the wave select switch
		const float depth = std::clamp(_c.mod * (1.0f / 4.5f), 0.0f, 1.0f) * 2.0f;
		float fc = 2016.0f * std::exp2(-_c.freq * 1.721f) * std::max(0.0f, 1.0f + depth * slopedOutA * 0.5f);
		fc = std::clamp(fc, 5.0f, m_rate * 0.45f);

		const float k = std::clamp(_c.resonance, 0.0f, 5.0f);
		// the analog noise floor seeds self-oscillation near the resonance threshold
		const float f = filter(mix * 0.5f + _noise * 1e-4f, fc, k) * 2.0f * (1.0f + 0.25f * std::min(k, 4.0f));

		// gain set pin: 10 kOhm in series with 4.7 uF between the filter and the VCAs
		const float y = f - m_acIn + m_acCoeff * m_acOut;
		m_acIn = f;
		m_acOut = y;

		// audio taper VCA, datasheet typical: 22 dB/V up to 3.3 V, then 50 %/V to full gain at 4.5 V
		float log;
		if(m_logGain >= 3.3f)
			log = std::min(1.0f, 0.4f + (m_logGain - 3.3f) * 0.5f);
		else
			log = 0.4f * std::pow(10.0f, (m_logGain - 3.3f) * (22.0f * 0.05f)) * std::clamp(m_logGain * (1.0f / 0.3f), 0.0f, 1.0f);

		const float lin = std::clamp(m_linGain * (1.0f / 4.5f), 0.0f, 1.0f);

		return y * log * lin;
	}
}
