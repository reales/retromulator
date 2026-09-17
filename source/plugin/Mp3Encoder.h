#pragma once

#include <string>

namespace retromulator
{
    // Encodes a finished WAV into MP3 CBR with libmp3lame, compiled in. Unlike JUCE's
    // LAMEEncoderAudioFormat, which shells out to an installed `lame` binary, this runs
    // in-process, so nothing has to be installed or bundled alongside the plugin.
    // Returns false and leaves no file behind if the encode fails.
    bool encodeWavToMp3(const std::string& wavPath, const std::string& mp3Path, int bitRateKbps);
}
