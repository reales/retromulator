/**
 * Ayumi core — YM2149Synth voice engine (port of SynthVoice.cpp / SynthSoftEnvelope.cpp).
 * Original: Ym2149Synth (c) 2016 Timothy Lamb, GPLv3.
 */
#include "synthVoice.h"
#include "synthTables.h"

#include <cmath>
#include <cstdlib>

namespace ayumiLib
{

#define map_int16(x, in_min, in_max, out_min, out_max) \
    (((int)(x) - (int)(in_min)) * ((int)(out_max) - (int)(out_min)) / ((int)(in_max) - (int)(in_min)) + (int)(out_min))

// Clamped note-table lookup. The firmware relied on flash over-read for out-of-range
// indices; clamp here to avoid UB without changing in-range tuning.
static inline uint16_t freqAt(int idx)
{
    if(idx < 0) idx = 0;
    if(idx >= kNoteTableSize) idx = kNoteTableSize - 1;
    return kFreqTable[idx];
}

// ── SynthSoftEnvelope ───────────────────────────────────────────────────────
void SynthSoftEnvelope::begin() { phaseLength = 255; }

bool SynthSoftEnvelope::update()
{
    uint16_t was = value;

    if(!shape) { value = max; return value != was; }

    if(value == static_cast<uint16_t>(-1))
    {
        value = 0;
        if(shape & 0x80) value = static_cast<uint16_t>(map_int16(value, 255, 0, min, max));
        else             value = static_cast<uint16_t>(map_int16(value, 0, 255, min, max));
        return true;
    }

    tick -= 1;
    if(tick > 0) return false;

    tick = static_cast<int16_t>(size);
    if(phase < 255) phase += increment;

    if(phase >= 255)
    {
        if(shape & 0x80) { value = min; return value != was; }
        else             { value = max; return value != was; }
    }

    if(lookupSize) value = lookupTable[phase];
    else           value = phase;

    if(shape & 0x80) value = static_cast<uint16_t>(map_int16(value, 255, 0, min, max));
    else             value = static_cast<uint16_t>(map_int16(value, 0, 255, min, max));

    return value != was;
}

uint16_t SynthSoftEnvelope::read() { return value; }
void SynthSoftEnvelope::setRange(uint16_t mn, uint16_t mx) { min = mn; max = mx; }
uint8_t SynthSoftEnvelope::getShape() { return shape; }

void SynthSoftEnvelope::setShape(uint8_t v)
{
    shape = static_cast<uint8_t>(v << 1);
    size = static_cast<uint16_t>((((static_cast<unsigned long>(shape & 0x7F)) << 5)) / 255);
    increment = 1;
    // Guard div-by-zero: shape==0 (env off) makes the denominator 0 — UB/SIGFPE on
    // x86. The original relied on AVR's non-trapping divide; increment is unused
    // while shape==0, so any value is fine.
    const int denom = (shape & 0x7F) << 5;
    if(size == 0 && denom != 0) increment = static_cast<uint8_t>(255 / denom);
}

void SynthSoftEnvelope::setLookupTable(const uint8_t* t, uint8_t sz) { lookupTable = t; lookupSize = sz; }
void SynthSoftEnvelope::reset() { tick = static_cast<int16_t>(size); phase = 0; value = static_cast<uint16_t>(-1); }

// ── SynthVoice ──────────────────────────────────────────────────────────────
void SynthVoice::begin(IYmSink* ym, uint8_t channel)
{
    Ym = ym;
    synth = channel;

    enableVoice = true;
    enableSoftsynth = true;

    Ym->setNoise(synth, 0);
    Ym->setEnv(synth, 0);
    volumeEnvelope.begin();
    pitchEnvelope.begin();

    volumeEnvelope.setShape(0x00);
    volumeEnvelope.setRange(0, 31);
    volumeEnvelope.setLookupTable(kVolumeEnvTable, 255);

    pitchEnvelope.setShape(0x00);
    pitchEnvelope.setRange(0, 0);

    synthType = 255;
    setSynthType(0);
}

void SynthVoice::updateSoftsynth()
{
    if(!enableSoftsynth || volume <= 0) return;

    softPhase += softIncrement;
    if(softPhase > 100000) softPhase -= 100000;
    // softPhase is unsigned; the original's < 0 guard is a no-op here.

    if(softPhase >= softWidth)
    {
        if(softWavPos) { softWavPos = 0; Ym->setVolume(synth, static_cast<uint8_t>(volume)); }
    }
    else
    {
        if(!softWavPos) { softWavPos = 1; Ym->setVolume(synth, 0); }
    }
}

void SynthVoice::updateEvents()
{
    uint16_t voiceF = static_cast<uint16_t>(currentNoteFreq + transpose);

    uint16_t envF = 0;
    uint16_t softF = 0;

    if(playing)
    {
        if(glideActive)
        {
            uint16_t destF = static_cast<uint16_t>(noteFreq + transpose);
            glidePhase += glideIncrement;
            if(destF > voiceF)
            {
                voiceF = static_cast<uint16_t>(voiceF + (glidePhase / 1000));
                if(voiceF >= destF)
                {
                    lastNoteFreq = static_cast<uint16_t>(-1);
                    voiceF = destF;
                    currentNoteFreq = noteFreq;
                    glideActive = false;
                }
            }
            else
            {
                voiceF = static_cast<uint16_t>(voiceF - (glidePhase / 1000));
                if(voiceF <= destF)
                {
                    lastNoteFreq = static_cast<uint16_t>(-1);
                    voiceF = destF;
                    currentNoteFreq = noteFreq;
                    glideActive = false;
                }
            }
        }

        if(bendWheel) voiceF = static_cast<uint16_t>(voiceF + bendWheel);

        envF = softF = voiceF;

        if(vibratoAmount && currentNoteFreq)
        {
            vibratoPhase += vibratoIncrement;
            if(vibratoPhase > 10000)  vibratoPhase -= 20000;
            if(vibratoPhase < -10000) vibratoPhase += 20000;
            voiceF = static_cast<uint16_t>(voiceF +
                map_int16(((std::abs(vibratoPhase) * 2) - 10000), -10000, 10000, vibratoAmount * -1, vibratoAmount) / 100);
        }

        if(pitchEnvAmount)
        {
            pitchEnvelope.update();
            uint16_t pitchEnvAmt = pitchEnvelope.read();
            voiceF = static_cast<uint16_t>(voiceF + pitchEnvAmt);
        }

        if(voiceF != lastNoteFreq)
        {
            lastNoteFreq = voiceF;

            if(!voicePitchModOnly) envF = softF = voiceF;

            if(enableSoftsynth)
            {
                uint16_t sf = softF;
                if(enableSoftDetune) sf = static_cast<uint16_t>(sf + pwmFreq + softFreqDetune);
                if(sf >= kNoteTableSize) sf = kNoteTableSize - 1;
                softIncrement = kSoftFreqTable[sf];
            }

            if(enableEnv)
            {
                if(voicePitchModOnly)
                {
                    // Acid on the pitch envelope
                    Ym->setTone(synth, freqAt(voiceF + (softFreqDetune >> 1)));
                    Ym->setTone(4, freqAt(envF + pwmFreq));
                }
                else
                {
                    Ym->setTone(4, freqAt(voiceF + pwmFreq));
                }
            }
            else if(enableVoice)
            {
                if(voicePitchModOnly)
                    Ym->setTone(synth, freqAt(voiceF + (softFreqDetune >> 1)));
                else
                    Ym->setTone(synth, freqAt(voiceF));
            }
            if(enableNoise)
                Ym->setTone(3, static_cast<uint16_t>(0x1F - ((static_cast<uint8_t>(voiceF / 10)) >> 2)));
        }

        if(volumeEnvelope.update())
        {
            volume = volumeEnvelope.read();
            if(volume == 0 && (volumeEnvelope.getShape() & 0x80)) playing = false;
            if(!enableSoftsynth) Ym->setVolume(synth, static_cast<uint8_t>(volume));
        }

        if(noiseDelay && !noiseDelayTriggered)
        {
            noiseDelayPhase += 1;
            if(noiseDelayPhase >= noiseDelayIncrement)
            {
                noiseDelayPhase = 0;
                noiseDelayTriggered = true;
                if(!enableNoise) Ym->setNoise(synth, 2);
                else             Ym->setNoise(synth, 0);
            }
        }
    }
}

void SynthVoice::playNote(uint8_t n, uint8_t v)
{
    if(v)
    {
        velocity = v;
        note     = n;
        noteFreq = static_cast<uint16_t>((static_cast<uint16_t>(n)) * 10);

        if(playing == false)
        {
            volumeEnvelope.reset();
            volumeEnvelope.setRange(0, static_cast<uint16_t>(v >> 3));
            currentNoteFreq = noteFreq;
            volume = -1;

            glidePhase = 0;
            glideActive = false;
        }
        else
        {
            if(glide)
            {
                if(glideActive) currentNoteFreq = lastNoteFreq;
                glideActive = true;
                glidePhase = 0;
                glideIncrement = static_cast<uint16_t>(
                    ((static_cast<uint32_t>(std::abs(noteFreq - currentNoteFreq))) * 10000) / glide);
                if(!glideIncrement) glideIncrement = 1;
            }
            else
            {
                glideIncrement = 0;
                currentNoteFreq = noteFreq;
                glidePhase = 0;
                glideActive = false;
            }
        }

        pitchEnvelope.reset();

        if(softIncrement > 1) softIncrement = 1;
        // softIncrement is unsigned; the original's < 0 clamp is a no-op here.

        if(!enableVoice) Ym->setNote(synth, 255);

        if(enableNoise)
        {
            Ym->setNoise(synth, 1);
        }
        else
        {
            noiseDelayTriggered = false;
            enableNoise = false;
            Ym->setNoise(synth, 0);
        }
        noiseDelayPhase = 0;

        if(enableEnv)
        {
            Ym->setEnv(synth, 1);
            Ym->setEnvShape(0, 0, 0, 0);
            if(envType == 1) Ym->setEnvShape(1, 0, 0, 0);
            else             Ym->setEnvShape(1, 0, 1, 0);
        }

        playing = true;
        lastNoteFreq = 0;
    }
    else if(n == note)
    {
        playing = false;
        volume = 0;
        if(enableEnv) Ym->setEnv(synth, 0);
        Ym->setVolume(synth, 0);
    }
}

void SynthVoice::setPitchbend(int v) { bendWheel = (v >> 2) / 100; }

void SynthVoice::setGlide(uint8_t v) { glide = v; glide <<= 6; }

void SynthVoice::setVolumeEnvShape(uint8_t v)
{
    if(v == 64) v = 65;
    volumeEnvelope.setShape(v);
}

void SynthVoice::setPitchEnvShape(uint8_t v) { pitchEnvelope.setShape(v); }

void SynthVoice::setPitchEnvAmount(uint8_t v)
{
    pitchEnvAmount = v;
    pitchEnvAmount *= 10;
    pitchEnvelope.setRange(0, pitchEnvAmount);
}

void SynthVoice::setVibratoAmount(uint8_t v) { vibratoAmount = v * 100; }

void SynthVoice::setVibratoFreq(uint8_t v)
{
    unsigned int ms = static_cast<unsigned int>((std::pow(8000.0f, 1.0f - (static_cast<float>(v)) / 127.0f)) + 5);
    vibratoIncrement = static_cast<int>((1.0f / ms) * 10000);
}

void SynthVoice::setPwmFreq(uint8_t v)
{
    pwmFreq = v;
    if(synthType == 6)
        softWidth = static_cast<uint32_t>((((float)(pwmFreq) / 255) + 0.5f) * 100000);
    if(playing) lastNoteFreq = 0;
}

void SynthVoice::setSoftDetune(uint8_t v)
{
    softFreqDetune = v;
    softFreqDetune *= 10;
    if(playing) lastNoteFreq = 0;
}

void SynthVoice::setNoiseDelay(uint8_t v)
{
    noiseDelay = v;
    v = static_cast<uint8_t>(127 - v);
    noiseDelayIncrement = static_cast<uint16_t>(v << 1);
}

void SynthVoice::setTranspose(uint8_t v)
{
    if(!v) v = 64;
    transpose = ((static_cast<int>(v)) - 64) * 10;
}

void SynthVoice::setSynthType(uint8_t v)
{
    if(v == synthType) return;
    synthType = v;

    if(enableNoise) Ym->setNoise(synth, 0);
    if(enableEnv)   Ym->setEnv(synth, 0);

    enableVoice = false;
    enableSoftsynth = false;
    enableEnv = false;
    envType = 0;
    enableNoise = false;
    enableSoftDetune = false;
    voicePitchModOnly = false;

    switch(synthType)
    {
        case 0:
            enableVoice = true;
            break;
        case 1:
            voicePitchModOnly = true; enableVoice = true; enableEnv = true; envType = 1;
            break;
        case 2:
            voicePitchModOnly = true; enableVoice = true; enableEnv = true; envType = 2;
            break;
        case 3:
            enableVoice = false; enableEnv = true; envType = 2;
            break;
        case 4:
            enableVoice = false; enableEnv = true; envType = 1;
            break;
        case 5:
            enableVoice = true; enableSoftsynth = true; enableSoftDetune = true; softWidth = 50000;
            break;
        case 6:
            enableVoice = true; enableSoftsynth = true; voicePitchModOnly = true; enableSoftDetune = false;
            softWidth = static_cast<uint32_t>((((float)(pwmFreq) / 255) + 0.5f) * 100000);
            break;
        case 7:
            enableNoise = true;
            break;
        default:
            enableVoice = true;
            break;
    }

    if(playing)
    {
        lastNoteFreq = 0;
        playing = false;
        playNote(static_cast<uint8_t>(note), velocity);
    }
}

} // namespace ayumiLib
