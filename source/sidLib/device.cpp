/**
 * SID Device — reSID + GoatTracker macro playback engine.
 *
 * Audio thread runs reSID at 985248 Hz PAL clock. A virtual "frame ticker"
 * fires at 50 Hz (every ~882 samples at 44100) and advances each active
 * voice's wavetable + pulsetable pointers, plus a global filtertable
 * pointer. Pointer arithmetic and command decoding mirror gplay.c from
 * GoatTracker 2 (Lasse Öörni, GPL v2).
 */
#include "device.h"

#include <algorithm>
#include <cmath>

namespace sidLib
{

// ── Construction ─────────────────────────────────────────────────────────
Device::Device(const synthLib::DeviceCreateParams& params)
    : synthLib::Device(params)
{
    m_sid.set_chip_model(reSID::MOS8580);
    m_sid.enable_filter(true);
    m_sid.enable_external_filter(true);
    m_sid.set_sampling_parameters(kClockPAL, reSID::SAMPLE_RESAMPLE,
                                  static_cast<double>(m_sampleRate));
    m_sid.reset();

    m_samplesPerFrame = static_cast<double>(m_sampleRate) / kFrameRate;
    initFallbackPatch();
    m_intBuf.resize(2048, 0);
}

Device::~Device() { m_shutdown.store(true); }

#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& state, synthLib::StateType type)
{
    if (type != synthLib::StateTypeGlobal) return false;
    state.clear();
    return true;
}

bool Device::setState(const std::vector<uint8_t>&, synthLib::StateType type)
{
    if (type != synthLib::StateTypeGlobal) return false;
    return true;
}
#endif

// ── Fallback "no instrument" patch — sawtooth + sustained ADSR ───────────
void Device::initFallbackPatch()
{
    writeReg(0x18, 0x0F); // master vol = max, no filter

    for (int v = 0; v < kNumVoices; ++v)
    {
        const uint8_t b = kVoiceBase[v];
        writeReg(b + 0x05, 0x09); // AD: A=0 D=9
        writeReg(b + 0x06, 0xF0); // SR: S=F R=0
        writeReg(b + 0x02, 0x00); writeReg(b + 0x03, 0x08); // PW = 0x800 (50%)
        writeReg(b + 0x04, 0x20); // saw, gate off
        m_voices[v].waveform   = 0x20;
        m_voices[v].pulseWidth = 0x800;
    }
}

void Device::writeReg(uint8_t reg, uint8_t val) { m_sid.write(reg, val); }

// ── Bank / instrument selection ──────────────────────────────────────────
bool Device::loadBankFile(const std::string& filePath)
{
    SidBank b;
    if (!loadBank(filePath, b)) return false;

    {
        std::lock_guard<std::mutex> lk(m_bankMutex);
        m_bank = std::move(b);
        m_currentInstrument = 0;
        m_currentPatchName.clear();
    }
    onAllNotesOff();
    return true;
}

bool Device::selectInstrument(int index)
{
    {
        std::lock_guard<std::mutex> lk(m_bankMutex);
        if (index < 1 || index >= static_cast<int>(m_bank.instruments.size()))
            return false;

        m_currentInstrument = index;
        m_currentPatchName  = m_bank.instruments[index].name;
    }

    // Silence everything — patch character (waveform/PW/macros) would otherwise
    // bleed across the program change and the previous note's release would
    // play with the new instrument's ADSR.
    onAllNotesOff();
    return true;
}

int Device::getInstrumentCount() const
{
    if (m_bank.instruments.size() < 2) return 0;
    return static_cast<int>(m_bank.instruments.size()) - 1;
}

std::string Device::getInstrumentName(int index) const
{
    if (index < 1 || index >= static_cast<int>(m_bank.instruments.size()))
        return {};
    return m_bank.instruments[index].name;
}

// ── Note + bend + vibrato → SID 16-bit frequency ─────────────────────────
static uint16_t noteToSidFreqFloat(float fractionalNote)
{
    const double freq = 440.0 * std::pow(2.0,
        (static_cast<double>(fractionalNote) - 69.0) / 12.0);
    const double f = freq * 16777216.0 / 985248.0;
    return static_cast<uint16_t>(std::clamp(static_cast<int>(f + 0.5), 0, 65535));
}

void Device::writeFreq(int v, uint8_t note)
{
    // Vibrato is a sine wave at kVibratoHz, scaled by mod wheel.
    const float vib = std::sin(m_vibratoPhase) * m_modWheel * kVibratoDepth;
    const float n   = static_cast<float>(note) + m_pitchBendSemis + vib;
    const uint16_t f = noteToSidFreqFloat(n);
    writeReg(kVoiceBase[v] + 0x00, static_cast<uint8_t>(f & 0xFF));
    writeReg(kVoiceBase[v] + 0x01, static_cast<uint8_t>((f >> 8) & 0xFF));
}

// ── keyOn / keyOff ───────────────────────────────────────────────────────
void Device::keyOn(int v, uint8_t note)
{
    auto&         vs   = m_voices[v];
    const uint8_t base = kVoiceBase[v];

    writeFreq(v, note);

    // Arm macro pointers from the active instrument (if any).
    // No instrument loaded → fall back to the "saw + sustained" default.
    bool armedFromInstr = false;
    {
        std::lock_guard<std::mutex> lk(m_bankMutex);
        if (m_currentInstrument > 0
            && m_currentInstrument < static_cast<int>(m_bank.instruments.size()))
        {
            const auto& ins = m_bank.instruments[m_currentInstrument];

            writeReg(base + 0x05, ins.ad);
            writeReg(base + 0x06, ins.sr);

            vs.wptr      = ins.wptr;
            vs.pptr      = ins.pptr;
            vs.waveDelay = 0;
            vs.pulseTime = 0;

            // firstwave: 0xff = keep current/keyon, 0xfe = keyoff (skip),
            // else direct waveform bits (high nibble) to seed.
            if (ins.firstwave == 0xfe)
            {
                writeReg(base + 0x04, vs.waveform); // gate off
                return;
            }
            else if (ins.firstwave == 0xff)
            {
                // Fall through to "gate on with current waveform"
            }
            else
            {
                vs.waveform = ins.firstwave;
            }
            armedFromInstr = true;
        }
    }

    if (!armedFromInstr)
    {
        // No instrument: default sawtooth + sustained ADSR
        vs.waveform = 0x20;
        writeReg(base + 0x05, 0x09);
        writeReg(base + 0x06, 0xF0);
        writeReg(base + 0x02, 0x00);
        writeReg(base + 0x03, 0x08);
        vs.pulseWidth = 0x800;
        vs.wptr = vs.pptr = 0;
    }

    writeReg(base + 0x04, static_cast<uint8_t>(vs.waveform | 0x01));

    // Apply first wavetable step immediately — avoids stale-waveform click
    // for firstwave=0xff instruments while waiting up to 20 ms for next tick.
    if (vs.wptr != 0)
    {
        std::lock_guard<std::mutex> lk(m_bankMutex);
        tickWaveTable(v);
    }
}

void Device::keyOff(int v)
{
    auto& vs = m_voices[v];
    writeReg(kVoiceBase[v] + 0x04, vs.waveform); // gate cleared, waveform retained
    vs.wptr = 0;
    vs.pptr = 0;
}

// ── Voice allocation (oldest-voice stealing) ─────────────────────────────
int Device::allocVoice()
{
    for (int v = 0; v < kNumVoices; ++v)
        if (!m_voices[v].active) return v;

    int      best    = 0;
    uint64_t bestAge = UINT64_MAX;
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (!m_voices[v].sustained && m_voices[v].age < bestAge)
        {
            bestAge = m_voices[v].age;
            best    = v;
        }
    }
    keyOff(best);
    m_voices[best] = VoiceMacroState{};
    return best;
}

