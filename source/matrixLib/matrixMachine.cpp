#include "matrixMachine.h"

#include <algorithm>
#include <cmath>

namespace matrixLib
{
	namespace
	{
		// Timing resistor times capacitor per converter range (3.3 nF, 510k, 36k in parallel
		// for the high range), taken as the firmware's waveshape calibration ($B490) leaves a
		// real voice: a 50 % pulse with the width CV at 0.4612 of full scale, so width 31 is square
		constexpr float RtCtLow = 7.585e-4f;
		constexpr float RtCtHigh = 5.467e-5f;

		constexpr float OutputGain = 0.18f;
		// the summing amplifier clips at 10.5 V, a voice delivers up to 4 V peak to peak
		constexpr float OutputRail = OutputGain * 10.5f / 2.0f;
	}

	Machine::Machine()
	{
		setSamplerate(48000.0f);
	}

	void Machine::setSamplerate(const float _rate)
	{
		m_rate = _rate;
		m_cyclesPerSample = static_cast<double>(Hardware::CpuClock) / _rate;
		m_dcCoeff = 1.0f - 2.0f * 3.14159265f * 5.0f / _rate;
		m_cmpCoeff = 1.0f - 1.0f / (33e-9f * 230e3f * _rate);
		for(auto& v : m_voices)
			v.setSamplerate(_rate * Oversampling);
	}

	void Machine::reset()
	{
		for(auto& v : m_voices)
			v.reset();
		m_decimator.reset();
		m_cycleAcc = 0.0;
		m_dcIn = m_dcOut = m_cmpIn = m_cmpOut = m_lastSum = 0.0f;
		m_hw.reset();
		m_sampleCycle = m_hw.getCycles();
		m_edgeTick = static_cast<double>(m_sampleCycle) * 2.0 - EdgeMargin;
	}

	void Machine::buildDischarges(const uint32_t _voice, const double _from, const double _to, Cem3396::Discharges& _d0, Cem3396::Discharges& _d1)
	{
		_d0.countA = _d0.countB = _d1.countA = _d1.countB = 0;

		auto& events = m_hw.scanDco(_voice);
		const double scale = _to > _from ? 2.0 / (_to - _from) : 0.0;

		auto split = [&](std::vector<uint64_t>& _ticks, float* _f0, uint32_t& _n0, float* _f1, uint32_t& _n1)
		{
			size_t used = 0;
			for(; used < _ticks.size(); ++used)
			{
				const auto tick = static_cast<double>(_ticks[used]);
				if(tick >= _to)
					break;
				const double pos = std::max(0.0, (tick - _from) * scale);
				if(pos < 1.0)
				{
					if(_n0 < Cem3396::Discharges::Max)
						_f0[_n0++] = static_cast<float>(pos);
				}
				else if(_n1 < Cem3396::Discharges::Max)
					_f1[_n1++] = static_cast<float>(pos - 1.0);
			}
			_ticks.erase(_ticks.begin(), _ticks.begin() + static_cast<std::ptrdiff_t>(used));
		};
		split(events.a, _d0.a, _d0.countA, _d1.a, _d1.countA);
		split(events.b, _d0.b, _d0.countB, _d1.b, _d1.countB);
	}

	float Machine::noise()
	{
		m_noiseState ^= m_noiseState << 13;
		m_noiseState ^= m_noiseState >> 17;
		m_noiseState ^= m_noiseState << 5;
		return static_cast<float>(static_cast<int32_t>(m_noiseState)) * (1.0f / 2147483648.0f);
	}

