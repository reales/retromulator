/**
 * Ayumi Device implementation.
 * Ayumi (c) Peter Sovietov (MIT). Voice engine ported from Ym2149Synth (c) 2016
 * Timothy Lamb (GPLv3).
 */
#include "device.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace ayumiLib
{

// YM register indices (match the firmware YM2149 class).
enum {
    REG_A_FREQ = 0x00, REG_B_FREQ = 0x02, REG_C_FREQ = 0x04,
    REG_NOISE_FREQ = 0x06, REG_MIXER = 0x07,
    REG_A_LEVEL = 0x08, REG_B_LEVEL = 0x09, REG_C_LEVEL = 0x0A,
    REG_ENV_FREQ = 0x0B, REG_ENV_SHAPE = 0x0D
};

// Signature of the .ay patch file.
static const char kAyMagic[4] = { 'A', 'Y', 'P', '1' };

// ── Construction ────────────────────────────────────────────────────────────
Device::Device(const synthLib::DeviceCreateParams& params)
    : synthLib::Device(params)
{
    // Firmware default mixer: B00111000 = tone A/B/C on, noise off on all.
    m_ymRegs[REG_MIXER] = 0x38;

    configureAyumi();

    for(int i = 0; i < kNumVoices; ++i)
        m_voices[i].begin(this, static_cast<uint8_t>(i));

    // Firmware defaults after begin(): synth type 0, no modulation.
    m_cc = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64 }; // CC11 transpose center
    for(uint8_t cc = 1; cc <= 11; ++cc)
        applyCC(cc, m_cc[cc - 1]);
}

Device::~Device()
{
    m_shutdown.store(true);
}

void Device::configureAyumi()
{
    ayumi_configure(&m_ay, m_isYM ? 1 : 0, kYmClockHz, static_cast<int>(m_sampleRate));
    ayumi_set_pan(&m_ay, 0, 0.5, 0);
    ayumi_set_pan(&m_ay, 1, 0.5, 0);
    ayumi_set_pan(&m_ay, 2, 0.5, 0);
}

void Device::setEngine(int isAY)
{
    // Message-thread call: only latch the request. processAudio applies it so the
    // ayumi_configure memset never races with ayumi_process on the audio thread.
    m_pendingIsYM.store(isAY == 0);
}

void Device::applyEngineIfPending()
{
    const bool ym = m_pendingIsYM.load();
    if(ym == m_isYM) return;
    m_isYM = ym;
    // Reconfigure preserves period/volume registers by re-pushing them.
    configureAyumi();
    for(uint8_t r = 0; r <= REG_ENV_SHAPE; ++r)
        ymWrite(r, m_ymRegs[r]);
}

// ── YM register model → Ayumi ───────────────────────────────────────────────
void Device::ymWrite(uint8_t reg, uint8_t value)
{
    if(reg > REG_ENV_SHAPE) return;
    m_ymRegs[reg] = value;

    switch(reg)
    {
    case REG_A_FREQ: case REG_A_FREQ + 1:
        ayumi_set_tone(&m_ay, 0, (m_ymRegs[REG_A_FREQ + 1] << 8) | m_ymRegs[REG_A_FREQ]);
        break;
    case REG_B_FREQ: case REG_B_FREQ + 1:
        ayumi_set_tone(&m_ay, 1, (m_ymRegs[REG_B_FREQ + 1] << 8) | m_ymRegs[REG_B_FREQ]);
        break;
    case REG_C_FREQ: case REG_C_FREQ + 1:
        ayumi_set_tone(&m_ay, 2, (m_ymRegs[REG_C_FREQ + 1] << 8) | m_ymRegs[REG_C_FREQ]);
        break;
    case REG_NOISE_FREQ:
        ayumi_set_noise(&m_ay, m_ymRegs[REG_NOISE_FREQ]);
        break;
    case REG_MIXER:
        pushMixer();
        break;
    case REG_A_LEVEL:
        ayumi_set_volume(&m_ay, 0, m_ymRegs[REG_A_LEVEL] & 0x0F);
        pushMixer();
        break;
    case REG_B_LEVEL:
        ayumi_set_volume(&m_ay, 1, m_ymRegs[REG_B_LEVEL] & 0x0F);
        pushMixer();
        break;
    case REG_C_LEVEL:
        ayumi_set_volume(&m_ay, 2, m_ymRegs[REG_C_LEVEL] & 0x0F);
        pushMixer();
        break;
    case REG_ENV_FREQ: case REG_ENV_FREQ + 1:
        ayumi_set_envelope(&m_ay, (m_ymRegs[REG_ENV_FREQ + 1] << 8) | m_ymRegs[REG_ENV_FREQ]);
        break;
    case REG_ENV_SHAPE:
        ayumi_set_envelope_shape(&m_ay, m_ymRegs[REG_ENV_SHAPE] & 0x0F);
        break;
    default:
        break;
    }
}

