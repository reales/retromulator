#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dsp56kBase/ringbuffer.h"
#include "synthLib/midiTypes.h"

namespace emu88Lib
{
	class Sc88Thread
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		using Render = std::function<SampleFrame()>;
		using SendMidi = std::function<void(const synthLib::SMidiEvent&)>;
		using ReadMidi = std::function<void(std::vector<synthLib::SMidiEvent>&)>;
		using BeforeJob = std::function<void()>;

		Sc88Thread(Render _render, SendMidi _sendMidi, ReadMidi _readMidi, BeforeJob _beforeJob);
		~Sc88Thread();

		void processSamples(uint32_t _count, uint32_t _requiredLatency,
		                    std::vector<synthLib::SMidiEvent>& _midiIn,
		                    std::vector<synthLib::SMidiEvent>& _midiOut);
		bool popSample(SampleFrame& _sample);

		// Frames dropped because their generation predates a transport jump. Steady counting
		// here while audio is running means the generation filter is eating good samples.
		uint64_t takeDiscardedFrames() { return m_discardedFrames.exchange(0); }

		// Samples rendered and wall time spent rendering them since the last call. Their
		// ratio against the sample rate is the render load; above 100% the ring must run dry.
		void takeRenderStats(uint64_t& _samples, uint64_t& _nanoseconds)
		{
			_samples = m_renderedSamples.exchange(0);
			_nanoseconds = m_renderNanoseconds.exchange(0);
		}

	private:
		using TimedMidi = std::pair<uint64_t, synthLib::SMidiEvent>;
		struct ProcessJob
		{
			uint32_t samplesToProcess = 0;
			std::vector<TimedMidi> midiEvents;
		};
		struct OutputFrame
		{
			SampleFrame sample{};
			uint32_t transportGeneration = 0;
		};

		void threadFunc();
		void processJob(ProcessJob& _job);
		void handleTransportDiscontinuity(uint32_t _generation);
		void recoverFromQueueOverflow();
		void silenceActiveChannels();
		void trackMidiActivity(const synthLib::SMidiEvent& _event);

		Render m_render;
		SendMidi m_sendMidi;
		ReadMidi m_readMidi;
		BeforeJob m_beforeJob;
		std::unique_ptr<std::thread> m_thread;
		std::atomic<bool> m_exit = false;
		std::atomic<uint64_t> m_droppedSamples = 0;
		std::atomic<bool> m_recoveryRequested = false;
		std::atomic<uint32_t> m_outputGeneration = 0;
		std::atomic<uint64_t> m_discardedFrames = 0;
		std::atomic<uint64_t> m_renderedSamples = 0;
		std::atomic<uint64_t> m_renderNanoseconds = 0;
		uint32_t m_currentLatency = 0;
		dsp56k::RingBuffer<OutputFrame, 16384, true> m_audioOut;
		std::vector<synthLib::SMidiEvent> m_midiOutput;
		std::mutex m_outputMutex;
		uint64_t m_inSampleOffset = 0;
		std::vector<ProcessJob> m_jobPool;
		dsp56k::RingBuffer<ProcessJob, 32, true> m_pendingJobs;
		uint64_t m_processedSampleOffset = 0;
		uint32_t m_workerGeneration = 0;
		uint64_t m_activeChannels = 0;
		std::vector<synthLib::SMidiEvent> m_tempMidiOut;
		std::vector<TimedMidi> m_tempMidiIn;
	};
}
