/**
 * OpenB3 Device adapter for Retromulator
 * Wraps the Beatrix tonewheel organ and Leslie engine (OpenB3 / setBfree) as a synthLib::Device
 *
 * Based on OpenB3 (AGPL v3, Michele Perrone) and setBfree (GPL v2+, Robin Gareus)
 */
#pragma once

#include "../synthLib/device.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Beatrix;

namespace openB3Lib
{

enum Param : uint8_t
{
	DrawbarUpper1, DrawbarUpper2, DrawbarUpper3, DrawbarUpper4, DrawbarUpper5,
	DrawbarUpper6, DrawbarUpper7, DrawbarUpper8, DrawbarUpper9,
	DrawbarLower1, DrawbarLower2, DrawbarLower3, DrawbarLower4, DrawbarLower5,
	DrawbarLower6, DrawbarLower7, DrawbarLower8, DrawbarLower9,
	DrawbarPedal1, DrawbarPedal2,
	VibratoUpper, VibratoLower, VibratoType,
	PercOn, PercSoft, PercFast, PercHarmonic,
	Overdrive, Drive,
	Rotary, Cabinet, LeslieBypass,
	HornSpeed, DrumSpeed, HornRamp, DrumRamp,
	Balance, MicDistance, Width, Brake, CabinetBody,
	Reverb, Volume,
	ParamCount
};

class Device : public synthLib::Device
{
public:
	Device(const synthLib::DeviceCreateParams& _params);
	~Device() override;

	float getSamplerate() const override;
	bool isValid() const override { return true; }

#if SYNTHLIB_DEMO_MODE == 0
	bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
	bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

	uint32_t getChannelCountIn() override { return 0; }
	uint32_t getChannelCountOut() override { return 2; }

	bool setDspClockPercent(uint32_t) override { return false; }
	uint32_t getDspClockPercent() const override { return 100; }
	uint64_t getDspClockHz() const override;

	uint32_t getInternalLatencyMidiToOutput() const override { return static_cast<uint32_t>(m_overdriveLatency); }

	// .fxp presets: an opaque chunk holding the parameters as XML
	bool loadPreset(const std::string& _path);
	bool savePreset(const std::string& _path, const std::string& _name) const;
	const std::string& getPatchName() const { return m_patchName; }

	// Writes the built-in presets into the folder, skipping files that already exist
	static void writeFactoryPresets(const std::string& _folder);

	// MIDI Program Change N loads file N of the active bank, latched here and polled by the editor
	void setProgramList(std::vector<std::string> _paths) { m_programList = std::move(_paths); }
	const std::vector<std::string>& getProgramList() const { return m_programList; }
	int takePendingProgramChange() { return m_pendingProgram.exchange(-1); }

	// Channel 1 split: pedals below C2, lower manual C2-B3, upper manual from C4
	bool getKeyboardSplit() const { return m_split.load(); }
	void setKeyboardSplit(bool _on) { m_split.store(_on); }

	// (cc, value) for every parameter, as the CC that drives it would carry it
	std::vector<std::pair<uint8_t, uint8_t>> getCcValues() const;

	// Rotary CC that mod wheel (CC1) or sustain (CC64) sets, -1 if the event leaves it alone
	static uint8_t rotaryCc();
	static int rotaryCcFromPedal(uint8_t _cc, uint8_t _value, uint8_t _currentRotaryCc);

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	void setParamCc(Param _p, uint8_t _cc) { m_values[_p].store(_cc, std::memory_order_relaxed); }
	void applyParams(bool _force);
	void processPreamp(float* _in, float* _out, int _n);
	void renderChunk(float* _l, float* _r, int _n);

	std::unique_ptr<Beatrix> m_beatrix;

	// CC value (0-127) per parameter: written from any thread, applied on the audio thread
	std::array<std::atomic<uint8_t>, ParamCount> m_values;
	std::array<int, ParamCount> m_applied{};
	float m_expression = 1.0f;
	float m_appliedExpression = -1.0f;
	std::string m_patchName;
	std::vector<std::string> m_programList;
	std::atomic<int> m_pendingProgram{-1};
	std::atomic<bool> m_split{false};
	bool m_appliedSplit = false;

	// 4x IIR oversampled overdrive; the clean path is delayed by the same amount so latency stays fixed
	juce::dsp::Oversampling<float> m_oversampler { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
	std::vector<float> m_odScratch;
	std::vector<float> m_cleanDelay;
	int m_cleanDelayPos = 0;
	int m_overdriveLatency = 0;

	// cabinet body (one per model, only the active one runs) and stereo plate after the Leslie
	juce::dsp::ConvolutionMessageQueue m_convQueue { 64 };
	std::array<std::unique_ptr<juce::dsp::Convolution>, 3> m_cabinets;
	std::unique_ptr<juce::dsp::Convolution> m_plate;
	int m_activeCabinet = 0;
	juce::AudioBuffer<float> m_bodyBuffer;
	juce::AudioBuffer<float> m_wetBuffer;
	juce::SmoothedValue<float> m_bodyMix, m_width, m_reverbMix;
};

} // namespace openB3Lib
