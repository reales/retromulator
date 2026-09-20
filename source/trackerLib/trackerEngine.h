#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace trackerLib
{
	// One loaded module. XM and MOD play through the Fasttracker 2 replayer,
	// S3M and IT through the Schism Tracker player with Nuked OPL3 for Adlib.
	class Engine
	{
	public:
		virtual ~Engine() = default;

		virtual void setSampleRate(uint32_t _rate) = 0;
		virtual void setSinc512(bool _enabled) = 0;		// offline quality
		virtual void setTempoScale(double _scale) = 0;	// host bpm / song bpm

		virtual void play(int _order, bool _stopAtEnd) = 0;
		virtual void stop() = 0;
		virtual void render(float* _interleavedStereo, uint32_t _frames) = 0;

		virtual bool hasEnded() const = 0;
		virtual bool isAmigaPanned() const { return false; }	// MOD: hard L-R-R-L
		virtual int getOrder() const = 0;
		virtual int getRow() const = 0;
		virtual int getOrderCount() const = 0;
		virtual int getChannelCount() const = 0;
		virtual int getInitialBpm() const = 0;
		virtual float getChannelLevel(int _channel) = 0;
		virtual std::string getTitle() const = 0;
	};

	bool isModuleExtension(const std::string& _extensionLowerCase);

	// Returns null if the data does not load, or if it is an XM/MOD and the
	// FT2 replayer (one per process) is taken by another instance.
	std::unique_ptr<Engine> createEngine(const std::vector<uint8_t>& _data, uint32_t _sampleRate);
}
