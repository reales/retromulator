// FMA contraction: see cem3396.cpp. The reference and SIMD voice sums below must fuse alike
#if defined(__clang__)
#pragma clang fp contract(on)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#include "voiceBoard.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace matrixClean
{
	using matrixLib::Cem3396;
	using matrixLib::Cem3396Bank;

	namespace
	{
		// 12 bit DAC, range 0 spans 5.42 V
		constexpr float DacFullScale = 5.42f * 145.0f / 159.6f;
		constexpr float DacReference = DacFullScale / 0.996f;
		constexpr uint64_t SyncPulseTicks = 7;          // 470 pF and 10 kOhm into a HC gate, about 3.5 us

		constexpr float OutputGain = 0.18f;
		// the summing amplifier clips at 10.5 V
		constexpr float OutputRail = OutputGain * 10.5f / 2.0f;

		constexpr double EdgeMargin = 1.0;              // one bus tick

		// discharge queues: capacity reserved at reset, pruned before it is reached (no allocation in render)
		constexpr size_t DischargeCapacity = 1024;
		constexpr size_t DischargePruneAt = 512;

		uint32_t chipOf(const uint32_t _voice, const uint32_t _dco) { return (_dco ? 2 : 0) + (_voice >= 3 ? 1 : 0); }
	}

	VoiceBoard::VoiceBoard()
	{
		m_rtCt.fill({RtCtLow, RtCtHigh});
		m_voiceGain.fill(1.0f);
		setSamplerate(48000.0f);
		reset();
	}

	void VoiceBoard::setSamplerate(const float _rate)
	{
		if(!(_rate > 0.0f))
			return;
		m_rate = _rate;
		m_ticksPerSample = static_cast<double>(BusClock) / _rate;
		m_dcCoeff = 1.0f - 2.0f * 3.14159265f * 5.0f / _rate;
		for(auto& v : m_voices)
			v.setSamplerate(_rate * Oversampling);
		m_bank.setSamplerate(_rate * Oversampling);
	}

	void VoiceBoard::reset()
	{
		for(auto& t : m_timers)
			t.reset();
		for(auto& d : m_dco)
		{
			DcoEvents keep = std::move(d.events);
			d = DcoScan();
			d.events = std::move(keep);
			for(auto* q : {&d.events.a, &d.events.b})
			{
				q->clear();
				q->reserve(DischargeCapacity);
			}
		}
		m_rangeHigh.fill(false);
		m_noise = false;
		m_control = 0;
		m_idle = false;

		m_cvCode.fill(0);
		m_cvVolts.fill(0.0f);
		m_cvHold.fill(0.0f);
		m_dacVolts = 0.0f;
		m_dacFast = false;
		m_cvChannel = 0;
		m_cvInhibit = true;

		for(auto& v : m_voices)
			v.reset();
		m_bank.reset();
		m_decimator.reset();

		m_events.clear();
		m_events.reserve(8192);
		m_next = 0;
		m_applied = 0;

		m_tickAcc = 0.0;
		m_sampleTick = 0;
		m_edgeTick = -EdgeMargin;
		m_dcIn = m_dcOut = 0.0f;
		m_noiseState = 0x12345678;
	}

	uint64_t VoiceBoard::usToTick(const double _us)
	{
		return _us <= 0.0 ? 0 : static_cast<uint64_t>(std::llround(_us * (BusClock / 1e6)));
	}

	void VoiceBoard::push(const Event& _e)
	{
		if(m_next > 4096 && m_next * 2 > m_events.size())
		{
			m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(m_next));
			m_next = 0;
		}
		if(m_events.size() == m_next || _e.tick >= m_events.back().tick)
		{
			m_events.push_back(_e);
			return;
		}
		const auto pos = std::upper_bound(m_events.begin() + static_cast<std::ptrdiff_t>(m_next), m_events.end(), _e,
			[](const Event& _x, const Event& _y) { return _x.tick < _y.tick; });
		m_events.insert(pos, _e);
	}

	void VoiceBoard::writeCv(const double _us, const uint32_t _channel, const uint16_t _code, const uint8_t _range)
	{
		push({usToTick(_us), Op::Cv, static_cast<uint8_t>(_channel & 63), static_cast<uint8_t>(_range & 3), _code & 0x1fffu});
	}

	void VoiceBoard::writeCv(const double _us, const uint32_t _channel, const uint16_t _dac, const bool _slow, const uint8_t _range)
	{
		writeCv(_us, _channel, static_cast<uint16_t>((_dac & 0xfff) | (_slow ? CvSlow : 0)), _range);
	}

	void VoiceBoard::inhibitCv(const double _us)
	{
		push({usToTick(_us), Op::CvInhibit, 0, 0, 1});
	}

	void VoiceBoard::writeDcoTimer(const double _us, const uint32_t _voice, const uint32_t _dco, const uint8_t _mode, const uint16_t _count)
	{
		writeDcoMode(_us, _voice, _dco, _mode);
		writeDcoCount(_us, _voice, _dco, _count);
	}

	void VoiceBoard::writeDcoMode(const double _us, const uint32_t _voice, const uint32_t _dco, const uint8_t _mode)
	{
		push({usToTick(_us), Op::DcoMode, static_cast<uint8_t>(_voice), static_cast<uint8_t>(_dco ? 1 : 0), _mode & 7u});
	}

	void VoiceBoard::writeDcoCount(const double _us, const uint32_t _voice, const uint32_t _dco, const uint16_t _count)
	{
		push({usToTick(_us), Op::DcoCount, static_cast<uint8_t>(_voice), static_cast<uint8_t>(_dco ? 1 : 0), _count});
	}

	void VoiceBoard::writeTimer(const double _us, const uint32_t _chip, const uint32_t _reg, const uint8_t _value)
	{
		push({usToTick(_us), Op::Timer, static_cast<uint8_t>(_chip & 3), static_cast<uint8_t>(_reg & 3), _value});
	}

	void VoiceBoard::writeRangeLatch(const double _us, const uint32_t _voice, const uint32_t _dco, const bool _high)
	{
		push({usToTick(_us), Op::Range, static_cast<uint8_t>(_voice), static_cast<uint8_t>(_dco ? 1 : 0), _high ? 1u : 0u});
	}

	void VoiceBoard::writeNoiseLatch(const double _us, const bool _on)
	{
		push({usToTick(_us), Op::Noise, 0, 0, _on ? 1u : 0u});
	}

	void VoiceBoard::writeControlLatch(const double _us, const uint8_t _value)
	{
		push({usToTick(_us), Op::Control, 0, 0, _value});
	}

	void VoiceBoard::setIdle(const double _us, const bool _idle)
	{
		push({usToTick(_us), Op::Idle, 0, 0, _idle ? 1u : 0u});
	}

	void VoiceBoard::cvWrite(const uint64_t, const uint32_t _channel, const uint16_t _code, const uint8_t _range)
	{
		// DAC 0 to -DacFullScale at the I/V converter, inverting amplifier with 12k feedback:
		// A8 removes the 133k in parallel and adds 20k from the reference, A7 adds 182k from it
		const auto bits = static_cast<uint32_t>(_code & 0xfff) << 3;
		const float dac = static_cast<float>(bits) * (DacFullScale / 32768.0f);
		const float feedback = (_range & RangeA8) ? 12.0f : 12.0f * 133.0f / 145.0f;
		float volts = dac * feedback / 10.0f;
		if(_range & RangeA8)
			volts -= DacReference * feedback / 20.0f;
		if(_range & RangeA7)
			volts -= DacReference * feedback / 182.0f;

		m_cvChannel = _channel;
		m_cvInhibit = false;
		m_cvCode[_channel] = _code & 0x1fff;
		m_dacVolts = volts;
		// balance and resonance have no fast path
		m_dacFast = !(_code & CvSlow) && _channel < VoiceCount * 8;
		m_cvHold[_channel] = volts;
		if(m_dacFast)
			m_cvVolts[_channel] = volts;
	}

	void VoiceBoard::timerWrite(const uint64_t _tick, const uint32_t _chip, const uint32_t _reg, const uint8_t _value)
	{
		scanChip(_chip, _tick);
		m_timers[_chip].write(static_cast<uint8_t>(_reg), _value, _tick);
		const uint32_t first = (_chip & 1) * 3;
		for(uint32_t v = first; v < first + 3; ++v)
			(_chip & 2 ? m_dco[v].quietB : m_dco[v].quietA) = 0;
	}

	void VoiceBoard::apply(const Event& _e)
	{
		const uint64_t t = std::max(_e.tick, m_applied);
		m_applied = t;

		switch(_e.op)
		{
		case Op::Cv:
			cvWrite(t, _e.a & 63, static_cast<uint16_t>(_e.value), _e.b);
			break;
		case Op::CvInhibit:
			m_cvInhibit = _e.value != 0;
			break;
		case Op::Timer:
			timerWrite(t, _e.a & 3, _e.b & 3, static_cast<uint8_t>(_e.value));
			break;
		case Op::DcoMode:
		{
			const uint32_t ctr = _e.a % 3;
			timerWrite(t, chipOf(_e.a, _e.b), 3, static_cast<uint8_t>((ctr << 6) | 0x30 | ((_e.value & 7) << 1)));
			break;
		}
		case Op::DcoCount:
		{
			const uint32_t chip = chipOf(_e.a, _e.b);
			const uint32_t ctr = _e.a % 3;
			const auto lsb = static_cast<uint8_t>(_e.value);
			const auto msb = static_cast<uint8_t>(_e.value >> 8);
			switch(m_timers[chip].getCounter(ctr).rw)
			{
			case 1:  timerWrite(t, chip, ctr, lsb); break;
			case 2:  timerWrite(t, chip, ctr, msb); break;
			default: timerWrite(t, chip, ctr, lsb); timerWrite(t, chip, ctr, msb); break;
			}
			break;
		}
		case Op::Range:
			if(_e.a < VoiceCount)
				m_rangeHigh[_e.a * 2 + (_e.b ? 1 : 0)] = _e.value != 0;
			break;
		case Op::Noise:
			m_noise = _e.value != 0;
			break;
		case Op::Control:
			// the sync gates change
			for(uint32_t v = 0; v < VoiceCount; ++v)
				scanDco(v, t);
			m_control = static_cast<uint8_t>(_e.value);
			break;
		case Op::Idle:
			m_idle = _e.value != 0;
			break;
		case Op::Latch:
			break;
		}
	}

	void VoiceBoard::applyUntil(const uint64_t _tick)
	{
		while(m_next < m_events.size() && m_events[m_next].tick < _tick)
			apply(m_events[m_next++]);
		m_applied = std::max(m_applied, _tick);
	}

	void VoiceBoard::advanceCv(const float _seconds)
	{
		// 33 nF hold capacitor into the 6.8 nF pin through 1 MOhm
		constexpr float hold = 33e-9f;
		constexpr float pin = 6.8e-9f;
		constexpr float tau = 1e6f * hold * pin / (hold + pin);
		// the step is the same every sample: the fast path keeps exp's result
		float moved;
		if(!m_reference && _seconds == m_cvStepSeconds)
			moved = m_cvStepMoved;
		else
		{
			moved = 1.0f - std::exp(-_seconds / tau);
			m_cvStepSeconds = _seconds;
			m_cvStepMoved = moved;
		}
		const float toPin = moved * hold / (hold + pin);
		const float fromHold = moved * pin / (hold + pin);

		for(size_t i = 0; i < m_cvVolts.size(); ++i)
		{
			const float d = m_cvHold[i] - m_cvVolts[i];
			m_cvVolts[i] += d * toPin;
			m_cvHold[i] -= d * fromHold;
		}

		// the multiplexer stays on its channel until the next write, the DAC keeps driving it
		if(!m_cvInhibit)
		{
			m_cvHold[m_cvChannel] = m_dacVolts;
			if(m_dacFast)
				m_cvVolts[m_cvChannel] = m_dacVolts;
		}
	}

	void VoiceBoard::scanDcoA(const uint32_t _voice, const uint64_t _until)
	{
		auto& d = m_dco[_voice];
		auto& timer = m_timers[chipOf(_voice, 0)];
		const uint32_t ch = _voice % 3;

		uint64_t t = d.scannedA;
		if(_until <= t)
			return;
		// no edge before quietA: getOut would return levelA and nextOutEdge quietA again
		if(_until <= d.quietA && !m_reference)
		{
			d.scannedA = _until;
			return;
		}
		while(true)
		{
			const bool level = timer.getOut(ch, t);
			if(level != d.levelA)
			{
				d.levelA = level;
				if(level)
				{
					// a sync discharge may already be queued behind this tick
					auto& a = d.events.a;
					a.insert(std::upper_bound(a.begin(), a.end(), t), t);
					if(a.size() >= DischargePruneAt)
						pruneDischarges(a);
				}
			}
			const uint64_t e = timer.nextOutEdge(ch, t);
			if(e >= _until)
			{
				d.quietA = e;
				break;
			}
			t = e;
		}
		d.scannedA = _until;
	}

	void VoiceBoard::scanDco(const uint32_t _voice, const uint64_t _until)
	{
		auto& d = m_dco[_voice];
		auto& timerA = m_timers[chipOf(_voice, 0)];
		auto& timerB = m_timers[chipOf(_voice, 1)];
		const uint32_t ch = _voice % 3;

		const bool syncGate = !(m_control & CtlSync1Off);
		const bool syncDischarge = !(m_control & CtlSync2Off);

		uint64_t t = d.scannedB;
		if(_until > t && _until <= d.quietB && !m_reference)
			d.scannedB = _until;
		else if(_until > t)
		{
			while(true)
			{
				const bool level = timerB.getOut(ch, t);
				if(level != d.levelB)
				{
					d.levelB = level;
					if(level)
					{
						d.events.b.push_back(t);
						if(d.events.b.size() >= DischargePruneAt)
							pruneDischarges(d.events.b);
					}
					else if(syncGate || syncDischarge)
					{
						// the differentiated falling edge holds the gates low for SyncPulseTicks
						scanDcoA(_voice, t);
						if(syncGate)
						{
							timerA.gate(ch, t, t + SyncPulseTicks);
							d.quietA = 0;
						}
						if(syncDischarge)
						{
							d.events.a.push_back(t + SyncPulseTicks);
							if(d.events.a.size() >= DischargePruneAt)
								pruneDischarges(d.events.a);
						}
					}
				}
				const uint64_t e = timerB.nextOutEdge(ch, t);
				if(e >= _until)
				{
					d.quietB = e;
					break;
				}
				t = e;
			}
			d.scannedB = _until;
		}
		scanDcoA(_voice, _until);
	}

	void VoiceBoard::pruneDischarges(std::vector<uint64_t>& _ticks) const
	{
		// Pulses before the span of the next rendered sample (m_edgeTick) all land at its start,
		// where buildDischarges() keeps the first Discharges::Max and drops the others. They pile
		// up only while the voices are idle; dropping them here gives the same samples
		size_t old = 0;
		while(old < _ticks.size() && static_cast<double>(_ticks[old]) < m_edgeTick)
			++old;
		constexpr size_t keep = Cem3396::Discharges::Max;
		if(old > keep)
			_ticks.erase(_ticks.begin() + keep, _ticks.begin() + static_cast<std::ptrdiff_t>(old));
	}

	void VoiceBoard::scanChip(const uint32_t _chip, const uint64_t _until)
	{
		const uint32_t first = (_chip & 1) * 3;
		for(uint32_t v = first; v < first + 3; ++v)
			scanDco(v, _until);
	}

	uint32_t VoiceBoard::getDcoPeriod(const uint32_t _voice, const uint32_t _dco)
	{
		scanDco(_voice, m_sampleTick);
		return m_timers[chipOf(_voice, _dco)].getPeriod(_voice % 3, m_sampleTick);
	}

	void VoiceBoard::buildControls(const uint32_t _voice, Cem3396::Controls& _c)
	{
		const uint32_t pA = getDcoPeriod(_voice, 0);
		const uint32_t pB = getDcoPeriod(_voice, 1);
		_c.periodA = pA ? static_cast<float>(pA) / BusClock : 0.0f;
		_c.periodB = pB ? static_cast<float>(pB) / BusClock : 0.0f;
		_c.rtCtA = m_rangeHigh[_voice * 2] ? RtCtHigh : RtCtLow;
		_c.rtCtB = m_rangeHigh[_voice * 2 + 1] ? RtCtHigh : RtCtLow;

		const auto& cv = m_cvVolts;
		_c.wsA = cv[voiceCv(_voice, CvWsA)];
		_c.wsB = cv[voiceCv(_voice, CvWsB)];
		_c.pwA = cv[voiceCv(_voice, CvPwA)];
		_c.pwB = cv[voiceCv(_voice, CvPwB)];
		_c.freq = cv[voiceCv(_voice, CvFreq)];
		_c.logGain = cv[voiceCv(_voice, CvLog)];
		_c.linGain = cv[voiceCv(_voice, CvLin)];
		_c.mod = cv[voiceCv(_voice, CvMod)];
		_c.balance = cv[balanceCv(_voice)];
		_c.resonance = cv[resonanceCv(_voice)];

		// wave select levels
		switch((m_control >> 4) & 3)
		{
		case 3:  _c.slopedA = true;  _c.slopedB = false; break;
		case 2:  _c.slopedA = false; _c.slopedB = false; break;
		case 1:  _c.slopedA = false; _c.slopedB = true;  break;
		default: _c.slopedA = true;  _c.slopedB = true;  break;
		}
		_c.noiseB = m_noise;
	}

	void VoiceBoard::buildDischarges(const uint32_t _voice, const double _from, const double _to, Cem3396::Discharges& _d0, Cem3396::Discharges& _d1)
	{
		_d0.countA = _d0.countB = _d1.countA = _d1.countB = 0;

		scanDco(_voice, m_sampleTick);
		auto& events = m_dco[_voice].events;
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

	float VoiceBoard::noise()
	{
		m_noiseState ^= m_noiseState << 13;
		m_noiseState ^= m_noiseState >> 17;
		m_noiseState ^= m_noiseState << 5;
		return static_cast<float>(static_cast<int32_t>(m_noiseState)) * (1.0f / 2147483648.0f);
	}

	void VoiceBoard::renderVoicesReference(const double _edgeFrom, const double _edgeTo, float& _sum0, float& _sum1)
	{
		Cem3396::Controls c;
		Cem3396::Discharges d0, d1;

		const float n0 = noise();
		const float n1 = noise();
		float sum0 = 0.0f, sum1 = 0.0f;
		if(!m_spread)
		{
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				buildControls(v, c);
				buildDischarges(v, _edgeFrom, _edgeTo, d0, d1);
				sum0 += m_voices[v].process(c, d0, n0);
				sum1 += m_voices[v].process(c, d1, n1);
			}
		}
		else
		{
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				buildControls(v, c);
				c.rtCtA = m_rtCt[v * 2][m_rangeHigh[v * 2] ? 1 : 0];
				c.rtCtB = m_rtCt[v * 2 + 1][m_rangeHigh[v * 2 + 1] ? 1 : 0];
				buildDischarges(v, _edgeFrom, _edgeTo, d0, d1);
				sum0 += m_voices[v].processSpread(c, d0, n0) * m_voiceGain[v];
				sum1 += m_voices[v].processSpread(c, d1, n1) * m_voiceGain[v];
			}
		}
		_sum0 = sum0;
		_sum1 = sum1;
	}

	void VoiceBoard::renderVoices(const double _edgeFrom, const double _edgeTo, float& _sum0, float& _sum1)
	{
		// buildControls() and buildDischarges() for all voices: one scan per voice, the later
		// scans of the reference path find nothing left to do
		Cem3396Bank::Controls bc;
		Cem3396::Discharges d0[VoiceCount], d1[VoiceCount];

		const float n0 = noise();
		const float n1 = noise();
		const uint64_t now = m_sampleTick;
		const auto& cv = m_cvVolts;
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			auto& d = m_dco[v];
			if(now <= d.quietA && now <= d.quietB)
			{
				d.scannedA = std::max(d.scannedA, now);
				d.scannedB = std::max(d.scannedB, now);
			}
			else
				scanDco(v, now);
			const uint32_t pA = m_timers[chipOf(v, 0)].getPeriod(v % 3, now);
			const uint32_t pB = m_timers[chipOf(v, 1)].getPeriod(v % 3, now);
			bc.periodA[v] = pA ? static_cast<float>(pA) / BusClock : 0.0f;
			bc.periodB[v] = pB ? static_cast<float>(pB) / BusClock : 0.0f;
			if(m_spread)
			{
				bc.rtCtA[v] = m_rtCt[v * 2][m_rangeHigh[v * 2] ? 1 : 0];
				bc.rtCtB[v] = m_rtCt[v * 2 + 1][m_rangeHigh[v * 2 + 1] ? 1 : 0];
			}
			else
			{
				bc.rtCtA[v] = m_rangeHigh[v * 2] ? RtCtHigh : RtCtLow;
				bc.rtCtB[v] = m_rangeHigh[v * 2 + 1] ? RtCtHigh : RtCtLow;
			}
			bc.wsA[v] = cv[voiceCv(v, CvWsA)];
			bc.wsB[v] = cv[voiceCv(v, CvWsB)];
			bc.pwA[v] = cv[voiceCv(v, CvPwA)];
			bc.pwB[v] = cv[voiceCv(v, CvPwB)];
			bc.freq[v] = cv[voiceCv(v, CvFreq)];
			bc.logGain[v] = cv[voiceCv(v, CvLog)];
			bc.linGain[v] = cv[voiceCv(v, CvLin)];
			bc.mod[v] = cv[voiceCv(v, CvMod)];
			bc.balance[v] = cv[balanceCv(v)];
			bc.resonance[v] = cv[resonanceCv(v)];

			auto& events = d.events;
			if(events.a.empty() && events.b.empty())
				d0[v].countA = d0[v].countB = d1[v].countA = d1[v].countB = 0;
			else
				buildDischarges(v, _edgeFrom, _edgeTo, d0[v], d1[v]);
		}
		switch((m_control >> 4) & 3)
		{
		case 3:  bc.slopedA = true;  bc.slopedB = false; break;
		case 2:  bc.slopedA = false; bc.slopedB = false; break;
		case 1:  bc.slopedA = false; bc.slopedB = true;  break;
		default: bc.slopedA = true;  bc.slopedB = true;  break;
		}
		bc.noiseB = m_noise;

		float out0[VoiceCount], out1[VoiceCount];
		float sum0 = 0.0f, sum1 = 0.0f;
		if(!m_spread)
		{
			m_bank.process(bc, d0, n0, out0);
			m_bank.process(bc, d1, n1, out1);
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				sum0 += out0[v];
				sum1 += out1[v];
			}
		}
		else
		{
			m_bank.processSpread(bc, d0, n0, out0);
			m_bank.processSpread(bc, d1, n1, out1);
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				sum0 += out0[v] * m_voiceGain[v];
				sum1 += out1[v] * m_voiceGain[v];
			}
		}
		_sum0 = sum0;
		_sum1 = sum1;
	}

	void VoiceBoard::render(float* _out, const size_t _count)
	{
		for(size_t i = 0; i < _count; ++i)
		{
			const uint64_t start = m_sampleTick;
			applyUntil(start);

			// discharge pulses are rendered one sample late, over the previous sample's span
			const double edgeFrom = m_edgeTick;
			const double edgeTo = (static_cast<double>(start) + m_tickAcc) - EdgeMargin;
			m_tickAcc += m_ticksPerSample;
			const auto ticks = static_cast<uint64_t>(m_tickAcc);
			m_tickAcc -= static_cast<double>(ticks);
			const uint64_t end = start + ticks;

			advanceCv(1.0f / m_rate);

			// voices run at twice the output rate
			float sum = 0.0f;
			if(!m_idle || isMuted())
			{
				float sum0 = 0.0f, sum1 = 0.0f;
				if(m_reference)
					renderVoicesReference(edgeFrom, edgeTo, sum0, sum1);
				else
					renderVoices(edgeFrom, edgeTo, sum0, sum1);
				sum = m_decimator.process(sum0, sum1);
			}
			// after the voices: pruneDischarges() reads the start of the span they consume
			m_edgeTick = edgeTo;

			// output coupling capacitor
			const float y = sum - m_dcIn + m_dcCoeff * m_dcOut;
			m_dcIn = sum;
			m_dcOut = y;

			_out[i] = isMuted() ? 0.0f : std::clamp(y * OutputGain, -OutputRail, OutputRail);

			// writes inside this sample's span act from the next sample on
			applyUntil(end);
			m_sampleTick = end;
		}
	}

	// ---- write log ----

	namespace
	{
		std::string trim(const std::string& _s)
		{
			size_t a = 0, b = _s.size();
			while(a < b && std::isspace(static_cast<unsigned char>(_s[a]))) ++a;
			while(b > a && std::isspace(static_cast<unsigned char>(_s[b - 1]))) --b;
			return _s.substr(a, b - a);
		}

		std::string lower(std::string _s)
		{
			for(auto& ch : _s)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			return _s;
		}

		bool toUint(const std::string& _s, uint32_t& _v)
		{
			if(_s.empty())
				return false;
			char* end = nullptr;
			const unsigned long v = std::strtoul(_s.c_str(), &end, 0);
			if(*end)
				return false;
			_v = static_cast<uint32_t>(v);
			return true;
		}
	}

	bool VoiceBoard::parseLogLine(const std::string& _line, Event& _e, std::string* _error)
	{
		auto fail = [&](const char* _msg) { if(_error) *_error = std::string(_msg) + ": " + _line; return false; };

		std::vector<std::string> f;
		size_t pos = 0;
		while(true)
		{
			const size_t comma = _line.find(',', pos);
			f.push_back(trim(_line.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos)));
			if(comma == std::string::npos)
				break;
			pos = comma + 1;
		}
		if(f.size() < 4)
			return fail("expected time_us,target,channel,value");

		char* end = nullptr;
		const double us = std::strtod(f[0].c_str(), &end);
		if(f[0].empty() || *end)
			return fail("bad time_us");

		uint32_t ch = 0, val = 0;
		if(!toUint(f[2], ch) || !toUint(f[3], val))
			return fail("bad channel or value");

		_e = Event();
		_e.tick = usToTick(us);
		_e.value = val;
		const std::string target = lower(f[1]);

		if(target == "cv")
		{
			if(ch >= CvChannelCount) return fail("cv channel out of range");
			_e.op = Op::Cv;
			_e.a = static_cast<uint8_t>(ch);
			_e.b = static_cast<uint8_t>((val >> 13) & 3);
			_e.value = val & 0x1fff;
		}
		else if(target == "mux")      { _e.op = Op::CvInhibit; }
		else if(target == "pit")
		{
			if(ch >= 16) return fail("pit channel out of range");
			_e.op = Op::Timer;
			_e.a = static_cast<uint8_t>(ch >> 2);
			_e.b = static_cast<uint8_t>(ch & 3);
			_e.value = val & 0xff;
		}
		else if(target == "dco" || target == "dcomode" || target == "range")
		{
			if(ch >= VoiceCount * 2) return fail("dco channel out of range");
			_e.op = target == "dco" ? Op::DcoCount : target == "dcomode" ? Op::DcoMode : Op::Range;
			_e.a = static_cast<uint8_t>(ch >> 1);
			_e.b = static_cast<uint8_t>(ch & 1);
		}
		else if(target == "noise")    { _e.op = Op::Noise; }
		else if(target == "ctrl")     { _e.op = Op::Control; _e.value = val & 0xff; }
		else if(target == "idle")     { _e.op = Op::Idle; }
		else if(target == "latch")    { _e.op = Op::Latch; _e.a = static_cast<uint8_t>(ch); _e.value = val & 0xff; }
		else
			return fail("unknown target");
		return true;
	}

	bool VoiceBoard::loadLog(const std::string& _path, std::vector<Event>& _events, std::string* _error)
	{
		std::ifstream in(_path);
		if(!in)
		{
			if(_error) *_error = "cannot open " + _path;
			return false;
		}
		std::string line;
		bool first = true;
		while(std::getline(in, line))
		{
			const std::string t = trim(line);
			if(t.empty() || t[0] == '#')
				continue;
			// header row
			if(first && !(std::isdigit(static_cast<unsigned char>(t[0])) || t[0] == '.' || t[0] == '-'))
			{
				first = false;
				continue;
			}
			first = false;
			Event e;
			if(!parseLogLine(t, e, _error))
				return false;
			_events.push_back(e);
		}
		return true;
	}

	bool VoiceBoard::replayLog(const std::string& _path, std::string* _error)
	{
		std::vector<Event> events;
		if(!loadLog(_path, events, _error))
			return false;
		for(const auto& e : events)
			push(e);
		return true;
	}
}