void Device::pushMixer()
{
    const uint8_t mix = m_ymRegs[REG_MIXER];
    // Mixer bits: 0-2 = tone disable A/B/C, 3-5 = noise disable A/B/C (1 = off).
    ayumi_set_mixer(&m_ay, 0, (mix >> 0) & 1, (mix >> 3) & 1, (m_ymRegs[REG_A_LEVEL] >> 4) & 1);
    ayumi_set_mixer(&m_ay, 1, (mix >> 1) & 1, (mix >> 4) & 1, (m_ymRegs[REG_B_LEVEL] >> 4) & 1);
    ayumi_set_mixer(&m_ay, 2, (mix >> 2) & 1, (mix >> 5) & 1, (m_ymRegs[REG_C_LEVEL] >> 4) & 1);
}

// ── IYmSink (mirrors firmware YM2149 register logic) ────────────────────────
void Device::setTone(uint8_t voice, uint16_t value)
{
    switch(voice)
    {
    case 0: ymWrite(REG_A_FREQ, value & 0xFF); ymWrite(REG_A_FREQ + 1, (value >> 8) & 0x0F); break;
    case 1: ymWrite(REG_B_FREQ, value & 0xFF); ymWrite(REG_B_FREQ + 1, (value >> 8) & 0x0F); break;
    case 2: ymWrite(REG_C_FREQ, value & 0xFF); ymWrite(REG_C_FREQ + 1, (value >> 8) & 0x0F); break;
    case 3: ymWrite(REG_NOISE_FREQ, value & 0x1F); break;
    case 4:
        value >>= 4;
        ymWrite(REG_ENV_FREQ, value & 0xFF); ymWrite(REG_ENV_FREQ + 1, (value >> 8) & 0x0F);
        break;
    default: break;
    }
}

void Device::setVolume(uint8_t voice, uint8_t value)
{
    if(voice > 2) return;
    uint8_t reg = static_cast<uint8_t>(REG_A_LEVEL + voice);
    value &= 0x0F;
    uint8_t v = static_cast<uint8_t>((m_ymRegs[reg] & 0x10) | value);
    ymWrite(reg, v);
}

void Device::setNoise(uint8_t voice, uint8_t value)
{
    if(voice > 2) return;
    // Firmware mapping: 1→enable noise, 2→disable both tone+noise, 0→tone only.
    uint8_t bits;
    switch(value)
    {
    case 1:  bits = 0b00000001; break;
    case 2:  bits = 0b00000000; break;
    default: bits = 0b00001000; break;
    }
    bits <<= voice;

    uint8_t mix = m_ymRegs[REG_MIXER];
    switch(voice)
    {
    case 0:  mix = static_cast<uint8_t>((mix & 0b11110110) | bits); break;
    case 1:  mix = static_cast<uint8_t>((mix & 0b11101101) | bits); break;
    default: mix = static_cast<uint8_t>((mix & 0b11011011) | bits); break;
    }
    ymWrite(REG_MIXER, mix);
}

void Device::setEnv(uint8_t voice, uint8_t value)
{
    if(voice > 2) return;
    uint8_t reg = static_cast<uint8_t>(REG_A_LEVEL + voice);
    value &= 0x01;
    value <<= 4;
    uint8_t v = static_cast<uint8_t>((m_ymRegs[reg] & 0x0F) | value);
    ymWrite(reg, v);
}

