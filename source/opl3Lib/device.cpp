/**
 * OPL3 Device implementation
 * Nuked OPL3 v1.8 (Nuke.YKT) — LGPL 2.1
 */
#include "device.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace opl3Lib
{

// ── OPL3 channel → slot base mapping ─────────────────────────────────────────
// Each OPL3 channel occupies two operator slots.
// Lower bank channels 0–8 → slots 0–17; upper bank channels 9–17 → slots 18–35.
// Within each bank, the slot layout is: ch 0→s0, ch1→s1, ch2→s2, ch3→s8, ch4→s9,
// ch5→s10, ch6→s16, ch7→s17, ch8→s18 (but we use the register address scheme).
//
// For register addressing we use the standard OPL3 operator address table:
//   Modulator slot reg offset: 0,1,2,8,9,10,16,17,18  (for channels 0-8 in bank 0/1)
//   Carrier   slot reg offset: mod + 3
//
// Simpler direct mapping for 2-op melody channels 0–17:
//   ch 0–8  → bank 0 (reg base 0x000), offsets {0,1,2,8,9,10,16,17,18}
//   ch 9–17 → bank 1 (reg base 0x100), offsets {0,1,2,8,9,10,16,17,18}

static const uint8_t kSlotOffset[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

// Returns the register bank base (0x000 or 0x100) and slot-within-bank for channel ch.
static void channelToBank(uint8_t ch, uint16_t& bank, uint8_t& chInBank)
{
    if (ch < 9)  { bank = 0x000; chInBank = ch; }
    else         { bank = 0x100; chInBank = static_cast<uint8_t>(ch - 9); }
}

// ── SbiPatch ──────────────────────────────────────────────────────────────────
bool SbiPatch::loadFromBytes(const uint8_t* data, size_t size, SbiPatch& out)
{
    // Minimum: 3 (magic) + 1 (0x1A) + 32 (name) + 11 (regs) = 47 bytes
    if (size < 47) return false;
    if (data[0] != 'S' || data[1] != 'B' || data[2] != 'I') return false;

    // Name: bytes 4–35, null-terminated
    char nameBuf[33] = {};
    std::memcpy(nameBuf, data + 4, 32);
    out.name = nameBuf;

    const uint8_t* r = data + 36;
    out.modTVSKSRMult = r[0];
    out.carTVSKSRMult = r[1];
    out.modKSLTL      = r[2];
    out.carKSLTL      = r[3];
    out.modARDR        = r[4];
    out.carARDR        = r[5];
    out.modSLRR        = r[6];
    out.carSLRR        = r[7];
    out.modWF          = r[8];
    out.carWF          = r[9];
    out.fbAlg          = r[10];
    return true;
}

// ── Device ────────────────────────────────────────────────────────────────────
Device::Device(const synthLib::DeviceCreateParams& params)
    : synthLib::Device(params)
{
    OPL3_Reset(&m_chip, static_cast<uint32_t>(m_sampleRate));

    // Enable OPL3 mode (register 0x105 bit 0)
    writeReg(0x105, 0x01);

    // Allow waveform select on all operators (register 0x01 bit 5)
    writeReg(0x001, 0x20);

    m_intBuf.resize(2048 * 2, 0); // stereo int16 scratch
}

Device::~Device()
{
    m_shutdown.store(true);
}

// ── State persistence ─────────────────────────────────────────────────────────
#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& state, synthLib::StateType type)
{
    if (type != synthLib::StateTypeGlobal) return false;
    // Store the 11 patch registers + name length + name bytes
    state.clear();
    state.push_back(m_currentPatch.modTVSKSRMult);
    state.push_back(m_currentPatch.carTVSKSRMult);
    state.push_back(m_currentPatch.modKSLTL);
    state.push_back(m_currentPatch.carKSLTL);
    state.push_back(m_currentPatch.modARDR);
    state.push_back(m_currentPatch.carARDR);
    state.push_back(m_currentPatch.modSLRR);
    state.push_back(m_currentPatch.carSLRR);
    state.push_back(m_currentPatch.modWF);
    state.push_back(m_currentPatch.carWF);
    state.push_back(m_currentPatch.fbAlg);
    // name as uint8 length + bytes
    const uint8_t nameLen = static_cast<uint8_t>(
        std::min(m_currentPatch.name.size(), size_t(255)));
    state.push_back(nameLen);
    for (uint8_t i = 0; i < nameLen; ++i)
        state.push_back(static_cast<uint8_t>(m_currentPatch.name[i]));
    return true;
}

bool Device::setState(const std::vector<uint8_t>& state, synthLib::StateType type)
{
    if (type != synthLib::StateTypeGlobal) return false;
    if (state.size() < 11) return false;

    m_currentPatch.modTVSKSRMult = state[0];
    m_currentPatch.carTVSKSRMult = state[1];
    m_currentPatch.modKSLTL      = state[2];
    m_currentPatch.carKSLTL      = state[3];
    m_currentPatch.modARDR        = state[4];
    m_currentPatch.carARDR        = state[5];
    m_currentPatch.modSLRR        = state[6];
    m_currentPatch.carSLRR        = state[7];
    m_currentPatch.modWF          = state[8];
    m_currentPatch.carWF          = state[9];
    m_currentPatch.fbAlg          = state[10];

    if (state.size() >= 12)
    {
        const uint8_t nameLen = state[11];
        if (state.size() >= size_t(12 + nameLen))
            m_currentPatch.name.assign(
                reinterpret_cast<const char*>(state.data() + 12), nameLen);
    }
    m_patchName = m_currentPatch.name;

    // Re-apply patch to all active voices
    for (uint8_t ch = 0; ch < kNumChannels; ++ch)
        if (m_voices[ch].active)
            applyPatch(ch);

    return true;
}
#endif

// ── SBI file loading ──────────────────────────────────────────────────────────
bool Device::loadSbi(const std::string& filePath)
{
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;

    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));

    SbiPatch patch;
    if (!SbiPatch::loadFromBytes(buf.data(), sz, patch)) return false;

    m_currentPatch = patch;
    m_patchName    = patch.name;

    // Re-apply patch to all active voices
    for (uint8_t ch = 0; ch < kNumChannels; ++ch)
        if (m_voices[ch].active)
            applyPatch(ch);

    return true;
}

