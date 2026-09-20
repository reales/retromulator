#include "device.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace trackerLib
{
	namespace
	{
		constexpr int kStop = -2;
		constexpr float kMeterFallSeconds = 0.3f;
		constexpr double kBassMixHz = 150.0;
		constexpr double kPi = 3.14159265358979323846;
	}

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		m_commands.reserve(64);
		m_buffer.resize(4096 * 2);
	}

	Device::~Device()
	{
		unloadModule();
	}

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

		std::lock_guard lock(m_engineMutex);
		m_sampleRate = _samplerate;
		if(m_engine)
			m_engine->setSampleRate(static_cast<uint32_t>(m_sampleRate));
		setupBassMix();
		return true;
	}

	bool Device::loadModule(const std::vector<uint8_t>& _data)
	{
		unloadModule();

		auto engine = createEngine(_data, static_cast<uint32_t>(m_sampleRate));
		if(!engine)
			return false;

		std::lock_guard lock(m_engineMutex);
		m_engine = std::move(engine);
		m_engine->setSampleRate(static_cast<uint32_t>(m_sampleRate));
		m_engine->setSinc512(m_offline);
		m_tempoScale = 1.0;
		m_orderCount = m_engine->getOrderCount();
		m_bassMix = m_engine->isAmigaPanned();
		setupBassMix();
		m_hasModule = true;
		return true;
	}

	void Device::unloadModule()
	{
		std::unique_ptr<Engine> old;
		{
			std::lock_guard lock(m_engineMutex);
			old = std::move(m_engine);
			m_atTop = true;
			m_hasModule = false;
			m_playing = false;
			m_ended = false;
			m_songFinished = false;
			m_order = m_row = m_orderCount = 0;
			m_request = kNoRequest;
		}
		for(auto& l : m_levels)
			l = 0.0f;
	}

	std::string Device::getTitle() const
	{
		auto& mutex = const_cast<std::mutex&>(m_engineMutex);
		std::lock_guard lock(mutex);
		return m_engine ? m_engine->getTitle() : std::string();
	}

	void Device::play()								{ m_request = 0; }
	void Device::playFromOrder(const int _order)	{ m_request = std::max(0, _order); }
	void Device::stop()								{ m_request = kStop; }

	bool Device::isPlaying() const
	{
		const auto request = m_request.load();
		if(request == kStop)	return false;
		if(request >= 0)		return m_hasModule.load();
		return m_playing.load();
	}

	void Device::setOfflineRender(const bool _enabled)
	{
		std::lock_guard lock(m_engineMutex);
		m_offline = _enabled;
		if(m_engine)
			m_engine->setSinc512(_enabled);
	}

	float Device::getMeterLevel(const int _bar) const
	{
		if(_bar < 0 || _bar >= kMeterColumns)
			return 0.0f;
		return m_levels[static_cast<size_t>(_bar)].load();
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
	{
		if(_ev.sysex.empty() && _ev.a == synthLib::M_TIMINGCLOCK)
		{
			onMidiClock(_ev.offset);
			return true;
		}

		if(!_ev.sysex.empty() || (_ev.a & 0xf0) != 0x90 || _ev.c == 0)
			return true;

		const int note = _ev.b;

		if(note == kPlayNote)
			m_commands.push_back({_ev.offset, 0});
		else if(note == kStopNote)
			m_commands.push_back({_ev.offset, kStop});
		else if(note == kPrevNote)
			m_playlistStep = -1;
		else if(note == kNextNote)
			m_playlistStep = +1;
		else if(note >= kFirstPosNote)
			m_commands.push_back({_ev.offset, note - kFirstPosNote});

		return true;
	}

	void Device::applyCommand(const Command& _c)
	{
		if(_c.order == kStop)
		{
			m_engine->stop();
			m_playing = false;
			m_atTop = true;
			return;
		}

		// position notes past the last order do nothing
		if(_c.order >= m_engine->getOrderCount())
			return;

		m_engine->play(std::max(0, _c.order), m_offline || m_stopAtEnd.load());
		m_songFinished = false;
		m_atTop = false;
		m_ended = false;
		m_playing = true;
	}

	float Device::getSyncBpm() const
	{
		const auto host = m_hostBpm.load();
		return host > 1.0f ? host : m_clockBpm.load();
	}

	void Device::onMidiClock(const uint32_t _offset)
	{
		const auto now = m_samplePos + _offset;
		const auto size = static_cast<int>(m_clockTimes.size());

		// a gap of a second means the clock stopped and started again
		if(m_clockCount > 0)
		{
			const auto last = m_clockTimes[static_cast<size_t>((m_clockIndex + size - 1) % size)];
			if(now <= last || now - last > static_cast<uint64_t>(m_sampleRate))
				m_clockCount = 0;
		}

		m_clockTimes[static_cast<size_t>(m_clockIndex)] = now;
		m_clockIndex = (m_clockIndex + 1) % size;
		m_clockCount = std::min(m_clockCount + 1, size);

		// a few ticks first: one interval alone jitters with the block size
		if(m_clockCount < 7)
			return;

		const int ticks = m_clockCount - 1;
		const auto oldest = m_clockTimes[static_cast<size_t>((m_clockIndex + size - m_clockCount) % size)];
		const auto bpm = 60.0 * m_sampleRate * ticks / (static_cast<double>(kClockTicks) * static_cast<double>(now - oldest));

		if(bpm >= 20.0 && bpm <= 999.0)
			m_clockBpm = static_cast<float>(std::round(bpm * 10.0) / 10.0);
	}

	void Device::applyTempo()
	{
		double scale = 1.0;
		const auto bpm = getSyncBpm();
		const auto songBpm = m_engine->getInitialBpm();

		// the song's initial BPM is taken as the host tempo; its own tempo changes stay relative
		if(m_tempoSync.load() && bpm > 1.0f && songBpm > 0)
			scale = static_cast<double>(bpm) / static_cast<double>(songBpm);

		if(scale != m_tempoScale)
		{
			m_tempoScale = scale;
			m_engine->setTempoScale(scale);
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		auto* outL = _outputs[0];
		auto* outR = _outputs[1];

		m_samplePos += _samples;

		// MIDI clock went away: fall back to the song's own tempo
		if(m_clockCount > 0)
		{
			const auto size = m_clockTimes.size();
			const auto last = m_clockTimes[(static_cast<size_t>(m_clockIndex) + size - 1) % size];
			if(m_samplePos - last > static_cast<uint64_t>(m_sampleRate))
			{
				m_clockCount = 0;
				m_clockBpm = 0.0f;
			}
		}

		std::unique_lock lock(m_engineMutex, std::try_to_lock);

		if(!lock.owns_lock() || !m_engine)
		{
			m_commands.clear();
			std::memset(outL, 0, _samples * sizeof(float));
			std::memset(outR, 0, _samples * sizeof(float));
			return;
		}

		const auto request = m_request.exchange(kNoRequest);
		if(request != kNoRequest)
			m_commands.insert(m_commands.begin(), {0, request});

		applyTempo();

		if(m_buffer.size() < _samples * 2)
			m_buffer.resize(_samples * 2);

		size_t pos = 0;
		size_t cmd = 0;

		while(pos < _samples)
		{
			while(cmd < m_commands.size() && m_commands[cmd].offset <= pos)
				applyCommand(m_commands[cmd++]);

			size_t end = _samples;
			if(cmd < m_commands.size())
				end = std::min<size_t>(end, m_commands[cmd].offset);

			const auto count = end - pos;

			if(m_playing.load())
			{
				m_engine->render(m_buffer.data(), static_cast<uint32_t>(count));
				if(m_bassMix)
					processBassMix(m_buffer.data(), count);
				for(size_t i = 0; i < count; ++i)
				{
					outL[pos + i] = m_buffer[i * 2];
					outR[pos + i] = m_buffer[i * 2 + 1];
				}

				if(m_engine->hasEnded())
				{
					m_playing = false;
					m_ended = true;
					if(!m_offline)
						m_songFinished = true;
				}
			}
			else
			{
				std::memset(outL + pos, 0, count * sizeof(float));
				std::memset(outR + pos, 0, count * sizeof(float));
			}

			pos = end;
		}

		while(cmd < m_commands.size())
			applyCommand(m_commands[cmd++]);
		m_commands.clear();

		m_order = m_atTop ? 0 : m_engine->getOrder();
		m_row = m_atTop ? 0 : m_engine->getRow();

		updateMeters(_samples);
	}

	void Device::setupBassMix()
	{
		const double w0 = 2.0 * kPi * kBassMixHz / static_cast<double>(m_sampleRate);
		const double cosW = std::cos(w0);
		const double alpha = std::sin(w0) / std::sqrt(2.0);
		const double a0 = 1.0 + alpha;

		m_lpB0 = m_lpB2 = (1.0 - cosW) * 0.5 / a0;
		m_lpB1 = (1.0 - cosW) / a0;
		m_lpA1 = -2.0 * cosW / a0;
		m_lpA2 = (1.0 - alpha) / a0;
		m_lpZ1 = m_lpZ2 = 0.0;
		m_width = m_stereoWidth.load();
	}

	void Device::processBassMix(float* _interleaved, const size_t _frames)
	{
		// the side signal loses its bass, what is left is scaled to the width
		const float target = m_stereoWidth.load();
		const float step = (target - m_width) / static_cast<float>(_frames);

		for(size_t i = 0; i < _frames; ++i)
		{
			const double l = _interleaved[i * 2];
			const double r = _interleaved[i * 2 + 1];
			const double mid = (l + r) * 0.5;
			const double side = (l - r) * 0.5;

			const double low = m_lpB0 * side + m_lpZ1;
			m_lpZ1 = m_lpB1 * side - m_lpA1 * low + m_lpZ2;
			m_lpZ2 = m_lpB2 * side - m_lpA2 * low;

			m_width += step;
			const double s = (side - low) * m_width;

			_interleaved[i * 2]     = static_cast<float>(mid + s);
			_interleaved[i * 2 + 1] = static_cast<float>(mid - s);
		}

		m_width = target;
	}

	void Device::updateMeters(const size_t _samples)
	{
		const int channels = std::min(m_engine->getChannelCount(), kMaxChannels);

		std::array<float, kMaxChannels> levels{};
		if(m_playing.load())
		{
			for(int c = 0; c < channels; ++c)
				levels[static_cast<size_t>(c)] = m_engine->getChannelLevel(c);
		}

		const float fall = std::exp(-static_cast<float>(_samples) / (m_sampleRate * kMeterFallSeconds));

		const int bars = std::clamp(channels, 1, kMeterColumns);
		m_meterBars = bars;

		for(int col = 0; col < kMeterColumns; ++col)
		{
			float level = 0.0f;

			if(col < bars && channels <= kMeterColumns)
			{
				level = levels[static_cast<size_t>(col)];
			}
			else if(col < bars)
			{
				// sum: the channels that share a bar add up as power
				const int first = col * channels / bars;
				const int last = (col + 1) * channels / bars;
				float power = 0.0f;
				for(int c = first; c < last; ++c)
					power += levels[static_cast<size_t>(c)] * levels[static_cast<size_t>(c)];
				level = std::min(1.0f, std::sqrt(power));
			}

			// square root: quiet channels still light a few cells
			auto& out = m_levels[static_cast<size_t>(col)];
			out = std::max(std::sqrt(level), out.load() * fall);
		}
	}
}
