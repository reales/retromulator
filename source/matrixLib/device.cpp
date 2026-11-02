#include "device.h"

#include "patch.h"
#include "clean/lfo.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

namespace matrixLib
{
	namespace
	{
		using matrixClean::HwWrite;
		using matrixClean::Target;

		// four times the MIDI rate, which keeps controller and pitch bend streams from
		// queueing up behind the 31.25 kbaud line
		constexpr double ByteUs = matrixClean::Core::ByteUs / 4.0;

		constexpr float DefaultSamplerate = 48000.0f;
		constexpr size_t MaxBlock = 4096;   // per unit buffers, grown only by a larger host block

		constexpr float SleepPeak = 3.1623e-6f;   // -110 dBFS
		constexpr float SleepSeconds = 0.1f;

		constexpr float LimitThreshold = 0.95f;
		constexpr float LimitReleaseS = 0.15f;
		float limitRelease(const float _rate) { return std::exp(-1.0f / (LimitReleaseS * _rate)); }

		// unit 0 keeps the instance seed, the others get their own from it
		uint32_t deriveSeed(const uint32_t _seed, const uint32_t _unit)
		{
			uint32_t x = _seed + _unit * 0x9e3779b9u;
			x = (x ^ (x >> 16)) * 0x7feb352du;
			x = (x ^ (x >> 15)) * 0x846ca68bu;
			return x ^ (x >> 16);
		}

		// continuous messages that only need their latest value per block
		uint32_t coalesceKey(const synthLib::SMidiEvent& _ev)
		{
			if(!_ev.sysex.empty() || _ev.a < 0x80)
				return 0;
			switch(_ev.a & 0xf0)
			{
			case 0xb0:
				// switches, bank select, data entry and mode messages carry state in every value
				if(_ev.b == 0 || _ev.b == 32 || _ev.b == 6 || _ev.b == 38 || (_ev.b >= 64 && _ev.b <= 69) || _ev.b >= 96)
					return 0;
				return 0x10000u | (_ev.a << 8) | _ev.b;
			case 0xd0:
			case 0xe0: return 0x10000u | (_ev.a << 8);
			default:   return 0;
			}
		}

		// LFO rate per speed value: level step per voice block, measured block rate
		constexpr double LfoHzPerIncrement = 5.9466e-4;

		struct LfoDivision { const char* name; double beats; };
		constexpr LfoDivision LfoDivisions[] =
		{
			{"Off", 0.0},
			{"4/1", 16.0}, {"2/1", 8.0}, {"1/1", 4.0},
			{"1/2 Dotted", 3.0}, {"1/2", 2.0}, {"1/2 Triplet", 4.0 / 3.0},
			{"1/4 Dotted", 1.5}, {"1/4", 1.0}, {"1/4 Triplet", 2.0 / 3.0},
			{"1/8 Dotted", 0.75}, {"1/8", 0.5}, {"1/8 Triplet", 1.0 / 3.0},
			{"1/16 Dotted", 0.375}, {"1/16", 0.25}, {"1/16 Triplet", 1.0 / 6.0},
			{"1/32", 0.125},
		};
		constexpr uint32_t LfoDivisionCount = sizeof(LfoDivisions) / sizeof(LfoDivisions[0]);
		constexpr double LfoSyncTolerance = 0.06;

		// without a host playhead, MIDI clock or SysEx tempo (the standalone app)
		constexpr float DefaultBpm = 120.0f;
		constexpr float MinBpm = 20.0f;
		constexpr float MaxBpm = 999.0f;

		// nearest speed for a rate, and how far off it is (ratio - 1)
		int nearestLfoSpeed(const double _hz, double& _error)
		{
			int best = 0;
			double bestErr = 1e9;
			for(int i = 0; i < 64; ++i)
			{
				const double hz = matrixClean::lfoCurve::step(i) * LfoHzPerIncrement;
				const double err = std::fabs(std::log(hz / _hz));
				if(err < bestErr)
				{
					bestErr = err;
					best = i;
				}
			}
			_error = std::exp(bestErr) - 1.0;
			return best;
		}