// ── OPL3 register write ───────────────────────────────────────────────────────
void Device::writeReg(uint16_t reg, uint8_t val)
{
    OPL3_WriteRegBuffered(&m_chip, reg, val);
}

// ── Apply current patch to OPL3 channel ch ───────────────────────────────────
void Device::applyPatch(uint8_t ch)
{
    uint16_t bank;
    uint8_t  chInBank;
    channelToBank(ch, bank, chInBank);

    const uint8_t so = kSlotOffset[chInBank]; // modulator slot offset
    const uint8_t sc = so + 3;                // carrier slot offset

    writeReg(bank | (0x20 + so), m_currentPatch.modTVSKSRMult);
    writeReg(bank | (0x20 + sc), m_currentPatch.carTVSKSRMult);
    writeReg(bank | (0x40 + so), m_currentPatch.modKSLTL);
    writeReg(bank | (0x40 + sc), m_currentPatch.carKSLTL);
    writeReg(bank | (0x60 + so), m_currentPatch.modARDR);
    writeReg(bank | (0x60 + sc), m_currentPatch.carARDR);
    writeReg(bank | (0x80 + so), m_currentPatch.modSLRR);
    writeReg(bank | (0x80 + sc), m_currentPatch.carSLRR);
    writeReg(bank | (0xE0 + so), m_currentPatch.modWF);
    writeReg(bank | (0xE0 + sc), m_currentPatch.carWF);
    // 0xC0: feedback/algorithm — also enable Left+Right output (bits 4-5)
    writeReg(bank | (0xC0 + chInBank), static_cast<uint8_t>(m_currentPatch.fbAlg | 0x30));
}

// ── Frequency calculation ─────────────────────────────────────────────────────
// OPL3 F-number formula: Fnum = note_freq * 2^(20-block) / 49716
// We use block 4 for the middle range, adjusting block per octave.
void Device::noteToFnumBlock(uint8_t note, float semitones,
                              uint16_t& fnum, uint8_t& block)
{
    // MIDI note 69 = A4 = 440 Hz
    const float freq = 440.0f * std::pow(2.0f,
        (static_cast<float>(note) - 69.0f + semitones) / 12.0f);

    // Find best block (0–7) so fnum stays in 0–1023
    block = 4;
    float fnumF = freq * (1 << (20 - block)) / 49716.0f;

    while (fnumF > 1023.0f && block < 7) { block++; fnumF /= 2.0f; }
    while (fnumF < 0.5f    && block > 0) { block--; fnumF *= 2.0f; }

    fnum = static_cast<uint16_t>(std::clamp(static_cast<int>(fnumF + 0.5f), 0, 1023));
}