// ── MIDI dispatch ────────────────────────────────────────────────────────
void Device::onNoteOn(uint8_t note, uint8_t vel)
{
    if (vel == 0) { onNoteOff(note); return; }

    const int v = allocVoice();
    ++m_ageCounter;

    auto& vs       = m_voices[v];
    vs.active      = true;
    vs.held        = true;
    vs.sustained   = false;
    vs.midiNote    = note;
    vs.age         = m_ageCounter;

    // Hand control of this voice's pulse width back to the patch macro.
    // Filter overrides are global (single SID filter), so they only clear
    // when ALL voices go silent — handled in onAllNotesOff().
    m_overridePulseWidth[v] = false;

    keyOn(v, note);

    // Arm filter macro on first note-on of any voice that triggers it
    {
        std::lock_guard<std::mutex> lk(m_bankMutex);
        if (m_currentInstrument > 0
            && m_currentInstrument < static_cast<int>(m_bank.instruments.size()))
        {
            const auto& ins = m_bank.instruments[m_currentInstrument];
            if (ins.fptr != 0 && m_filterPtr == 0)
            {
                m_filterPtr  = ins.fptr;
                m_filterTime = 0;
            }
        }
    }
}

void Device::onNoteOff(uint8_t note)
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vs = m_voices[v];
        if (vs.active && vs.held && vs.midiNote == note)
        {
            vs.held = false;
            if (m_sustainPedal)
            {
                vs.sustained = true;
            }
            else
            {
                keyOff(v);
                vs.active    = false;
                vs.sustained = false;
            }
            return;
        }
    }
}

