#include "device.h"

#include "../../Modules/SFZero/SFZero.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cstring>

namespace akaiLib
{
    static constexpr int kNumVoices = 64;

    Device::Device(const synthLib::DeviceCreateParams& _params)
        : synthLib::Device(_params)
        , m_synth(std::make_unique<sfzero::Synth>())
        , m_formatManager(std::make_unique<juce::AudioFormatManager>())
    {
        m_formatManager->registerBasicFormats();

        for(int i = 0; i < kNumVoices; ++i)
            m_synth->addVoice(new sfzero::Voice());
    }

    Device::~Device() = default;

    float Device::getSamplerate() const
    {
        return m_samplerate;
    }

    bool Device::setSamplerate(float _samplerate)
    {
        m_samplerate = _samplerate;
        if(m_synth)
            m_synth->setCurrentPlaybackSampleRate(static_cast<double>(_samplerate));
        return true;
    }

    void Device::getSupportedSamplerates(std::vector<float>& _dst) const
    {
        _dst.push_back(44100.0f);
        _dst.push_back(48000.0f);
        _dst.push_back(88200.0f);
        _dst.push_back(96000.0f);
    }

#if SYNTHLIB_DEMO_MODE == 0
    bool Device::getState(std::vector<uint8_t>& /*_state*/, synthLib::StateType /*_type*/)
    {
        return false;
    }

    bool Device::setState(const std::vector<uint8_t>& /*_state*/, synthLib::StateType /*_type*/)
    {
        return false;
    }
#endif

    // ── Sound file loading ────────────────────────────────────────────────────

    bool Device::loadSoundFile(const std::string& filePath)
    {
        if(filePath.empty())
            return false;

        const juce::File file(filePath);
        if(!file.existsAsFile())
            return false;

        const auto ext = file.getFileExtension().toLowerCase();

        sfzero::Sound* sound = nullptr;

        if(ext == ".sf2")
            sound = new sfzero::SF2Sound(file);
        else if(ext == ".zbp" || ext == ".zbb")
            sound = new sfzero::ZBPSound(file);
        else if(ext == ".sfz" || ext == ".wav" || ext == ".aif" || ext == ".aiff" ||
                ext == ".flac" || ext == ".ogg")
        {
            // For WAV/AIFF/FLAC/OGG: create a minimal SFZ in memory that maps
            // the sample across the full key range.
            if(ext != ".sfz")
            {
                auto* s = new sfzero::Sound(file);
                // Build a single-region SFZ text that maps the file
                const juce::String sfzText =
                    "<group>\n<region> sample=" + file.getFileName() + " lokey=0 hikey=127\n";
                s->loadRegionsFromText(sfzText.toRawUTF8(),
                                       static_cast<unsigned int>(sfzText.length()));
                sound = s;
            }
            else
            {
                sound = new sfzero::Sound(file);
            }
        }
        else
            return false;

        // Only call loadRegions() if regions weren't already loaded from
        // inline SFZ text (e.g. for .wav/.aif/.flac/.ogg files).
        if(sound->getRegions().size() == 0)
            sound->loadRegions();
        sound->loadSamples(m_formatManager.get());

        const auto& errors = sound->getErrors();
        if(errors.size() > 0)
        {
            fprintf(stderr, "[Akai S1000] Sound load errors:\n");
            for(const auto& e : errors)
                fprintf(stderr, "  %s\n", e.toRawUTF8());
        }

        // Determine preset count
        const int presets = sound->numSubsounds();

        {
            std::lock_guard<std::mutex> lock(m_lock);

            m_synth->clearSounds();
            m_synth->addSound(sound);
            m_synth->setCurrentPlaybackSampleRate(static_cast<double>(m_samplerate));

            m_filePath      = filePath;
            m_presetCount   = (presets > 1) ? presets : 0;
            m_selectedPreset = 0;

            if(presets > 1)
                sound->useSubsound(0);
        }

        return true;
    }

    // ── Preset support ────────────────────────────────────────────────────────

    int Device::getPresetCount() const
    {
        return m_presetCount;
    }

    std::string Device::getPresetName(int index) const
    {
        if(index < 0 || index >= m_presetCount)
            return {};

        if(m_synth->getNumSounds() == 0)
            return {};

        auto* sound = dynamic_cast<sfzero::Sound*>(m_synth->getSound(0).get());
        if(!sound)
            return {};

        return sound->subsoundName(index).toStdString();
    }

    bool Device::selectPreset(int index)
    {
        if(index < 0 || index >= m_presetCount)
            return false;

        if(m_synth->getNumSounds() == 0)
            return false;

        auto* sound = dynamic_cast<sfzero::Sound*>(m_synth->getSound(0).get());
        if(!sound)
            return false;

        std::lock_guard<std::mutex> lock(m_lock);
        sound->useSubsound(index);
        m_selectedPreset = index;
        return true;
    }

    int Device::getSelectedPreset() const
    {
        return m_selectedPreset;
    }