// ── Key on/off ────────────────────────────────────────────────────────────────
void Device::keyOn(uint8_t oplCh, uint8_t note, float pitchBend)
{
    uint16_t bank;
    uint8_t  chInBank;
    channelToBank(oplCh, bank, chInBank);

    applyPatch(oplCh);

    uint16_t fnum; uint8_t blk;
    noteToFnumBlock(note, pitchBend, fnum, blk);

    // Write F-number low 8 bits, then block + high 2 bits of fnum (no key-on yet)
    writeReg(bank | (0xA0 + chInBank), static_cast<uint8_t>(fnum & 0xFF));
    writeReg(bank | (0xB0 + chInBank), static_cast<uint8_t>(
        ((blk & 0x07) << 2) | ((fnum >> 8) & 0x03)));

    // Key on
    writeReg(bank | (0xB0 + chInBank), static_cast<uint8_t>(
        0x20 | ((blk & 0x07) << 2) | ((fnum >> 8) & 0x03)));
}

void Device::keyOff(uint8_t oplCh)
{
    uint16_t bank;
    uint8_t  chInBank;
    channelToBank(oplCh, bank, chInBank);

    // Clear key-on bit, preserve frequency
    const uint8_t prev = 0; // We don't cache B0 register, so just write 0 (frequency already decayed)
    (void)prev;

    // Read current fnum/block from voice slot — we kept note there
    const auto& voice = m_voices[oplCh];
    uint16_t fnum; uint8_t blk;
    noteToFnumBlock(voice.midiNote, voice.pitchBendSemitones, fnum, blk);

    writeReg(bank | (0xA0 + chInBank), static_cast<uint8_t>(fnum & 0xFF));
    writeReg(bank | (0xB0 + chInBank), static_cast<uint8_t>(
        ((blk & 0x07) << 2) | ((fnum >> 8) & 0x03))); // key-on cleared
}

void Device::setPitch(uint8_t oplCh, uint8_t note, float semitones)
{
    uint16_t bank;
    uint8_t  chInBank;
    channelToBank(oplCh, bank, chInBank);

    uint16_t fnum; uint8_t blk;
    noteToFnumBlock(note, semitones, fnum, blk);

    const bool held = m_voices[oplCh].held || m_voices[oplCh].sustained;

    writeReg(bank | (0xA0 + chInBank), static_cast<uint8_t>(fnum & 0xFF));
    writeReg(bank | (0xB0 + chInBank), static_cast<uint8_t>(
        (held ? 0x20 : 0x00) | ((blk & 0x07) << 2) | ((fnum >> 8) & 0x03)));
}

// ── Voice allocation (oldest-voice stealing) ─────────────────────────────────
uint8_t Device::allocVoice(uint8_t midiNote)
{
    // 1. Find a free voice
    for (uint8_t i = 0; i < kNumChannels; ++i)
        if (!m_voices[i].active) return i;

    // 2. Steal oldest held voice (not sustained)
    uint8_t  best = 0;
    uint64_t bestAge = UINT64_MAX;
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (!m_voices[i].sustained && m_voices[i].age < bestAge)
        {
            bestAge = m_voices[i].age;
            best    = i;
        }
    }

    // Force key off on stolen voice
    keyOff(best);
    m_voices[best].active    = false;
    m_voices[best].held      = false;
    m_voices[best].sustained = false;
    return best;
}

// ── MIDI dispatch ─────────────────────────────────────────────────────────────
void Device::onNoteOn(uint8_t note, uint8_t vel)
{
    if (vel == 0) { onNoteOff(note); return; }

    const uint8_t ch = allocVoice(note);

    m_ageCounter++;
    m_voices[ch].active              = true;
    m_voices[ch].held                = true;
    m_voices[ch].sustained           = false;
    m_voices[ch].midiNote            = note;
    m_voices[ch].channel             = ch;
    m_voices[ch].age                 = m_ageCounter;
    m_voices[ch].pitchBendSemitones  = m_pitchBend;

    // Simple velocity → TL attenuation: louder = less attenuation on carrier
    // TL is 6 bits (0=max, 63=min). Map velocity 0–127 → attenuation 32–0 added to patch TL.
    const uint8_t savedCarKSLTL = m_currentPatch.carKSLTL;
    const uint8_t velAtten = static_cast<uint8_t>((127 - vel) * 32 / 127);
    const uint8_t carTL = static_cast<uint8_t>(
        std::min(63, (m_currentPatch.carKSLTL & 0x3F) + velAtten));
    m_currentPatch.carKSLTL = static_cast<uint8_t>(
        (m_currentPatch.carKSLTL & 0xC0) | carTL);

    keyOn(ch, note, m_pitchBend);

    // Restore original carKSLTL (velocity shouldn't permanently alter patch)
    m_currentPatch.carKSLTL = savedCarKSLTL;
}