void Device::onAllNotesOff()
{
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (m_voices[v].active)
        {
            keyOff(v);
            m_voices[v] = VoiceMacroState{};
        }
        m_overridePulseWidth[v] = false;
    }
    m_filterPtr  = 0;
    m_filterTime = 0;
    m_overrideFilterCutoff = false;
    m_overrideFilterCtrl   = false;
}

void Device::onSustainPedal(bool down)
{
    m_sustainPedal = down;
    if (down) return;
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto& vs = m_voices[v];
        if (vs.active && vs.sustained && !vs.held)
        {
            keyOff(v);
            vs.active    = false;
            vs.sustained = false;
        }
    }
}

bool Device::sendMidi(const synthLib::SMidiEvent& ev,
                      std::vector<synthLib::SMidiEvent>&)
{
    if (!ev.sysex.empty()) return true;
    if (ev.a >= 0xF8)      return true;

    switch (ev.a & 0xF0)
    {
    case 0x80: onNoteOff(ev.b); break;
    case 0x90: onNoteOn (ev.b, ev.c); break;
    case 0xB0:
        switch (ev.b)
        {
        case 1:   onModWheel(ev.c); break;          // CC1 — vibrato depth
        case 22:                                     // pitch-bend range in semitones (capped at 48)
            m_pitchBendRange = static_cast<float>(std::min<int>(ev.c, 48));
            break;
        case 64:  onSustainPedal(ev.c >= 64); break; // sustain pedal
        case 71:  onFilterResonance(ev.c); break;    // SID filter resonance
        case 74:  onFilterCutoff(ev.c); break;       // SID filter cutoff
        case 75:  onPulseWidth(ev.c); break;         // pulse width
        case 120: case 123: onAllNotesOff(); break;
        }
        break;
    case 0xE0: // pitch bend: 14-bit, lsb=ev.b, msb=ev.c
        onPitchBend((static_cast<int>(ev.c) << 7) | static_cast<int>(ev.b));
        break;
    }
    return true;
}

// ── Pitch bend / mod wheel / live CC handlers ────────────────────────────
void Device::onPitchBend(int raw14)
{
    const float norm = static_cast<float>(raw14 - 8192) / 8192.0f; // -1..+1
    m_pitchBendSemis = norm * m_pitchBendRange;
    // Re-write freq on all sounding voices immediately for snappy response
    for (int v = 0; v < kNumVoices; ++v)
        if (m_voices[v].active)
            writeFreq(v, m_voices[v].midiNote);
}

void Device::onModWheel(uint8_t value)
{
    m_modWheel = static_cast<float>(value) / 127.0f;
    // Vibrato applies on next frame tick — no immediate re-write needed,
    // and re-writing here would only set freq to phase-snapshot anyway.
}

// SID filter cutoff is 11-bit ($15 = low 3 bits, $16 = high 8 bits).
// CC 0..127 → cutoff 0..2047 (we use the top 8 bits only for granularity).
//
// The chip's filter is bypassed unless (a) at least one voice is routed
// through it via $17 low nibble and (b) a filter mode bit is set in $18
// bits 4-6. Patches that don't use the filter leave both at zero, so a
// bare cutoff write would be inaudible — we ensure both are armed when
// the user first touches CC74.
void Device::onFilterCutoff(uint8_t value)
{
    // Map CC 0..127 → 11-bit cutoff 0..2047 across the full filter range.
    const int      cutoff11 = (static_cast<int>(value) * 2047) / 127;
    const uint8_t  lo3      = static_cast<uint8_t>(cutoff11 & 0x07);
    const uint8_t  hi8      = static_cast<uint8_t>((cutoff11 >> 3) & 0xFF);

    m_filterCutoffHi = hi8;
    m_overrideFilterCutoff = true;
    writeReg(0x15, lo3);
    writeReg(0x16, hi8);

    // Auto-arm filter routing + low-pass mode if patch didn't set them up.
    // Once armed, leave the resonance nibble alone so CC71 still works
    // independently. Mark $17/$18 as overridden so the filtertable can't
    // disable the filter behind our back.
    if ((m_filterCtrl & 0x07) == 0)
    {
        m_filterCtrl = static_cast<uint8_t>((m_filterCtrl & 0xF0) | 0x07);
        writeReg(0x17, m_filterCtrl);
    }
    if (m_filterTypeBits == 0x00)
    {
        m_filterTypeBits = 0x10; // low-pass
        writeReg(0x18, static_cast<uint8_t>(m_filterTypeBits | 0x0F));
    }
    m_overrideFilterCtrl = true;
}