    // ── Auto-slice ───────────────────────────────────────────────────────────

    bool Device::isSliceable() const
    {
        if(m_filePath.empty() || m_presetCount > 0)
            return false;

        const juce::File f(m_filePath);
        const auto ext = f.getFileExtension().toLowerCase();
        return (ext == ".wav" || ext == ".aif" || ext == ".aiff" ||
                ext == ".flac" || ext == ".ogg");
    }

    bool Device::autoSlice(int numSlices)
    {
        if(numSlices < 1 || m_filePath.empty())
            return false;

        const juce::File file(m_filePath);
        if(!file.existsAsFile())
            return false;

        // We need the sample length — load the file via format manager to query it.
        std::unique_ptr<juce::AudioFormatReader> reader(
            m_formatManager->createReaderFor(file));
        if(!reader)
            return false;

        const juce::int64 totalFrames = static_cast<juce::int64>(reader->lengthInSamples);
        reader.reset(); // close the reader before reloading

        if(totalFrames <= 0)
            return false;

        const juce::int64 sliceLen = totalFrames / numSlices;
        if(sliceLen <= 0)
            return false;

        // Build SFZ text with one region per slice, mapped to consecutive keys from C4 (60)
        juce::String sfzText = "<group>\n";
        for(int i = 0; i < numSlices; ++i)
        {
            const int key = 60 + i;
            if(key > 127) break;

            const juce::int64 sliceStart = i * sliceLen;
            const juce::int64 sliceEnd   = (i == numSlices - 1)
                                           ? (totalFrames - 1)
                                           : ((i + 1) * sliceLen - 1);

            sfzText += "<region> sample=" + file.getFileName()
                     + " lokey=" + juce::String(key)
                     + " hikey=" + juce::String(key)
                     + " pitch_keycenter=" + juce::String(key)
                     + " offset=" + juce::String(sliceStart)
                     + " end=" + juce::String(sliceEnd)
                     + "\n";
        }

        auto* sound = new sfzero::Sound(file);
        sound->loadRegionsFromText(sfzText.toRawUTF8(),
                                   static_cast<unsigned int>(sfzText.length()));
        sound->loadSamples(m_formatManager.get());

        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_synth->clearSounds();
            m_synth->addSound(sound);
            m_synth->setCurrentPlaybackSampleRate(static_cast<double>(m_samplerate));
            m_presetCount   = 0;
            m_selectedPreset = 0;
        }

        return true;
    }

    // ── Audio processing ──────────────────────────────────────────────────────

    void Device::processAudio(const synthLib::TAudioInputs& /*_inputs*/,
                              const synthLib::TAudioOutputs& _outputs,
                              size_t _samples)
    {
        if(_samples == 0)
            return;

        const int numSamples = static_cast<int>(_samples);

        // Render into a temporary stereo buffer
        juce::AudioSampleBuffer buffer(2, numSamples);
        buffer.clear();

        {
            std::lock_guard<std::mutex> lock(m_lock);
            juce::MidiBuffer emptyMidi;
            m_synth->renderNextBlock(buffer, emptyMidi, 0, numSamples);
        }

        // Copy to output
        if(_outputs[0])
            std::memcpy(_outputs[0], buffer.getReadPointer(0), _samples * sizeof(float));
        if(_outputs[1])
            std::memcpy(_outputs[1], buffer.getReadPointer(1), _samples * sizeof(float));
    }

    // ── MIDI handling ─────────────────────────────────────────────────────────

    bool Device::sendMidi(const synthLib::SMidiEvent& _ev,
                          std::vector<synthLib::SMidiEvent>& /*_response*/)
    {
        if(!_ev.sysex.empty())
            return false; // no sysex support for sample player

        const uint8_t status  = _ev.a & 0xf0;
        const int channel     = (_ev.a & 0x0f) + 1;

        switch(status)
        {
        case synthLib::M_NOTEON:
            if(_ev.c > 0)
                m_synth->noteOn(channel, _ev.b, _ev.c / 127.0f);
            else
                m_synth->noteOff(channel, _ev.b, 0.0f, true);
            return true;

        case synthLib::M_NOTEOFF:
            m_synth->noteOff(channel, _ev.b, _ev.c / 127.0f, true);
            return true;

        case synthLib::M_PITCHBEND:
            m_synth->handlePitchWheel(channel, (_ev.c << 7) | _ev.b);
            return true;

        case synthLib::M_CONTROLCHANGE:
            m_synth->handleController(channel, _ev.b, _ev.c);
            return true;

        case synthLib::M_PROGRAMCHANGE:
            if(m_presetCount > 0)
                selectPreset(_ev.b);
            return true;

        case synthLib::M_AFTERTOUCH:
            m_synth->handleChannelPressure(channel, _ev.b);
            return true;

        case synthLib::M_POLYPRESSURE:
            m_synth->handleAftertouch(channel, _ev.b, _ev.c);
            return true;

        default:
            return false;
        }
    }
}