		void pushToBoard(matrixClean::VoiceBoard& _vb, const HwWrite& _w)
		{
			const double t = _w.timeUs;
			switch(_w.target)
			{
			case Target::Cv: _vb.writeCv(t, _w.channel, static_cast<uint16_t>(_w.value & 0x1fff), static_cast<uint8_t>((_w.value >> 13) & 3)); break;
			case Target::Mux: if(_w.value) _vb.inhibitCv(t); break;
			case Target::Pit: _vb.writeTimer(t, _w.channel / 4, _w.channel % 4, static_cast<uint8_t>(_w.value)); break;
			case Target::DcoMode: _vb.writeDcoMode(t, _w.channel / 2, _w.channel % 2, static_cast<uint8_t>(_w.value)); break;
			case Target::Dco: _vb.writeDcoCount(t, _w.channel / 2, _w.channel % 2, static_cast<uint16_t>(_w.value)); break;
			case Target::Range: _vb.writeRangeLatch(t, _w.channel / 2, _w.channel % 2, _w.value != 0); break;
			case Target::Noise: _vb.writeNoiseLatch(t, _w.value != 0); break;
			case Target::Ctrl: _vb.writeControlLatch(t, static_cast<uint8_t>(_w.value)); break;
			case Target::Idle: _vb.setIdle(t, _w.value != 0); break;
			case Target::Latch: break;
			}
		}
	}

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		for(auto& n : m_noteBoard)
			n.fill(-1);
		const auto seed = randomAnalogSeed();
		for(auto& b : m_boards)
		{
			b = std::make_unique<Board>();
			b->core.setOutputMode(matrixClean::OutputMode::Vintage);
			b->board.setSamplerate(DefaultSamplerate);
			b->board.reset();
			b->core.setAnalog(&b->analog);
			b->mono.resize(MaxBlock);
		}
		m_boards[0]->analog.setSeed(seed);
		deriveAnalog();
		applyAnalog();
		m_limitRelease = limitRelease(DefaultSamplerate);
		m_writeBuf.reserve(8192);
		m_outBuf.reserve(4096);
		m_pending.reserve(512);
		m_midiOut.reserve(64);
		m_txSysex.reserve(1024);
		m_boards[0]->core.getState(m_stateBuf);
		for(uint32_t b = 0; b < BoardCount; ++b)
			flushWrites(b);
	}

	Device::~Device() = default;

	void Device::getSupportedSamplerates(std::vector<float>& _dst) const
	{
		_dst.push_back(44100.0f);
		_dst.push_back(48000.0f);
		_dst.push_back(88200.0f);
		_dst.push_back(96000.0f);
	}

	bool Device::setSamplerate(const float _samplerate)
	{
		if(!synthLib::Device::setSamplerate(_samplerate))
			return false;
		std::lock_guard lock(m_mutex);
		for(auto& b : m_boards)
			b->board.setSamplerate(_samplerate);
		m_limitRelease = limitRelease(_samplerate);
		return true;
	}

#if SYNTHLIB_DEMO_MODE == 0
	bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType)
	{
		// the audio thread waits only for two copies into buffers that already have the room
		std::lock_guard stateLock(m_stateMutex);
		{
			std::lock_guard lock(m_mutex);
			m_boards[0]->core.getState(m_stateBuf);
			m_analogCopy = m_boards[0]->analog;
		}
		_state.insert(_state.end(), m_stateBuf.begin(), m_stateBuf.end());
		const auto a = m_analogCopy.getState();
		_state.insert(_state.end(), a.begin(), a.end());
		return true;
	}

	bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType)
	{
		std::lock_guard lock(m_mutex);
		for(auto& b : m_boards)
			if(!b->core.setState(_state))
				return false;
		clearNotes(-1);
		// the core ignores the analog chunk behind its state; without one the unit is off
		auto& analog = m_boards[0]->analog;
		size_t offset = 0, size = 0;
		if(!matrixClean::analog::Unit::findState(_state, matrixClean::Core::stateSize(), offset, size) || !analog.setState(&_state[offset], size))
			analog.setEnabled(false);
		deriveAnalog();
		applyAnalog();
		for(uint32_t b = 0; b < BoardCount; ++b)
			flushWrites(b);
		return true;
	}