// SID $17 layout: bits 7-4 = resonance, bits 3-0 = filter routing per voice.
// We override the resonance nibble and route all 3 voices through the filter
// (lower nibble = 0x07 = filt1+filt2+filt3 enable).
void Device::onFilterResonance(uint8_t value)
{
    const uint8_t res = static_cast<uint8_t>((value * 15) / 127);
    m_filterCtrl = static_cast<uint8_t>((res << 4) | 0x07);
    m_overrideFilterCtrl = true; // freeze patch's filtertable mode/res sets

    writeReg(0x17, m_filterCtrl);
    // If no patch filter mode is active, default to low-pass so the filter
    // is audible the moment the user touches CC71.
    if (m_filterTypeBits == 0x00)
        m_filterTypeBits = 0x10; // bit 4 = lowpass
    writeReg(0x18, static_cast<uint8_t>(m_filterTypeBits | 0x0F));
}

// CC75 sets the 12-bit pulse width on all 3 voices simultaneously.
// Once touched, each voice's pulsetable stops writing PW for the rest of the
// current note (cleared on next note-on for that voice).
void Device::onPulseWidth(uint8_t value)
{
    const uint16_t pw = static_cast<uint16_t>((value * 4095) / 127);
    for (int v = 0; v < kNumVoices; ++v)
    {
        m_voices[v].pulseWidth = pw;
        m_overridePulseWidth[v] = true;
        writeReg(kVoiceBase[v] + 0x02, static_cast<uint8_t>(pw & 0xFF));
        writeReg(kVoiceBase[v] + 0x03, static_cast<uint8_t>((pw >> 8) & 0x0F));
    }
}

void Device::readMidiOut(std::vector<synthLib::SMidiEvent>&) {}

// ── Macro engine ─────────────────────────────────────────────────────────
// Per gplay.c. We hold m_bankMutex throughout a tick so a swap of the
// bank doesn't race with a half-walked pointer chain.

void Device::tickWaveTable(int v)
{
    auto& vs = m_voices[v];
    if (!vs.active || vs.wptr == 0) return;

    const auto& lt = m_bank.ltable[kWTBL];
    const auto& rt = m_bank.rtable[kWTBL];

    // The wavetable is allowed to advance multiple steps per frame ONLY for
    // jump/wave-set commands (delays naturally consume frames). To keep this
    // simple we walk forward once per frame, but follow inline waveform sets
    // since GoatTracker treats those as advancing immediately.
    for (int safety = 0; safety < 32; ++safety)
    {
        if (vs.wptr == 0) return;
        const size_t i = static_cast<size_t>(vs.wptr) - 1;
        if (i >= lt.size()) { vs.wptr = 0; return; }

        const uint8_t l = lt[i];
        const uint8_t r = rt[i];

        if (l == 0xFF) // jump
        {
            vs.wptr = r; // 0 = stop
            continue;
        }

        if (l <= 0x0F) // delay
        {
            if (vs.waveDelay == 0)
                vs.waveDelay = l;
            else
                vs.waveDelay--;
            if (vs.waveDelay == 0)
                ++vs.wptr;
            return; // delay always consumes a frame
        }

        if (l >= 0xF0 && l <= 0xFE) // wave command — best-effort
        {
            // We don't implement portamento/vibrato in plugin context.
            // Just advance past the command.
            ++vs.wptr;
            continue;
        }

        // 0x10–0xDF: waveform set (high nibble = SID $04 waveform bits,
        //            low bit = gate; we store wavebits-only and re-add gate
        //            from voice state so keyOff cleanly clears it).
        // 0xE0–0xEF: "silent" waveform — keep current waveform but force
        //            gate off this frame.
        if (l < 0xE0)
        {
            vs.waveform = static_cast<uint8_t>(l & 0xFE); // strip gate bit
            const bool gate = vs.held || vs.sustained;
            writeReg(kVoiceBase[v] + 0x04,
                     static_cast<uint8_t>(vs.waveform | (gate ? 0x01 : 0x00)));
        }
        else
        {
            // Silent — write low-nibble bits only; chip output goes quiet.
            // Don't mutate vs.waveform — when the table moves on to the next
            // real waveform set the previous bits would otherwise be lost.
            writeReg(kVoiceBase[v] + 0x04, static_cast<uint8_t>(l & 0x0F));
        }

        // r=0x80: no change. r<0x80: relative offset. r>0x80: absolute (mask 0x7F).
        if (r != 0x80)
        {
            int n = (r < 0x80) ? static_cast<int>(vs.midiNote) + static_cast<int>(r)
                                : static_cast<int>(r & 0x7F);
            n = std::clamp(n, 0, 127);
            writeFreq(v, static_cast<uint8_t>(n));
        }

        ++vs.wptr;
        return; // a wave write consumes the frame
    }
    vs.wptr = 0; // safety break — corrupted table would loop forever otherwise
}