void Device::setEnvShape(uint8_t cont, uint8_t attack, uint8_t alt, uint8_t hold)
{
    cont &= 1; attack &= 1; alt &= 1; hold &= 1;
    ymWrite(REG_ENV_SHAPE, static_cast<uint8_t>((cont << 3) | (attack << 2) | (alt << 1) | hold));
}

void Device::setNote(uint8_t voice, float value)
{
    // Firmware helper used only to silence a disabled voice (value 255).
    if(voice > 2) return;
    uint16_t f;
    if(value > 127.0f) f = 0;
    else f = static_cast<uint16_t>((2000000.0f / ((std::pow(2.0f, ((value - 69.0f) / 12.0f)) * 440.0f))) / 16.0f);
    setTone(voice, f);
}

// ── CC dispatch (global to all voices) ──────────────────────────────────────
void Device::applyCC(uint8_t cc, uint8_t value)
{
    if(cc < 1 || cc > 11) return;
    m_cc[cc - 1] = value;

    for(auto& v : m_voices)
    {
        switch(cc)
        {
        case 1:  v.setPwmFreq(value);       break;
        case 2:  v.setSoftDetune(value);    break;
        case 3:  v.setSynthType(value);     break;
        case 4:  v.setVolumeEnvShape(value);break;
        case 5:  v.setGlide(value);         break;
        case 6:  v.setVibratoFreq(value);   break;
        case 7:  v.setVibratoAmount(value); break;
        case 8:  v.setNoiseDelay(value);    break;
        case 9:  v.setPitchEnvAmount(value);break;
        case 10: v.setPitchEnvShape(value); break;
        case 11: v.setTranspose(value);     break;
        default: break;
        }
    }
}

// ── Polyphony ───────────────────────────────────────────────────────────────
int Device::allocVoice(uint8_t note)
{
    for(int i = 0; i < kNumVoices; ++i)
        if(!m_slots[i].active) return i;

    // Steal the oldest active voice.
    int best = 0; uint64_t bestAge = UINT64_MAX;
    for(int i = 0; i < kNumVoices; ++i)
        if(m_slots[i].age < bestAge) { bestAge = m_slots[i].age; best = i; }
    return best;
}

void Device::onNoteOn(uint8_t note, uint8_t vel)
{
    if(vel == 0) { onNoteOff(note); return; }

    const int idx = allocVoice(note);
    m_slots[idx].active = true;
    m_slots[idx].note   = note;
    m_slots[idx].age    = ++m_ageCounter;
    m_voices[idx].playNote(note, vel);
}

void Device::onNoteOff(uint8_t note)
{
    for(int i = 0; i < kNumVoices; ++i)
    {
        if(m_slots[i].active && m_slots[i].note == note)
        {
            m_voices[i].playNote(note, 0);
            m_slots[i].active = false;
            break;
        }
    }
}

void Device::onAllNotesOff()
{
    for(int i = 0; i < kNumVoices; ++i)
    {
        if(m_slots[i].active)
            m_voices[i].playNote(m_slots[i].note, 0);
        m_slots[i].active = false;
    }
}

void Device::onPitchBend(int value14)
{
    const int v = value14 - 0x2000;
    for(auto& voice : m_voices)
        voice.setPitchbend(v);
}

// ── sendMidi ────────────────────────────────────────────────────────────────
bool Device::sendMidi(const synthLib::SMidiEvent& ev, std::vector<synthLib::SMidiEvent>&)
{
    if(!ev.sysex.empty()) return true;
    if(ev.a >= 0xF8)      return true;

    const uint8_t status = ev.a & 0xF0;
    switch(status)
    {
    case 0x80: onNoteOff(ev.b); break;
    case 0x90: onNoteOn(ev.b, ev.c); break;
    case 0xB0:
        if(ev.b >= 1 && ev.b <= 11) applyCC(ev.b, ev.c);
        else if(ev.b == 120 || ev.b == 123) onAllNotesOff();
        break;
    case 0xC0: // Program Change → latch index; processor loads it off the audio thread
        m_pendingProgram.store(static_cast<int>(ev.b));
        break;
    case 0xE0:
        onPitchBend((static_cast<int>(ev.c) << 7) | static_cast<int>(ev.b));
        break;
    default: break;
    }
    return true;
}

