/**
 * CA-72 Device adapter for Retromulator
 * Wraps the CA-72 Minimoog Model D circuit model as a synthLib::Device
 *
 * Based on the CA-72 by Idle Foundry (GPL-3.0-or-later)
 */
#pragma once

#include "../synthLib/device.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ca72Lib
{
class Synth;

class Device : public synthLib::Device
{
public:
	Device(const synthLib::DeviceCreateParams& _params);
	~Device() override;

	float getSamplerate() const override { return m_samplerate; }
	void getSupportedSamplerates(std::vector<float>& _dst) const override;
	void getPreferredSamplerates(std::vector<float>& _dst) const override { getSupportedSamplerates(_dst); }
	bool setSamplerate(float _samplerate) override;
	bool isValid() const override { return true; }

#if SYNTHLIB_DEMO_MODE == 0
	bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
	bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

	uint32_t getChannelCountIn() override { return 0; }
	uint32_t getChannelCountOut() override { return 2; }

	bool setDspClockPercent(uint32_t) override { return false; }
	uint32_t getDspClockPercent() const override { return 100; }
	uint64_t getDspClockHz() const override { return static_cast<uint64_t>(m_samplerate); }

	// The parameters and engine, shared with the panel so it can outlive the device
	std::shared_ptr<Synth> getSynth() const { return m_synth; }

	// Offline renders take the 4x engine
	void setNonRealtime(bool _offline);

	// .fxp presets: an opaque chunk holding the parameters as XML
	bool loadPreset(const std::string& _path);
	bool savePreset(const std::string& _path, const std::string& _name) const;
	const std::string& getPatchName() const { return m_patchName; }

	// .fxb banks: every program of the bank as an .fxp in the folder, returns the files written
	static std::vector<std::string> extractBank(const std::string& _fxbPath, const std::string& _folder);
	static bool writeBank(const std::string& _fxbPath, const std::vector<std::string>& _fxpPaths);

	// Writes the built-in presets into the folder, skipping files that already exist
	static void writeFactoryPresets(const std::string& _folder);

	// MIDI Program Change N loads file N of the active bank, latched here and polled by the editor
	void setProgramList(std::vector<std::string> _paths) { m_programList = std::move(_paths); }
	const std::vector<std::string>& getProgramList() const { return m_programList; }
	int takePendingProgramChange() { return m_pendingProgram.exchange(-1); }

	// (cc, value) for every parameter with a CC, as that CC would carry it
	std::vector<std::pair<uint8_t, uint8_t>> getCcValues() const;
	// Same for the parameters edited on the panel since the last call
	std::vector<std::pair<uint8_t, uint8_t>> takePanelEdits();

protected:
	void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
	void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
	bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

private:
	std::shared_ptr<Synth> m_synth;
	float m_samplerate = 48000.0f;
	juce::MidiBuffer m_midi;
	juce::AudioBuffer<float> m_out;
	std::string m_patchName;
	std::vector<std::string> m_programList;
	std::atomic<int> m_pendingProgram{-1};
};
} // namespace ca72Lib