	void Machine::buildControls(const uint32_t _voice, Cem3396::Controls& _c)
	{
		const auto& cv = m_hw.getCvVolts();

		const uint32_t pA = m_hw.getDcoPeriod(_voice, 0);
		const uint32_t pB = m_hw.getDcoPeriod(_voice, 1);
		_c.periodA = pA ? static_cast<float>(pA) / Hardware::VoiceTimerClock : 0.0f;
		_c.periodB = pB ? static_cast<float>(pB) / Hardware::VoiceTimerClock : 0.0f;
		_c.rtCtA = m_hw.getRangeHigh(_voice, 0) ? RtCtHigh : RtCtLow;
		_c.rtCtB = m_hw.getRangeHigh(_voice, 1) ? RtCtHigh : RtCtLow;

		_c.wsA = cv[Hardware::voiceCv(_voice, Hardware::CvWsA)];
		_c.wsB = cv[Hardware::voiceCv(_voice, Hardware::CvWsB)];
		_c.pwA = cv[Hardware::voiceCv(_voice, Hardware::CvPwA)];
		_c.pwB = cv[Hardware::voiceCv(_voice, Hardware::CvPwB)];
		_c.freq = cv[Hardware::voiceCv(_voice, Hardware::CvFreq)];
		_c.logGain = cv[Hardware::voiceCv(_voice, Hardware::CvLog)];
		_c.linGain = cv[Hardware::voiceCv(_voice, Hardware::CvLin)];
		_c.mod = cv[Hardware::voiceCv(_voice, Hardware::CvMod)];
		_c.balance = cv[Hardware::balanceCv(_voice)];
		_c.resonance = cv[Hardware::resonanceCv(_voice)];

		// $1D00: bits 5-4 drive the wave select level
		const uint8_t ctl = m_hw.getLatch(0x1d00);
		switch((ctl >> 4) & 3)
		{
		case 3:  _c.slopedA = true;  _c.slopedB = false; break;
		case 2:  _c.slopedA = false; _c.slopedB = false; break;
		case 1:  _c.slopedA = false; _c.slopedB = true;  break;
		default: _c.slopedA = true;  _c.slopedB = true;  break;
		}
		_c.noiseB = m_hw.getLatch(0x1c06) != 0;
	}

	void Machine::render(float* _out, const size_t _count)
	{
		auto& acia = m_hw.getAcia();
		Cem3396::Controls c;
		Cem3396::Discharges d0, d1;

		for(size_t i = 0; i < _count; ++i)
		{
			// the sample grid runs on its own clock, the CPU overshoots it by part of an instruction
			const uint64_t start = m_sampleCycle;

			// the discharge pulses come from timer edges the CPU has already passed, so the
			// converters render the previous sample's span of timer ticks
			const double edgeFrom = m_edgeTick;
			const double edgeTo = (static_cast<double>(start) + m_cycleAcc) * 2.0 - EdgeMargin;
			m_edgeTick = edgeTo;
			m_cycleAcc += m_cyclesPerSample;
			const auto cycles = static_cast<uint64_t>(m_cycleAcc);
			m_cycleAcc -= static_cast<double>(cycles);
			const uint64_t end = start + cycles;
			m_sampleCycle = end;

			m_hw.advanceCv(1.0f / m_rate);

			// voices run at twice the rate, the CPU and CVs at the output rate
			const float n0 = noise();
			const float n1 = noise();
			float sum0 = 0.0f, sum1 = 0.0f;
			for(uint32_t v = 0; v < Hardware::VoiceCount; ++v)
			{
				buildControls(v, c);
				buildDischarges(v, edgeFrom, edgeTo, d0, d1);
				sum0 += m_voices[v].process(c, d0, n0);
				sum1 += m_voices[v].process(c, d1, n1);
			}
			const float sum = m_decimator.process(sum0, sum1);

			// output coupling capacitor
			const float y = sum - m_dcIn + m_dcCoeff * m_dcOut;
			m_dcIn = sum;
			m_dcOut = y;

			// The calibration comparator has its own coupling (33 nF into 230 kOhm). It sees this
			// sample ahead of the CPU, with the zero crossing placed between the two samples
			const float cmp = sum - m_cmpIn + m_cmpCoeff * m_cmpOut;
			m_cmpIn = sum;
			m_cmpOut = cmp;

			const bool calibrating = isCalibrating();
			if(calibrating)
			{
				// comparator with a little hysteresis so the noise floor alone does not switch it
				constexpr float hysteresis = 2e-3f;
				const bool before = m_comparator;
				bool after = before;
				float threshold = 0.0f;
				if(!before && cmp > hysteresis) { after = true; threshold = hysteresis; }
				else if(before && cmp < -hysteresis) { after = false; threshold = -hysteresis; }

				uint64_t at = end;
				if(before != after && m_lastSum != cmp)
				{
					const float frac = std::clamp((m_lastSum - threshold) / (m_lastSum - cmp), 0.0f, 1.0f);
					at = start + static_cast<uint64_t>(frac * static_cast<float>(cycles));
				}
				acia.setCts(before, at, after);
				m_comparator = after;
			}
			else
			{
				acia.setCts(false, 0, false);
			}
			m_lastSum = cmp;
			if(onComparatorSample)
				onComparatorSample(cmp);

			m_hw.runUntil(end);

			_out[i] = calibrating ? 0.0f : std::clamp(y * OutputGain, -OutputRail, OutputRail);
		}
	}
}