void Device::tickPulseTable(int v)
{
    auto& vs = m_voices[v];
    if (!vs.active || vs.pptr == 0) return;

    const auto& lt = m_bank.ltable[kPTBL];
    const auto& rt = m_bank.rtable[kPTBL];

    for (int safety = 0; safety < 32; ++safety)
    {
        if (vs.pptr == 0) return;
        const size_t i = static_cast<size_t>(vs.pptr) - 1;
        if (i >= lt.size()) { vs.pptr = 0; return; }

        const uint8_t l = lt[i];
        const uint8_t r = rt[i];

        if (l == 0xFF) // jump
        {
            vs.pptr = r;
            continue;
        }

        const bool pwOverride = m_overridePulseWidth[v];

        if (l >= 0x80) // direct PW set (12-bit: low 4 bits of l = high nibble)
        {
            const uint16_t pw = static_cast<uint16_t>(((l & 0x0F) << 8) | r);
            vs.pulseWidth = pw;
            if (!pwOverride)
            {
                writeReg(kVoiceBase[v] + 0x02, static_cast<uint8_t>(pw & 0xFF));
                writeReg(kVoiceBase[v] + 0x03, static_cast<uint8_t>((pw >> 8) & 0x0F));
            }
            ++vs.pptr;
            return;
        }

        // l == 0x00: cutoff/PW direct set with rtable as 8-bit value
        if (l == 0x00)
        {
            vs.pulseWidth = r;
            if (!pwOverride)
            {
                writeReg(kVoiceBase[v] + 0x02, r);
                writeReg(kVoiceBase[v] + 0x03, 0x00);
            }
            ++vs.pptr;
            return;
        }

        // l in 0x01..0x7F: modulation duration starts (or continues)
        if (vs.pulseTime == 0)
        {
            vs.pulseTime  = l;
            // rtable encoded as signed byte: < 0x80 = positive, >= 0x80 = negative
            vs.pulseSpeed = (r < 0x80) ? static_cast<int>(r)
                                       : static_cast<int>(r) - 0x100;
        }
        int pw = static_cast<int>(vs.pulseWidth) + vs.pulseSpeed;
        pw &= 0x0FFF;
        vs.pulseWidth = static_cast<uint16_t>(pw);
        if (!pwOverride)
        {
            writeReg(kVoiceBase[v] + 0x02, static_cast<uint8_t>(pw & 0xFF));
            writeReg(kVoiceBase[v] + 0x03, static_cast<uint8_t>((pw >> 8) & 0x0F));
        }

        if (--vs.pulseTime == 0)
            ++vs.pptr;
        return;
    }
    vs.pptr = 0;
}

