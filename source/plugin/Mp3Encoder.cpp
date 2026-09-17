#include "Mp3Encoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

#include "lame.h"

namespace retromulator
{
    bool encodeWavToMp3(const std::string& wavPath, const std::string& mp3Path, const int bitRateKbps)
    {
        const juce::File wavFile{juce::String(wavPath)};
        const juce::File mp3File{juce::String(mp3Path)};

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader(
            wav.createReaderFor(new juce::FileInputStream(wavFile), true));
        if(!reader || reader->numChannels == 0)
            return false;

        const auto numChannels = static_cast<int>(reader->numChannels);
        const auto sampleRate  = static_cast<int>(reader->sampleRate);

        struct LameDeleter { void operator()(lame_global_flags* f) const { if(f) lame_close(f); } };
        std::unique_ptr<lame_global_flags, LameDeleter> gf(lame_init());
        if(!gf)
            return false;

        lame_set_in_samplerate (gf.get(), sampleRate);
        lame_set_out_samplerate(gf.get(), sampleRate);
        lame_set_num_channels  (gf.get(), numChannels);
        lame_set_mode          (gf.get(), numChannels == 1 ? MONO : JOINT_STEREO);
        // CBR at the requested rate: vbr_off is what keeps 320 actually 320.
        lame_set_VBR           (gf.get(), vbr_off);
        lame_set_brate         (gf.get(), bitRateKbps);
        // 2 is LAME's "high quality" setting; 0 is slower for no audible gain here.
        lame_set_quality       (gf.get(), 2);

        if(lame_init_params(gf.get()) < 0)
            return false;

        mp3File.deleteFile();
        std::unique_ptr<juce::FileOutputStream> out(mp3File.createOutputStream());
        if(!out || !out->openedOk())
            return false;

        constexpr int kBlock = 4096;
        juce::AudioBuffer<float> buffer(numChannels, kBlock);
        // LAME's documented worst case is 1.25 * samples + 7200.
        std::vector<unsigned char> mp3Buffer(static_cast<size_t>(1.25 * kBlock) + 7200);

        auto remaining = reader->lengthInSamples;
        juce::int64 position = 0;

        while(remaining > 0)
        {
            const auto count = static_cast<int>(std::min<juce::int64>(remaining, kBlock));
            buffer.clear();
            if(!reader->read(&buffer, 0, count, position, true, numChannels > 1))
                return false;

            const float* const left  = buffer.getReadPointer(0);
            const float* const right = numChannels > 1 ? buffer.getReadPointer(1) : left;

            // The _ieee_float entry point expects full-scale floats, which is what the
            // reader already hands back, so nothing needs rescaling.
            const int written = lame_encode_buffer_ieee_float(
                gf.get(), left, right, count, mp3Buffer.data(),
                static_cast<int>(mp3Buffer.size()));
            if(written < 0)
                return false;
            if(written > 0 && !out->write(mp3Buffer.data(), static_cast<size_t>(written)))
                return false;

            position  += count;
            remaining -= count;
        }

        const int flushed = lame_encode_flush(gf.get(), mp3Buffer.data(),
                                              static_cast<int>(mp3Buffer.size()));
        if(flushed > 0 && !out->write(mp3Buffer.data(), static_cast<size_t>(flushed)))
            return false;

        out->flush();
        const bool ok = mp3File.getSize() > 0;
        if(!ok)
            mp3File.deleteFile();
        return ok;
    }
}
