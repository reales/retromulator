/**
 * OpenWurli Device adapter for Retromulator
 * Wraps the OpenWurli Wurlitzer 200A engine as a synthLib::Device
 *
 * Based on OpenWurli 0.9.0 (GPL v3), physically modeled Wurlitzer 200A
 */
#pragma once

#include "../synthLib/device.h"
#include "owVoice.h"
#include "owDkPreamp.h"
#include "owTremolo.h"
#include "owOversampler.h"
#include "owPowerAmp.h"
#include "owSpeaker.h"
#include "owTables.h"

#include <atomic>
#include <vector>
#include <cstdint>
#include <array>

namespace openWurliLib
{

/// Per-sample linear ramp toward a target so block-rate setters don't zipper.
class LinearSmoother
{
public:
	void init(double initial, uint32_t rampSamples)
	{
		m_current = m_target = initial;
		m_step = 0.0;
		m_remaining = 0;
		m_ramp = rampSamples;
	}

	void setTarget(double target)
	{
		if (std::abs(target - m_target) < 1e-9)
			return;
		m_target = target;
		if (m_ramp == 0)
		{
			m_current = target;
			m_remaining = 0;
			return;
		}
		m_step = (target - m_current) / static_cast<double>(m_ramp);
		m_remaining = m_ramp;
	}

	void snapTo(double value)
	{
		m_current = m_target = value;
		m_step = 0.0;
		m_remaining = 0;
	}

	double target() const { return m_target; }

	double next()
	{
		if (m_remaining > 0)
		{
			m_current += m_step;
			m_remaining--;
			if (m_remaining == 0)
				m_current = m_target;
		}
		return m_current;
	}

private:
	double m_current = 0.0;
	double m_target = 0.0;
	double m_step = 0.0;
	uint32_t m_remaining = 0;
	uint32_t m_ramp = 0;
};

class Device : public synthLib::Device
{
public:
	Device(const synthLib::DeviceCreateParams& _params);
	~Device() override;

	float getSamplerate() const override;
	bool isValid() const override;

#if SYNTHLIB_DEMO_MODE == 0
	bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
	bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

	uint32_t getChannelCountIn() override { return 0; }
	uint32_t getChannelCountOut() override { return 2; }

	bool setDspClockPercent(uint32_t _percent) override;
	uint32_t getDspClockPercent() const override { return 100; }
	uint64_t getDspClockHz() const override;

	// ── Parameter accessors (for UI) ─────────────────────────────────────
	float getVolume()           const { return m_volume; }
	float getTremoloDepth()     const { return m_tremoloDepth; }
	float getSpeakerCharacter() const { return m_speakerCharacter; }
	bool  getMlpEnabled()       const { return m_mlpEnabled; }
	int   getVelocityCurve()    const { return m_velocityCurve; }

	void setVolume(float v)           { m_volume           = std::clamp(v, 0.0f, 1.0f); }
	void setTremoloDepth(float v)     { m_tremoloDepth     = std::clamp(v, 0.0f, 1.0f); }
	void setSpeakerCharacter(float v) { m_speakerCharacter = std::clamp(v, 0.0f, 1.0f); }
	void setMlpEnabled(bool v)        { m_mlpEnabled = v; }
	void setVelocityCurve(int curve)  { m_velocityCurve = std::clamp(curve, 0, 4); }

	static constexpr int kVelocityCurveDefault = 2; // Medium

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	void noteOn(uint8_t note, uint8_t velocity);
	void noteOff(uint8_t note);
	void allNotesOff();
	size_t allocateVoice();
	void renderVoicesToAmpOut(size_t offset, size_t len);
	void renderOutput(float* outL, float* outR, size_t len);
	void warmUp();
	void resetChain();
	void cleanupVoices();

	// Voice management
	static constexpr size_t MAX_VOICES = 64;

	enum class VoiceState { Free, Held, Sustained, Releasing };

	struct VoiceSlot
	{
		openWurli::Voice voice;
		VoiceState state = VoiceState::Free;
		uint8_t midiNote = 0;
		uint64_t age = 0;
		// Stealing crossfade
		openWurli::Voice stealVoice;
		bool hasStealVoice = false;
		uint32_t stealFade = 0;
		uint32_t stealFadeLen = 0;
	};

	std::array<VoiceSlot, MAX_VOICES> m_voices;
	uint64_t m_ageCounter = 0;

	// Shared signal chain (mono, post voice-sum)
	openWurli::DkPreamp m_preamp;
	openWurli::Tremolo m_tremolo;
	openWurli::Oversampler m_oversampler;
	openWurli::PowerAmp m_powerAmp;
	openWurli::Speaker m_speaker;

	// C-9 pole at the amp input (volume network), one-pole state and coefficient
	double m_c9State = 0.0;
	double m_c9Alpha = 1.0;

	// Parameters (MIDI CC mapped)
	float m_volume = 0.8f;
	float m_expression = 1.0f;
	float m_tremoloDepth = 0.5f;
	float m_speakerCharacter = 0.0f;
	bool m_mlpEnabled = false;
	int   m_velocityCurve = kVelocityCurveDefault;

	// Smoothed audio-rate params
	LinearSmoother m_volumeSmoother;
	LinearSmoother m_tremoloDepthSmoother;
	LinearSmoother m_speakerCharacterSmoother;

	// Oversampling
	bool m_oversample = true;
	double m_sampleRate = 44100.0;
	double m_osSampleRate = 88200.0;

	// Scratch buffers
	static constexpr size_t MAX_BLOCK = 8192;
	std::vector<double> m_voiceBuf;
	std::vector<double> m_sumBuf;
	std::vector<double> m_upBuf;
	std::vector<double> m_outBuf;

	// Sustain pedal: held voices transition Held → Sustained on note-off,
	// released when the pedal lifts.
	bool m_sustainPedal = false;

	std::atomic<bool> m_shutdown{false};
};

} // namespace openWurliLib
