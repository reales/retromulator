/**
 * CA-72 engine host: the CA-72 plugin's processor without its editor and programs.
 * Owns the parameters (shared with the panel GUI) and the voice engine.
 *
 * Based on the CA-72 by Idle Foundry (GPL-3.0-or-later)
 */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PolyEngine.h"

#include <atomic>
#include <map>

namespace ca72Lib
{
class Synth : public juce::AudioProcessor, private juce::Thread, private juce::AudioProcessorParameter::Listener
{
public:
    // LO: fitted light voice. HI and ULTRA: the circuit as upstream ships it.
    // ULTRA 4X (offline renders only): the circuit voice at 4x the host rate, decimated once at the output.
    enum QualityMode { qualityLo, qualityHi, qualityUltra, qualityUltra4x };

    Synth();
    ~Synth() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void audioWorkgroupContextChanged (const juce::AudioWorkgroup& wg) override;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "CA-72"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Every parameter back to its default except QUALITY, POLY and UNISON, then the given values (a preset).
    void applyPreset (const std::map<juce::String, float>& values);
    // Every sound parameter (QUALITY left out) as id -> value.
    std::map<juce::String, float> getPresetValues() const;

    // Parameters edited on the message thread (the panel) since the last call, by index
    // into getParameters(). Edits made on the audio thread (MIDI CCs) are not reported.
    uint64_t takeEditedParams() { return edited.exchange (0); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> building { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    ca72::voice::Panel readPanel() const;
    ca72poly::Settings readSettings() const;
    void run() override;
    void requestEngine (QualityMode mode);
    std::unique_ptr<ca72poly::Engine> buildEngine (QualityMode mode, double rate) const;
    QualityMode modeNow() const;

    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}

    double hostRate = 48000.0;
    uint64_t seed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    std::unique_ptr<ca72poly::Engine> engine;
    std::unique_ptr<ca72poly::Engine> pending;
    juce::SpinLock swapLock;
    std::atomic<int> wantedMode { -1 };
    std::atomic<bool> fresh { false };
    juce::AudioBuffer<float> scratch;
    juce::AudioWorkgroup workgroup;
    juce::SpinLock workgroupLock;

    std::atomic<uint64_t> edited { 0 };
    std::atomic<bool> applyingPreset { false };

    float lastGain = -1.0f;
    float bend = 0.0f;
    float modWheel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Synth)
};
} // namespace ca72Lib
