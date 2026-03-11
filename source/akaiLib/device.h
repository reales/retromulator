#pragma once

#include "synthLib/device.h"

#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace juce { class AudioFormatManager; }

namespace sfzero { class Synth; class Sound; class SF2Sound; class ZBPSound; }

namespace akaiLib
{
    class Device final : public synthLib::Device
    {
    public:
        explicit Device(const synthLib::DeviceCreateParams& _params);
        ~Device() override;

        // ── synthLib::Device interface ────────────────────────────────────────
        float getSamplerate() const override;
        bool  setSamplerate(float _samplerate) override;
        void  getSupportedSamplerates(std::vector<float>& _dst) const override;
        bool  isValid() const override { return true; }

        uint32_t getChannelCountIn()  override { return 0; }
        uint32_t getChannelCountOut() override { return 2; }

        bool setDspClockPercent(uint32_t) override { return true; }
        uint32_t getDspClockPercent() const override { return 100; }
        uint64_t getDspClockHz() const override { return 0; }

#if SYNTHLIB_DEMO_MODE == 0
        bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
        bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
#endif

        // ── Sound file loading ────────────────────────────────────────────────
        // Load an SFZ, SF2, ZBP/ZBB, or WAV file as the current sound.
        // Returns true on success. Thread-safe (locks internally).
        bool loadSoundFile(const std::string& filePath);

        // ── Preset/program support (SF2 / ZBP multi-preset files) ─────────────
        int  getPresetCount() const;
        std::string getPresetName(int index) const;
        bool selectPreset(int index);
        int  getSelectedPreset() const;

        // Currently loaded file path
        const std::string& getLoadedFilePath() const { return m_filePath; }

    protected:
        void processAudio(const synthLib::TAudioInputs& _inputs,
                          const synthLib::TAudioOutputs& _outputs,
                          size_t _samples) override;

        bool sendMidi(const synthLib::SMidiEvent& _ev,
                      std::vector<synthLib::SMidiEvent>& _response) override;

        void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override {}

    private:
        float m_samplerate = 44100.0f;

        std::unique_ptr<sfzero::Synth>   m_synth;
        std::unique_ptr<juce::AudioFormatManager> m_formatManager;

        // Mutex guards sound loading vs audio processing
        std::mutex m_lock;

        std::string m_filePath;

        // For multi-preset files (SF2, ZBP)
        int m_presetCount    = 0;
        int m_selectedPreset = 0;
    };
}