void Device::readMidiOut(std::vector<synthLib::SMidiEvent>&) {}

// ── processAudio ────────────────────────────────────────────────────────────
void Device::processAudio(const synthLib::TAudioInputs&,
                          const synthLib::TAudioOutputs& outputs, size_t samples)
{
    if(m_shutdown.load()) return;

    applyEngineIfPending();

    auto* outL = outputs[0];
    auto* outR = outputs[1];

    const double eventStep = kEventHz / m_sampleRate; // events per output sample
    const double softStep  = kSoftHz  / m_sampleRate;

    for(size_t i = 0; i < samples; ++i)
    {
        // Soft-PWM tick(s) at 22050 Hz.
        m_softAcc += softStep;
        while(m_softAcc >= 1.0)
        {
            m_softAcc -= 1.0;
            for(auto& v : m_voices) v.updateSoftsynth();
        }

        // Event tick(s) at 1 kHz.
        m_eventAcc += eventStep;
        while(m_eventAcc >= 1.0)
        {
            m_eventAcc -= 1.0;
            for(auto& v : m_voices) v.updateEvents();
        }

        ayumi_process(&m_ay);
        ayumi_remove_dc(&m_ay);

        outL[i] = static_cast<float>(m_ay.left);
        outR[i] = static_cast<float>(m_ay.right);
    }
}

// ── .ay patch loading ───────────────────────────────────────────────────────
bool Device::loadPatchFile(const std::string& filePath)
{
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if(!f.is_open()) return false;

    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));

    // Format: 4-byte magic "AYP1", 11 CC bytes, optional name.
    if(sz < 4 + 11) return false;
    if(std::memcmp(buf.data(), kAyMagic, 4) != 0) return false;

    for(int i = 0; i < 11; ++i)
        m_patch.cc[i] = buf[4 + i];

    m_patch.name.clear();
    if(sz > 4 + 11)
        m_patch.name.assign(reinterpret_cast<const char*>(buf.data() + 4 + 11), sz - (4 + 11));
    m_patchName = m_patch.name;

    // Apply. CC3 (synth type) first so dependent state is consistent.
    applyCC(3, m_patch.cc[2]);
    for(uint8_t cc = 1; cc <= 11; ++cc)
        if(cc != 3) applyCC(cc, m_patch.cc[cc - 1]);

    return true;
}

// ── State persistence ───────────────────────────────────────────────────────
#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& state, synthLib::StateType type)
{
    if(type != synthLib::StateTypeGlobal) return false;
    state.clear();
    state.push_back(static_cast<uint8_t>(m_pendingIsYM.load() ? 0 : 1)); // engine (intended)
    for(int i = 0; i < 11; ++i) state.push_back(m_cc[i]);
    const uint8_t nameLen = static_cast<uint8_t>(std::min(m_patchName.size(), size_t(255)));
    state.push_back(nameLen);
    for(uint8_t i = 0; i < nameLen; ++i) state.push_back(static_cast<uint8_t>(m_patchName[i]));
    return true;
}

bool Device::setState(const std::vector<uint8_t>& state, synthLib::StateType type)
{
    if(type != synthLib::StateTypeGlobal) return false;
    if(state.size() < 1 + 11) return false;

    setEngine(state[0]);
    std::array<uint8_t, 11> cc{};
    for(int i = 0; i < 11; ++i) cc[i] = state[1 + i];

    if(state.size() >= size_t(1 + 11 + 1))
    {
        const uint8_t nameLen = state[12];
        if(state.size() >= size_t(13 + nameLen))
            m_patchName.assign(reinterpret_cast<const char*>(state.data() + 13), nameLen);
    }

    applyCC(3, cc[2]);
    for(uint8_t c = 1; c <= 11; ++c)
        if(c != 3) applyCC(c, cc[c - 1]);
    return true;
}
#endif

} // namespace ayumiLib
