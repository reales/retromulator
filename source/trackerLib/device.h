#pragma once

#include "../synthLib/device.h"
#include "trackerEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace trackerLib
{
	// Trackermeister: tracker module player. XM/MOD through the FT2 replayer,
	// S3M and IT through Schism. Transport and song position follow MIDI notes.
	class Device final : public synthLib::Device
	{
	public:
		static constexpr int kPlayNote      = 12;	// always restarts from the top
		static constexpr int kPrevNote      = 13;	// playlist: the owner loads the module
		static constexpr int kStopNote      = 14;
		static constexpr int kNextNote      = 15;
		static constexpr int kFirstPosNote  = 24;	// note 24 + n starts at order n
		static constexpr int kMeterColumns  = 16;
		static constexpr int kMaxChannels   = 64;
		static constexpr float kSilenceSeconds = 5.0f;

		explicit Device(const synthLib::DeviceCreateParams& _params);
		~Device() override;

		float getSamplerate() const override { return m_sampleRate; }
		void getSupportedSamplerates(std::vector<float>& _dst) const override;
		bool setSamplerate(float _samplerate) override;
		bool isValid() const override { return true; }

		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }

		uint32_t getChannelCountIn() override  { return 0; }
		uint32_t getChannelCountOut() override { return 2; }

		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return static_cast<uint64_t>(m_sampleRate); }

		// Message thread. The old module is released first: the FT2 replayer is one per process.
		bool loadModule(const std::vector<uint8_t>& _data);
		void unloadModule();
		bool hasModule() const { return m_hasModule.load(); }
		std::string getTitle() const;

		// Any thread; the audio thread picks the request up.
		void play();						// from the top, also while playing
		void playFromOrder(int _order);
		void stop();
		// A request the audio thread has not picked up yet already counts.
		bool isPlaying() const;

		void setTempoSync(bool _enabled)	{ m_tempoSync = _enabled; }
		bool getTempoSync() const			{ return m_tempoSync.load(); }
		void setHostBpm(float _bpm)			{ m_hostBpm = _bpm; }
		// Host playhead tempo, or the tempo measured from incoming MIDI clock.
		float getSyncBpm() const;
		bool hasSyncBpm() const				{ return getSyncBpm() > 1.0f; }

		// BassMX, MOD only: bass below 150 Hz goes to the centre, the rest keeps
		// this share of the Amiga hard panning (1 = untouched).
		void setStereoWidth(float _width)	{ m_stereoWidth = std::clamp(_width, 0.0f, 1.0f); }
		float getStereoWidth() const		{ return m_stereoWidth.load(); }

		// Offline: 512 tap sinc, and the song stops at its end instead of looping.
		void setOfflineRender(bool _enabled);
		bool hasEnded() const { return m_ended.load(); }

		// Playlist: a live song stops at its end too, and says so once. So does a song that
		// has been silent for kSilenceSeconds after making sound, when it would stop at its end.
		void setStopAtEnd(bool _enabled)	{ m_stopAtEnd = _enabled; }
		bool consumeSongFinished()			{ return m_songFinished.exchange(false); }
		// Previous / next notes since the last call: -1, +1 or 0.
		int consumePlaylistStep()			{ return m_playlistStep.exchange(0); }

		int getOrder() const		{ return m_order.load(); }
		int getRow() const			{ return m_row.load(); }
		int getOrderCount() const	{ return m_orderCount.load(); }
		// One meter bar per module channel, up to 16. Beyond that the channels are
		// summed into 16 bars.
		int getMeterBars() const	{ return m_meterBars.load(); }
		float getMeterLevel(int _bar) const;

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

	private:
		struct Command
		{
			uint32_t offset;
			int order;		// >= 0 start there, -2 stop
		};

		void applyCommand(const Command& _c);
		void applyTempo();
		void onMidiClock(uint32_t _offset);
		void updateMeters(size_t _samples);
		bool isSilentTooLong(size_t _frames);
		void setupBassMix();
		void processBassMix(float* _interleaved, size_t _frames);

		float m_sampleRate = 48000.0f;

		std::mutex m_engineMutex;
		std::unique_ptr<Engine> m_engine;
		bool m_atTop = true;		// stopped: the position reads as the song start
		bool m_offline = false;
		double m_tempoScale = 1.0;
		bool m_heardSound = false;
		size_t m_silentFrames = 0;

		std::vector<Command> m_commands;	// audio thread only
		std::vector<float> m_buffer;

		// BassMX: 2nd order Butterworth low pass on the side signal
		bool m_bassMix = false;
		double m_lpB0 = 0.0, m_lpB1 = 0.0, m_lpB2 = 0.0, m_lpA1 = 0.0, m_lpA2 = 0.0;
		double m_lpZ1 = 0.0, m_lpZ2 = 0.0;
		float m_width = 0.7f;

		static constexpr int kNoRequest = -100;
		std::atomic<int> m_request{kNoRequest};

		std::atomic<bool> m_hasModule{false};
		std::atomic<bool> m_playing{false};
		std::atomic<bool> m_ended{false};
		std::atomic<bool> m_stopAtEnd{false};
		std::atomic<float> m_gain{1.0f};	// CC 7
		std::atomic<bool> m_songFinished{false};
		std::atomic<int> m_playlistStep{0};
		std::atomic<bool> m_tempoSync{false};
		std::atomic<float> m_stereoWidth{0.7f};
		std::atomic<float> m_hostBpm{0.0f};
		std::atomic<float> m_clockBpm{0.0f};

		// MIDI clock, audio thread: times of the last beat's worth of ticks, in samples
		static constexpr int kClockTicks = 24;
		std::array<uint64_t, kClockTicks + 1> m_clockTimes{};
		int m_clockCount = 0;
		int m_clockIndex = 0;
		uint64_t m_samplePos = 0;
		std::atomic<int> m_order{0};
		std::atomic<int> m_row{0};
		std::atomic<int> m_orderCount{0};
		std::atomic<int> m_meterBars{kMeterColumns};
		std::array<std::atomic<float>, kMeterColumns> m_levels{};
	};
}
