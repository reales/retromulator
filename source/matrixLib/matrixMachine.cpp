#include "matrixMachine.h"

#include <algorithm>
#include <cmath>

namespace matrixLib
{
	namespace
	{
		// timing resistor times capacitor per converter range: with the firmware's nominal
		// waveshape calibration a 2.5 V ramp for shape 0 (saw), 5 V for shape 63 (triangle)
		constexpr float RtCtLow = 7.34e-4f;
		constexpr float RtCtHigh = RtCtLow / 13.8f;

		constexpr float OutputGain = 0.18f;
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
		for(auto& v : m_voices)
			v.setSamplerate(_rate * Oversampling);
	}

	void Machine::reset()
	{
		for(auto& v : m_voices)
			v.reset();
		m_decimator.reset();
		m_cycleAcc = 0.0;
		m_dcIn = m_dcOut = m_lastSum = 0.0f;
		m_hw.reset();
		m_sampleCycle = m_hw.getCycles();
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

		// $1D00: bits 5-4 drive the wave select level, bits 3-2 the sync gates
		const uint8_t ctl = m_hw.getLatch(0x1d00);
		switch((ctl >> 4) & 3)
		{
		case 3:  _c.slopedA = true;  _c.slopedB = false; break;
		case 2:  _c.slopedA = false; _c.slopedB = false; break;
		case 1:  _c.slopedA = false; _c.slopedB = true;  break;
		default: _c.slopedA = true;  _c.slopedB = true;  break;
		}
		_c.sync = static_cast<uint8_t>(3 - ((ctl >> 2) & 3));
		_c.noiseB = m_hw.getLatch(0x1c06) != 0;
	}

	void Machine::render(float* _out, const size_t _count)
	{
		auto& acia = m_hw.getAcia();
		Cem3396::Controls c;

		for(size_t i = 0; i < _count; ++i)
		{
			// the sample grid runs on its own clock, the CPU overshoots it by part of an instruction
			const uint64_t start = m_sampleCycle;
			m_cycleAcc += m_cyclesPerSample;
			const auto cycles = static_cast<uint64_t>(m_cycleAcc);
			m_cycleAcc -= static_cast<double>(cycles);
			const uint64_t end = start + cycles;
			m_sampleCycle = end;

			// voices run at twice the rate, the CPU and CVs at the output rate
			const float n0 = noise();
			const float n1 = noise();
			float sum0 = 0.0f, sum1 = 0.0f;
			for(uint32_t v = 0; v < Hardware::VoiceCount; ++v)
			{
				buildControls(v, c);
				sum0 += m_voices[v].process(c, n0);
				sum1 += m_voices[v].process(c, n1);
			}
			const float sum = m_decimator.process(sum0, sum1);

			// output coupling capacitor
			const float y = sum - m_dcIn + m_dcCoeff * m_dcOut;
			m_dcIn = sum;
			m_dcOut = y;

			// the calibration comparator sees this sample ahead of the CPU, with the
			// zero crossing placed between the two samples
			const bool calibrating = isCalibrating();
			if(calibrating)
			{
				// comparator with a little hysteresis so the noise floor alone does not switch it
				constexpr float hysteresis = 2e-3f;
				const bool before = m_comparator;
				bool after = before;
				float threshold = 0.0f;
				if(!before && y > hysteresis) { after = true; threshold = hysteresis; }
				else if(before && y < -hysteresis) { after = false; threshold = -hysteresis; }

				uint64_t at = end;
				if(before != after && m_lastSum != y)
				{
					const float frac = std::clamp((m_lastSum - threshold) / (m_lastSum - y), 0.0f, 1.0f);
					at = start + static_cast<uint64_t>(frac * static_cast<float>(cycles));
				}
				acia.setCts(before, at, after);
				m_comparator = after;
			}
			else
			{
				acia.setCts(false, 0, false);
			}
			m_lastSum = y;
			if(onComparatorSample)
				onComparatorSample(y);

			m_hw.runUntil(end);

			_out[i] = calibrating ? 0.0f : std::tanh(y * OutputGain);
		}
	}
}
