/**
 * Ayumi core — YM2149Synth voice engine (port).
 *
 * Faithful re-implementation of Ym2149Synth's SynthVoice + SynthSoftEnvelope
 * (c) 2016 Timothy Lamb, GPLv3. The real YM2149 hardware writes are abstracted
 * behind IYmSink so the host device can route them into the Ayumi emulator.
 *
 * Timing model (from the firmware): updateEvents() runs at 1 kHz, updateSoftsynth()
 * runs at 22050 Hz. The host drives both ticks off the audio clock.
 */
#pragma once
#include <cstdint>

namespace ayumiLib
{

// Abstract YM register sink — mirrors the firmware YM2149 class surface used by
// the voice engine. voice indices: 0=A,1=B,2=C, 3=Noise, 4=Envelope.
class IYmSink
{
public:
    virtual ~IYmSink() = default;
    virtual void setTone(uint8_t voice, uint16_t value) = 0;      // period in native YM units
    virtual void setVolume(uint8_t voice, uint8_t value) = 0;     // 0..15
    virtual void setNoise(uint8_t voice, uint8_t value) = 0;      // 0=voice,1=noise,2=mix
    virtual void setEnv(uint8_t voice, uint8_t value) = 0;        // 0=off,1=on
    virtual void setEnvShape(uint8_t cont, uint8_t attack, uint8_t alt, uint8_t hold) = 0;
    virtual void setNote(uint8_t voice, float value) = 0;         // note-number setter (mute helper)
};

// ── Soft envelope (volume / pitch ramp) ─────────────────────────────────────
class SynthSoftEnvelope
{
public:
    void begin();
    bool update();
    uint16_t read();
    void setShape(uint8_t v);
    void setRange(uint16_t mn, uint16_t mx);
    void setLookupTable(const uint8_t* t, uint8_t size);
    uint8_t getShape();
    void reset();

private:
    const uint8_t* lookupTable = nullptr;
    uint8_t  lookupSize  = 0;
    uint16_t phase       = 0;
    uint16_t size        = 0;
    int16_t  tick        = 0;
    uint8_t  increment   = 0;
    uint8_t  phaseLength = 255;
    uint8_t  shape       = 0;
    uint16_t min         = 0;
    uint16_t max         = 0;
    uint16_t value       = 0;
};

// ── One synth voice (monophonic, maps to one YM channel) ────────────────────
class SynthVoice
{
public:
    SynthSoftEnvelope volumeEnvelope;
    SynthSoftEnvelope pitchEnvelope;
    bool playing = false;

    void begin(IYmSink* ym, uint8_t channel);   // channel = YM channel 0..2
    void updateSoftsynth();                      // 22050 Hz tick
    void updateEvents();                         // 1 kHz tick
    void playNote(uint8_t n, uint8_t v);         // v==0 → note off (if note matches)

    void setVolumeEnvShape(uint8_t v);
    void setPitchEnvAmount(uint8_t v);
    void setPitchEnvShape(uint8_t v);
    void setVibratoAmount(uint8_t v);
    void setVibratoFreq(uint8_t v);
    void setNoiseDelay(uint8_t v);
    void setTranspose(uint8_t v);
    void setPitchbend(int v);
    void setGlide(uint8_t v);
    void setPwmFreq(uint8_t v);
    void setSoftDetune(uint8_t v);
    void setSynthType(uint8_t v);

    uint8_t getPlayingNote() const { return static_cast<uint8_t>(note); }

private:
    IYmSink* Ym    = nullptr;
    uint8_t  synth = 0;   // YM channel index 0..2

    uint8_t synthType = 255;

    bool enableVoice       = false;
    bool enableSoftsynth   = false;
    bool enableSoftDetune  = false;
    bool enableNoise       = false;
    bool enableEnv         = false;
    bool voicePitchModOnly = false;

    uint8_t envType = 0;

    uint32_t softPhase     = 0;
    uint16_t softIncrement = 0;
    uint32_t softWidth     = 0;
    uint8_t  softWavPos    = 0;

    int volume = 0;

    uint16_t note            = 0;
    uint8_t  velocity        = 0;
    uint16_t noteFreq        = 0;
    uint16_t currentNoteFreq = 0;
    uint16_t lastNoteFreq    = 0;

    int transpose = 0;
    int bendWheel = 0;

    uint16_t pitchEnvAmount = 0;
    uint16_t softFreqDetune = 0;
    uint16_t pwmFreq        = 0;

    bool     glideActive    = false;
    uint16_t glide          = 0;
    uint16_t glideIncrement = 0;
    uint32_t glidePhase     = 0;

    int vibratoAmount    = 0;
    int vibratoPhase     = 0;
    int vibratoIncrement = 0;

    uint8_t  noiseDelay          = 0;
    bool     noiseDelayTriggered = false;
    uint16_t noiseDelayIncrement = 0;
    uint16_t noiseDelayPhase     = 0;
};

} // namespace ayumiLib