#endif

	void Device::resetMemory()
	{
		std::vector<uint8_t> init;
		std::make_unique<matrixClean::Core>()->getState(init);
		std::lock_guard lock(m_mutex);
		for(auto& b : m_boards)
			b->core.setState(init);
		clearNotes(-1);
		m_boards[0]->analog.setEnabled(false);
		deriveAnalog();
		applyAnalog();
		for(uint32_t b = 0; b < BoardCount; ++b)
			flushWrites(b);
	}

	// units 1 and 2: unit 0's settings with their own seed, so they differ like separate units
	void Device::deriveAnalog()
	{
		const auto& a0 = m_boards[0]->analog;
		for(uint32_t b = 1; b < BoardCount; ++b)
		{
			auto& a = m_boards[b]->analog;
			const auto seed = deriveSeed(a0.seed(), b);
			if(a.seed() != seed)
				a.setSeed(seed);
			if(a.amount() != a0.amount())
				a.setAmount(a0.amount());
			if(a.residual() != a0.residual())
				a.setResidual(a0.residual());
			a.setThermal(a0.thermal());
			a.setEnabled(a0.enabled());
		}
	}

	void Device::applyAnalog()
	{
		for(auto& b : m_boards)
			b->analog.applyTo(b->board);
	}

	void Device::setAnalogEnabled(const bool _on)
	{
		std::lock_guard lock(m_mutex);
		m_boards[0]->analog.setEnabled(_on);
		deriveAnalog();
		applyAnalog();
	}

	bool Device::getAnalogEnabled() const
	{
		std::lock_guard lock(m_mutex);
		return m_boards[0]->analog.enabled();
	}

	void Device::setAnalogAmount(const float _amount)
	{
		std::lock_guard lock(m_mutex);
		m_boards[0]->analog.setAmount(_amount);
		deriveAnalog();
		applyAnalog();
	}

	float Device::getAnalogAmount() const
	{
		std::lock_guard lock(m_mutex);
		return m_boards[0]->analog.amount();
	}

	void Device::setAnalogSeed(const uint32_t _seed)
	{
		std::lock_guard lock(m_mutex);
		m_boards[0]->analog.setSeed(_seed);
		deriveAnalog();
		applyAnalog();
	}

	uint32_t Device::getAnalogSeed() const
	{
		std::lock_guard lock(m_mutex);
		return m_boards[0]->analog.seed();
	}

	void Device::setAnalogMode(const matrixClean::analog::Thermal _mode)
	{
		std::lock_guard lock(m_mutex);
		m_boards[0]->analog.setThermal(_mode);
		deriveAnalog();
	}

	matrixClean::analog::Thermal Device::getAnalogMode() const
	{
		std::lock_guard lock(m_mutex);
		return m_boards[0]->analog.thermal();
	}

	void Device::setAnalogResidual(const matrixClean::analog::Residual _residual)
	{
		std::lock_guard lock(m_mutex);
		m_boards[0]->analog.setResidual(_residual);
		deriveAnalog();
	}

	matrixClean::analog::Residual Device::getAnalogResidual() const
	{
		std::lock_guard lock(m_mutex);
		return m_boards[0]->analog.residual();
	}

	uint32_t Device::randomAnalogSeed()
	{
		std::random_device rd;
		return rd();
	}

	void Device::flushWrites(const uint32_t _board)
	{
		auto& b = *m_boards[_board];
		b.core.takeWrites(m_writeBuf);
		for(const auto& w : m_writeBuf)
			pushToBoard(b.board, w);
		// replies and echo come from unit 0 only
		b.core.takeMidiOut(m_outBuf);
		if(_board == 0)
			for(const auto byte : m_outBuf)
				parseMidiOut(byte);
	}

	uint32_t Device::getLfoDivisionCount()
	{
		return LfoDivisionCount;
	}

	const char* Device::getLfoDivisionName(const uint32_t _division)
	{
		return _division < LfoDivisionCount ? LfoDivisions[_division].name : "";
	}

	void Device::setLfoSync(const uint32_t _lfo, const uint32_t _division)
	{
		m_lfoSync[_lfo & 1].store(_division < LfoDivisionCount ? _division : 0);
		m_lfoSyncDirty.store(true);
	}

	bool Device::hasTempoSource() const
	{
		return m_hostBpm.load() > 0.0f || m_clockBpm.load() > 0.0f || m_sysexBpm.load() > 0.0f;
	}

	float Device::getHostBpm() const
	{
		const float host = m_hostBpm.load();
		if(host > 0.0f)
			return host;
		const float clock = m_clockBpm.load();
		if(clock > 0.0f)
			return clock;
		const float sysex = m_sysexBpm.load();
		return sysex > 0.0f ? sysex : DefaultBpm;
	}

	bool Device::isLfoDivisionReachable(const uint32_t _division) const
	{
		const float bpm = getHostBpm();
		if(_division == 0 || _division >= LfoDivisionCount)
			return true;
		if(bpm <= 0.0f)
			return false;
		double err;
		nearestLfoSpeed(bpm / 60.0 / LfoDivisions[_division].beats, err);
		return err <= LfoSyncTolerance;
	}

	void Device::applyLfoSync()
	{
		const float bpm = getHostBpm();
		m_sentBpm = bpm;
		for(uint32_t lfo = 0; lfo < 2; ++lfo)
		{
			const auto division = m_lfoSync[lfo].load();
			int speed = -1;
			if(division != 0 && bpm > 0.0f)
			{
				double err;
				speed = nearestLfoSpeed(bpm / 60.0 / LfoDivisions[division].beats, err);
			}
			for(auto& b : m_boards)
				b->core.setLfoSpeedOverride(lfo, speed);
		}
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
	{
		// tempo for the LFO sync, never forwarded to the core
		const auto& sx = _ev.sysex;
		if(sx.size() == 7 && sx[0] == 0xf0 && sx[1] == 0x7d && sx[2] == 0x54 && sx[6] == 0xf7)
		{
			const uint32_t us = (static_cast<uint32_t>(sx[3] & 0x7f) << 14) | ((sx[4] & 0x7f) << 7) | (sx[5] & 0x7f);
			if(us > 0)
				m_sysexBpm.store(std::clamp(static_cast<float>(60000000.0 / us), MinBpm, MaxBpm));
			return true;
		}

		// Logic's AU wrapper streams MIDI Tuning and master tuning SysEx nonstop; only Oberheim
		// messages and the identity request reach the core
		if(!sx.empty() && (sx.size() < 5 || (sx[1] != patch::IdOberheim && !(sx[1] == 0x7e && sx[3] == 0x06 && sx[4] == 0x01))))
			return true;

		if(_ev.sysex.empty() && _ev.a == 0xf8)
		{
			// MIDI clock, 24 per quarter note: the tempo is taken over a whole beat
			const uint64_t t = m_sampleTime + _ev.offset;
			const auto rate = static_cast<double>(getSamplerate());
			if(m_lastClock == 0 || t - m_lastClock > static_cast<uint64_t>(rate))
			{
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			else if(++m_clockCount == 24 && t > m_clockBeatStart)
			{
				m_clockBpm.store(std::clamp(static_cast<float>(60.0 * rate / static_cast<double>(t - m_clockBeatStart)), MinBpm, MaxBpm));
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			m_lastClock = t;
			return true;
		}

		m_pending.push_back({_ev.offset, _ev});
		return true;
	}

	void Device::pushBytes(Board& _b, const uint8_t* _bytes, const size_t _count, const double _timeUs)
	{
		double t = std::max(_timeUs, _b.lineFree);
		for(size_t i = 0; i < _count; ++i)
		{
			t += ByteUs;
			_b.core.midiIn(t, _bytes[i]);
		}
		_b.lineFree = t;
	}

	void Device::broadcast(const uint8_t* _bytes, const size_t _count, const double _timeUs)
	{
		for(auto& b : m_boards)
			pushBytes(*b, _bytes, _count, _timeUs);
	}

	int Device::routeNoteOn(const uint8_t _channel, const uint8_t _note)
	{
		auto& held = m_noteBoard[_channel][_note];
		if(held >= 0)
			return held;

		// unison and mono modes play one note per unit, so each note gets its own unit
		auto& core = m_boards[0]->core;
		const bool mono = core.editBuffer().keyboardMode() == matrixClean::KeyboardMode::Unison
			|| core.master().unison() || core.master().monoMode();
		const uint32_t capacity = mono ? 1 : matrixClean::VoiceCount;

		// lowest unit with a free voice: 1-6 notes stay on unit 0, the next unit wakes when needed
		int board = -1;
		for(uint32_t b = 0; b < BoardCount && board < 0; ++b)
			if(m_boardNotes[b] < capacity)
				board = static_cast<int>(b);
		if(board < 0)
		{
			// all units full: the one holding the oldest note, its own allocator steals
			uint32_t oldest = 0;
			int oc = -1, on = -1;
			for(int c = 0; c < 16; ++c)
				for(int n = 0; n < 128; ++n)
				{
					const uint32_t age = m_noteClock - m_noteAge[c][n];
					if(m_noteBoard[c][n] >= 0 && m_noteCounted[c][n] && (oc < 0 || age > oldest))
					{
						oldest = age;
						oc = c;
						on = n;
					}
				}
			board = oc >= 0 ? m_noteBoard[oc][on] : 0;
			if(oc >= 0)
			{
				m_noteCounted[oc][on] = false;
				--m_boardNotes[board];
			}
		}
		held = static_cast<int8_t>(board);
		m_noteAge[_channel][_note] = m_noteClock++;
		m_noteCounted[_channel][_note] = true;
		++m_boardNotes[board];
		return board;
	}

	void Device::noteOff(const uint8_t _channel, const uint8_t _note)
	{
		auto& held = m_noteBoard[_channel][_note];
		if(held >= 0 && m_noteCounted[_channel][_note])
			--m_boardNotes[held];
		m_noteCounted[_channel][_note] = false;
		held = -1;
	}

	void Device::clearNotes(const int _channel)
	{
		for(int c = 0; c < 16; ++c)
		{
			if(_channel >= 0 && c != _channel)
				continue;
			for(int n = 0; n < 128; ++n)
				if(m_noteBoard[c][n] >= 0)
					noteOff(static_cast<uint8_t>(c), static_cast<uint8_t>(n));
		}
	}

	bool Device::envelopesDone(const uint32_t _board) const
	{
		using Stage = matrixClean::Envelope::Stage;
		const auto& core = m_boards[_board]->core;
		for(uint32_t v = 0; v < matrixClean::VoiceCount; ++v)
			for(uint32_t e = 0; e < 3; ++e)
			{
				const auto& env = core.envelope(v, e);
				if(env.stage() != Stage::Idle && !(env.stage() == Stage::Release && env.level() == 0))
					return false;
			}
		return true;
	}

	void Device::wake(const uint32_t _board, const double _timeUs)
	{
		auto& unit = *m_boards[_board];
		unit.quietSamples = 0;
		if(!unit.asleep)
			return;
		unit.asleep = false;
		unit.board.setIdle(_timeUs, false);
	}

	// Units 1 and 2 sleep (voice board idle: the voices are not rendered, Core, timers and CVs
	// keep running) once no note is routed to them, every envelope of every voice is idle or
	// at zero in release, and the output peak stayed under SleepPeak for SleepSeconds. Unit 0
	// never sleeps, so up to six notes render exactly like one Matrix-1000
	void Device::updateSleep(const uint32_t _board, const size_t _samples)
	{
		auto& unit = *m_boards[_board];
		if(_board == 0 || unit.asleep || !m_sleepEnabled)
			return;
		float peak = 0.0f;
		for(size_t i = 0; i < _samples; ++i)
			peak = std::max(peak, std::fabs(unit.mono[i]));
		if(m_boardNotes[_board] || peak >= SleepPeak || !envelopesDone(_board))
		{
			unit.quietSamples = 0;
			return;
		}
		unit.quietSamples += static_cast<uint32_t>(_samples);
		if(unit.quietSamples < static_cast<uint32_t>(SleepSeconds * unit.board.getSamplerate()))
			return;
		unit.asleep = true;
		unit.board.setIdle(unit.board.getTimeUs(), true);
	}

	void Device::pushMidi(const synthLib::SMidiEvent& _ev, const double _timeUs)
	{
		if(!_ev.sysex.empty())
		{
			broadcast(_ev.sysex.data(), _ev.sysex.size(), _timeUs);
			return;
		}

		const uint8_t status = _ev.a;
		if(status < 0x80)
			return;

		// clock and active sensing do nothing on the Matrix, and every byte costs line time
		if(status >= 0xf8)
			return;

		uint8_t bytes[3] = {status, static_cast<uint8_t>(_ev.b & 0x7f), static_cast<uint8_t>(_ev.c & 0x7f)};
		const uint8_t channel = status & 0x0f;
		size_t count = 3;
		switch(status & 0xf0)
		{
		case 0x90:
			if(bytes[2])
			{
				const auto board = static_cast<uint32_t>(routeNoteOn(channel, bytes[1]));
				wake(board, _timeUs);
				pushBytes(*m_boards[board], bytes, count, _timeUs);
				return;
			}
			[[fallthrough]];
		case 0x80:
		{
			const int board = m_noteBoard[channel][bytes[1]];
			noteOff(channel, bytes[1]);
			// a note the router never saw (or lost) is released everywhere
			if(board >= 0)
				pushBytes(*m_boards[static_cast<size_t>(board)], bytes, count, _timeUs);
			else
				broadcast(bytes, count, _timeUs);
			return;
		}
		case 0xb0:
			if(bytes[1] == 120 || bytes[1] == 123)
				clearNotes(channel);
			break;
		case 0xc0:
		case 0xd0:
			count = 2;
			break;
		case 0xf0:
			count = (status == 0xf1 || status == 0xf3) ? 2 : (status == 0xf2 ? 3 : 1);
			break;
		default:
			break;
		}
		broadcast(bytes, count, _timeUs);
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		// stable insertion sort: std::stable_sort may allocate
		for(auto it = m_pending.begin(); it != m_pending.end(); ++it)
		{
			const auto pos = std::upper_bound(m_pending.begin(), it, *it, [](const PendingEvent& _a, const PendingEvent& _b) { return _a.offset < _b.offset; });
			std::rotate(pos, it, it + 1);
		}

		// drop continuous messages that a later one of the same kind replaces in this block
		for(size_t i = 0; i < m_pending.size(); ++i)
		{
			const auto key = coalesceKey(m_pending[i].event);
			if(!key)
				continue;
			for(size_t j = i + 1; j < m_pending.size(); ++j)
			{
				if(coalesceKey(m_pending[j].event) == key)
				{
					m_pending[i].event.a = 0;
					break;
				}
			}
		}

		{
			std::lock_guard lock(m_mutex);

			for(auto& b : m_boards)
				if(b->mono.size() < _samples)
					b->mono.resize(_samples);

			// every unit runs on the same clock
			auto& board0 = m_boards[0]->board;
			const double usPerSample = 1e6 / static_cast<double>(board0.getSamplerate());
			const double t0 = board0.getTimeUs();

			for(const auto& p : m_pending)
				pushMidi(p.event, t0 + static_cast<double>(std::min<size_t>(p.offset, _samples)) * usPerSample);
			m_pending.clear();

			const bool anySync = m_lfoSync[0].load() || m_lfoSync[1].load();
			if(m_lfoSyncDirty.exchange(false) || (anySync && std::fabs(getHostBpm() - m_sentBpm) > 0.05f))
				applyLfoSync();

			const double t1 = t0 + static_cast<double>(_samples) * usPerSample;
			for(uint32_t b = 0; b < BoardCount; ++b)
			{
				auto& unit = *m_boards[b];
				unit.core.advance(t1);
				flushWrites(b);
				// an envelope the unit starts by itself (LFO or pedal trigger) wakes it
				if(unit.asleep && !envelopesDone(b))
					wake(b, t0);
				unit.board.render(unit.mono.data(), _samples);
				updateSleep(b, _samples);
			}

			// unity sum: one unit sounds as before. Six can reach about 5.7, so a peak limiter
			// (instant attack, LimitReleaseS release) holds the sum at LimitThreshold, above
			// the 0.945 one unit clips at, so it never acts on a single unit
			const float* m0 = m_boards[0]->mono.data();
			for(size_t i = 0; i < _samples; ++i)
			{
				float sum = m0[i];
				for(uint32_t b = 1; b < BoardCount; ++b)
					sum += m_boards[b]->mono[i];
				const float a = std::fabs(sum);
				m_limitEnv = std::max(a, m_limitEnv * m_limitRelease);
				if(m_limitEnv > LimitThreshold)
					sum *= LimitThreshold / m_limitEnv;
				if(_outputs[0]) _outputs[0][i] = sum;
				if(_outputs[1]) _outputs[1][i] = sum;
			}
		}

		m_sampleTime += _samples;
		if(m_lastClock && m_sampleTime - m_lastClock > static_cast<uint64_t>(getSamplerate()))
			m_clockBpm.store(0.0f);
	}

	void Device::parseMidiOut(const uint8_t _byte)
	{
		if(_byte == 0xf0)
		{
			m_txSysex.assign(1, _byte);
			return;
		}
		if(!m_txSysex.empty())
		{
			m_txSysex.push_back(_byte);
			if(_byte == 0xf7)
			{
				synthLib::SMidiEvent e(synthLib::MidiEventSource::Device);
				e.sysex.assign(m_txSysex.begin(), m_txSysex.end());
				m_midiOut.push_back(std::move(e));
				m_txSysex.clear();
			}
			return;
		}
		if(_byte >= 0xf8)
			return;

		if(_byte & 0x80)
		{
			// F4-F7 carry no data
			if(_byte >= 0xf4)
			{
				if(_byte != 0xf7)
					m_midiOut.emplace_back(synthLib::MidiEventSource::Device, _byte, 0, 0);
				m_txRunningStatus = 0;
			}
			else
				m_txRunningStatus = _byte;
			m_txShort.clear();
			return;
		}
		if(!m_txRunningStatus)
			return;

		m_txShort.push_back(_byte);
		const auto type = m_txRunningStatus & 0xf0;
		const size_t len = (type == 0xc0 || type == 0xd0 || m_txRunningStatus == 0xf1 || m_txRunningStatus == 0xf3) ? 1 : 2;
		if(m_txShort.size() == len)
		{
			m_midiOut.emplace_back(synthLib::MidiEventSource::Device, m_txRunningStatus, m_txShort[0], len > 1 ? m_txShort[1] : 0);
			m_txShort.clear();
			if(type == 0xf0)
				m_txRunningStatus = 0;
		}
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		std::lock_guard lock(m_mutex);
		_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}
}
