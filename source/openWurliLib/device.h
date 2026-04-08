/**
 * OpenWurli Device adapter for Retromulator
 * Wraps the OpenWurli Wurlitzer 200A engine as a synthLib::Device
 *
 * Based on OpenWurli (GPL v3) — physically modeled Wurlitzer 200A
 */
#pragma once

#include "../synthLib/device.h"
#include "owVoice.h"
#include "owMelangePreamp.h"
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
	void setTremoloDepth(float v)     { m_tremoloDepth     = std::clamp(v, 0.0f, 1.0f);  m_tremolo.setDepth(m_tremoloDepth); }
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
	void renderSubblock(size_t offset, size_t len);
	void cleanupVoices();

	// Voice management
	static constexpr size_t MAX_VOICES = 64;

	enum class VoiceState { Free, Held, Releasing };

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
	openWurli::MelangePreamp m_preamp;
	openWurli::Tremolo m_tremolo;
	openWurli::Oversampler m_oversampler;
	openWurli::PowerAmp m_powerAmp;
	openWurli::Speaker m_speaker;

	// Parameters (MIDI CC mapped)
	float m_volume = 1.0f;
	float m_tremoloDepth = 0.5f;
	float m_speakerCharacter = 0.0f;
	bool m_mlpEnabled = true;
	int   m_velocityCurve = kVelocityCurveDefault;

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

	// Sustain pedal
	bool m_sustainPedal = false;
	uint8_t m_sustainedNotes[128] = {};  // count of deferred note-offs per note

	std::atomic<bool> m_shutdown{false};
};

} // namespace openWurliLib