void Device::onNoteOff(uint8_t note)
{
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (m_voices[i].active && m_voices[i].held && m_voices[i].midiNote == note)
        {
            m_voices[i].held = false;
            if (m_sustainPedal)
            {
                m_voices[i].sustained = true;
            }
            else
            {
                keyOff(i);
                m_voices[i].active    = false;
                m_voices[i].sustained = false;
            }
            break; // release oldest occurrence only
        }
    }
}

void Device::onAllNotesOff()
{
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (m_voices[i].active)
        {
            keyOff(i);
            m_voices[i].active    = false;
            m_voices[i].held      = false;
            m_voices[i].sustained = false;
        }
    }
}

void Device::onPitchBend(float semitones)
{
    m_pitchBend = semitones;
    for (uint8_t i = 0; i < kNumChannels; ++i)
    {
        if (m_voices[i].active)
        {
            m_voices[i].pitchBendSemitones = semitones;
            setPitch(i, m_voices[i].midiNote, semitones);
        }
    }
}

// ── sendMidi ──────────────────────────────────────────────────────────────────
bool Device::sendMidi(const synthLib::SMidiEvent& ev, std::vector<synthLib::SMidiEvent>&)
{
    if (!ev.sysex.empty()) return true; // no sysex for OPL3
    if (ev.a >= 0xF8)      return true; // real-time

    const uint8_t status = ev.a & 0xF0;

    switch (status)
    {
    case 0x80: // Note Off
        onNoteOff(ev.b);
        break;

    case 0x90: // Note On
        onNoteOn(ev.b, ev.c);
        break;

    case 0xB0: // Control Change
        switch (ev.b)
        {
        case 7:  // Main volume — scale all carrier TLs
            // Simple approach: store and apply on next note
            break;
        case 64: // Sustain pedal
            m_sustainPedal = (ev.c >= 64);
            if (!m_sustainPedal)
            {
                // Release all sustained voices
                for (uint8_t i = 0; i < kNumChannels; ++i)
                {
                    if (m_voices[i].active && m_voices[i].sustained && !m_voices[i].held)
                    {
                        keyOff(i);
                        m_voices[i].active    = false;
                        m_voices[i].sustained = false;
                    }
                }
            }
            break;
        case 120: // All sound off
        case 123: // All notes off
            onAllNotesOff();
            break;
        }
        break;

    case 0xE0: // Pitch Bend
    {
        // 14-bit value: LSB=ev.b, MSB=ev.c
        const int raw = (static_cast<int>(ev.c) << 7) | static_cast<int>(ev.b);
        const float norm = static_cast<float>(raw - 8192) / 8192.0f; // -1..+1
        onPitchBend(norm * kPitchBendRange);
        break;
    }

    default:
        break;
    }

    return true;
}

// ── readMidiOut ───────────────────────────────────────────────────────────────
void Device::readMidiOut(std::vector<synthLib::SMidiEvent>&)
{
    // OPL3 has no MIDI output
}

// ── processAudio ──────────────────────────────────────────────────────────────
void Device::processAudio(const synthLib::TAudioInputs&,
                          const synthLib::TAudioOutputs& outputs, size_t samples)
{
    if (m_shutdown.load()) return;

    // Grow scratch buffer if needed
    if (m_intBuf.size() < samples * 2)
        m_intBuf.resize(samples * 2, 0);

    // Generate stereo int16 samples from OPL3
    // OPL3_GenerateStream produces interleaved [L, R] int16 pairs
    OPL3_GenerateStream(&m_chip, m_intBuf.data(), static_cast<uint32_t>(samples));

    // Convert int16 stereo → float stereo.
    // OPL3 peaks well below ±32768 in practice (single voice ~±8192), so apply ×4
    // output gain to bring levels in line with other synths at unity.
    auto* outL = outputs[0];
    auto* outR = outputs[1];
    constexpr float kScale = 4.0f / 32768.0f;

    for (size_t i = 0; i < samples; ++i)
    {
        outL[i] = static_cast<float>(m_intBuf[i * 2 + 0]) * kScale;
        outR[i] = static_cast<float>(m_intBuf[i * 2 + 1]) * kScale;
    }
}

} // namespace opl3Lib