void Device::tickFilterTable()
{
    if (m_filterPtr == 0) return;
    if (m_bank.ltable[kFTBL].empty()) { m_filterPtr = 0; return; }

    const auto& lt = m_bank.ltable[kFTBL];
    const auto& rt = m_bank.rtable[kFTBL];

    for (int safety = 0; safety < 16; ++safety)
    {
        if (m_filterPtr == 0) return;
        const size_t i = static_cast<size_t>(m_filterPtr) - 1;
        if (i >= lt.size()) { m_filterPtr = 0; return; }

        const uint8_t l = lt[i];
        const uint8_t r = rt[i];

        if (l == 0xFF) { m_filterPtr = r; continue; }

        if (l >= 0x80) // mode/resonance set
        {
            // Always update the bookkeeping so subsequent jumps still work,
            // but skip the chip writes if the user has CC-overridden the filter.
            m_filterTypeBits = static_cast<uint8_t>(l & 0x70);
            m_filterCtrl     = r;
            if (!m_overrideFilterCtrl)
            {
                writeReg(0x17, m_filterCtrl);
                writeReg(0x18, static_cast<uint8_t>(m_filterTypeBits | 0x0F));
            }
            ++m_filterPtr;
            return;
        }

        if (l == 0x00) // cutoff direct set
        {
            m_filterCutoffHi = r;
            if (!m_overrideFilterCutoff)
            {
                writeReg(0x15, 0x00);
                writeReg(0x16, m_filterCutoffHi);
            }
            ++m_filterPtr;
            return;
        }

        // l in 0x01..0x7F: modulation duration
        if (m_filterTime == 0)
        {
            m_filterTime  = l;
            m_filterSpeed = (r < 0x80) ? static_cast<int>(r)
                                       : static_cast<int>(r) - 0x100;
        }
        int co = static_cast<int>(m_filterCutoffHi) + m_filterSpeed;
        m_filterCutoffHi = static_cast<uint8_t>(std::clamp(co, 0, 255));
        if (!m_overrideFilterCutoff)
            writeReg(0x16, m_filterCutoffHi);

        if (--m_filterTime == 0)
            ++m_filterPtr;
        return;
    }
    m_filterPtr = 0;
}

void Device::tickMacros()
{
    std::lock_guard<std::mutex> lk(m_bankMutex);
    for (int v = 0; v < kNumVoices; ++v)
    {
        tickWaveTable(v);
        tickPulseTable(v);
    }
    tickFilterTable();

    // Advance vibrato LFO at 50 Hz frame rate.
    static constexpr float kTwoPi = 6.28318530717958647692f;
    m_vibratoPhase += kTwoPi * kVibratoHz / static_cast<float>(kFrameRate);
    if (m_vibratoPhase > kTwoPi) m_vibratoPhase -= kTwoPi;

    // Re-write frequency on all sounding voices each frame so vibrato
    // and the wavetable's note-offset stay in sync. Only voices without
    // a wavetable note-offset fall through to here naturally — the table
    // already writes freq when r != 0x80, so we only need to touch voices
    // that aren't being driven by a wavetable note.
    if (m_modWheel > 0.001f)
    {
        for (int v = 0; v < kNumVoices; ++v)
            if (m_voices[v].active)
                writeFreq(v, m_voices[v].midiNote);
    }
}

// ── Audio render ─────────────────────────────────────────────────────────
void Device::processAudio(const synthLib::TAudioInputs&,
                          const synthLib::TAudioOutputs& outputs, size_t samples)
{
    if (m_shutdown.load()) return;

    if (m_intBuf.size() < samples)
        m_intBuf.resize(samples, 0);

    auto* outL = outputs[0];
    auto* outR = outputs[1];

    size_t produced = 0;
    while (produced < samples)
    {
        // How many samples until the next 50 Hz frame tick fires?
        const double remaining = m_samplesPerFrame - m_frameAccumulator;
        const size_t toFrame   = static_cast<size_t>(std::max(1.0, std::ceil(remaining)));
        const size_t want      = std::min(samples - produced, toFrame);

        reSID::cycle_count delta = static_cast<reSID::cycle_count>(
            std::ceil(static_cast<double>(want) * kClockPAL / m_sampleRate) + 4);
        const int got = m_sid.clock(delta, m_intBuf.data() + produced,
                                    static_cast<int>(want), 1);
        if (got <= 0) break;

        produced            += static_cast<size_t>(got);
        m_frameAccumulator  += static_cast<double>(got);
        if (m_frameAccumulator >= m_samplesPerFrame)
        {
            m_frameAccumulator -= m_samplesPerFrame;
            tickMacros();
        }
    }

    // SID is mono — duplicate into both stereo channels.
    // Calibrated so a 3-voice chord in low octaves sits at ~0 dBFS.
    constexpr float kScale = 2.249f / 32768.0f;
    for (size_t i = 0; i < samples; ++i)
    {
        const float s = static_cast<float>(m_intBuf[i]) * kScale;
        outL[i] = s;
        outR[i] = s;
    }
}

} // namespace sidLib
