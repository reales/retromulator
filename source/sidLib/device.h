/**
 * SID Device — Retromulator adapter for reSID (Dag Lem, GPL v2).
 *
 * Features:
 *  - 8580 chip model, RESAMPLE sampling, PAL clock (985248 Hz)
 *  - 3-voice polyphony (oldest-voice stealing); all voices share the
 *    currently selected GoatTracker instrument
 *  - GoatTracker .sng / .ins patch loading (GTS3/4/5, GTI3/4/5) — bank holds
 *    N instruments + four shared macro tables
 *  - Per-voice wavetable + pulsetable playback at 50 Hz frame rate
 *  - Global filtertable playback (filter is shared on SID hardware)
 *  - Speedtable used by wave commands (portamento / vibrato lookup)
 *  - Sustain pedal (CC64)
 *
 * Macro coverage in this pass:
 *  - WTBL: delays (0x00–0x0f), waveform sets (0x10–0xdf), silent waveform
 *    (0xe0–0xef), jump (0xff). Wave commands (0xf0–0xfe) other than no-op
 *    and "set wave" are recognised but mostly ignored — portamento/vibrato
 *    require a pattern context we don't have in plugin mode.
 *  - PTBL: pulse modulation (0x01–0x7f duration), direct set (0x80–0xff),
 *    jump (0xff at idx 0).
 *  - FTBL: cutoff direct set (0x00), modulation duration (0x01–0x7f), filter
 *    mode/resonance set (0x80–0xff), jump (0xff at idx 0).
 *  - STBL: read-only lookup for wave commands.
 */
#pragma once

#include "../synthLib/device.h"
#include "insLoader.h"
#include "resid/sid.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sidLib
{

struct VoiceMacroState
{
    bool     active     = false;
    bool     held       = false;
    bool     sustained  = false;
    uint8_t  midiNote   = 0;
    uint64_t age        = 0;

    // Macro pointers (1-based into bank tables; 0 = stopped/no table)
    uint8_t  wptr       = 0;
    uint8_t  pptr       = 0;
    uint8_t  waveDelay  = 0;  // remaining delay frames before next WTBL step
    uint8_t  pulseTime  = 0;  // remaining frames in current pulsetable duration
    int      pulseSpeed = 0;  // signed delta added each frame during PTBL duration

    // Latched chip values for this voice (so we can rewrite freq on retrigger)
    uint8_t  waveform   = 0x20; // last waveform bits written to $04 (without gate)
    uint16_t pulseWidth = 0x800;
};

class Device : public synthLib::Device
{
public:
    explicit Device(const synthLib::DeviceCreateParams& params);
    ~Device() override;

    float    getSamplerate() const override { return m_sampleRate; }
    bool     isValid()       const override { return true; }

    uint32_t getChannelCountIn()  override { return 0; }
    uint32_t getChannelCountOut() override { return 2; }

    bool     setDspClockPercent(uint32_t)       override { return false; }
    uint32_t getDspClockPercent()         const override { return 100; }
    uint64_t getDspClockHz()              const override { return static_cast<uint64_t>(m_sampleRate); }

#if SYNTHLIB_DEMO_MODE == 0
    bool getState(std::vector<uint8_t>& state, synthLib::StateType type) override;
    bool setState(const std::vector<uint8_t>& state, synthLib::StateType type) override;
#endif

    // Bank/patch management — called by HeadlessProcessor on browse/program-change
    bool loadBankFile(const std::string& filePath);   // .sng / .ins / .sid
    bool selectInstrument(int index);                  // 1-based; 0 = none
    int  getInstrumentCount() const;                   // number of instruments (excl. index 0)
    std::string getInstrumentName(int index) const;    // 1-based
    const std::string& getBankName()    const { return m_bank.name; }
    const std::string& getCurrentPatchName() const { return m_currentPatchName; }

protected:
    void readMidiOut(std::vector<synthLib::SMidiEvent>& midiOut) override;
    void processAudio(const synthLib::TAudioInputs& inputs,
                      const synthLib::TAudioOutputs& outputs, size_t samples) override;
    bool sendMidi(const synthLib::SMidiEvent& ev,
                  std::vector<synthLib::SMidiEvent>& response) override;

private:
    static constexpr int kNumVoices = 3;

    // SID PAL clock + GoatTracker frame rate (50 Hz PAL)
    static constexpr double kClockPAL  = 985248.0;
    static constexpr double kFrameRate = 50.0;

    // Voice register layout: voice i registers start at base 7*i:
    //   $00/$01 freq lo/hi   $02/$03 PW lo/hi   $04 ctrl   $05 AD   $06 SR
    static constexpr uint8_t kVoiceBase[kNumVoices] = { 0x00, 0x07, 0x0E };

    void initFallbackPatch();
    void writeReg(uint8_t reg, uint8_t val);

    void writeFreq (int v, uint8_t midiNote);
    void keyOn     (int v, uint8_t midiNote);
    void keyOff    (int v);

    int  allocVoice();

    void onNoteOn  (uint8_t note, uint8_t vel);
    void onNoteOff (uint8_t note);
    void onAllNotesOff();
    void onSustainPedal(bool down);
    void onPitchBend(int raw14);          // 0..16383, 8192 = centre
    void onModWheel (uint8_t value);      // 0..127

    // Live MIDI CC overrides (apply on top of any patch macro modulation)
    void onFilterCutoff   (uint8_t value); // CC74 — overrides $15/$16 immediately
    void onFilterResonance(uint8_t value); // CC71 — overrides $17 res nibble
    void onPulseWidth     (uint8_t value); // CC75 — sets all 3 voices' PW

    // Macro engine — called once per ~882 samples (50 Hz @ 44100)
    void tickMacros();
    void tickWaveTable (int v);
    void tickPulseTable(int v);
    void tickFilterTable();

    reSID::SID  m_sid;
    float       m_sampleRate = 44100.0f;

    SidBank     m_bank;
    int         m_currentInstrument = 0; // 1-based
    std::string m_currentPatchName;
    std::mutex  m_bankMutex;             // guards bank swap vs audio thread

    std::array<VoiceMacroState, kNumVoices> m_voices{};
    uint64_t    m_ageCounter   = 0;
    bool        m_sustainPedal = false;

    // Global filter macro state (shared across all voices)
    uint8_t     m_filterPtr        = 0;
    uint8_t     m_filterTime       = 0;
    int         m_filterSpeed      = 0;
    uint8_t     m_filterCutoffHi   = 0x80;     // SID $16 (8 MSBs of 11-bit cutoff)
    uint8_t     m_filterCtrl       = 0x00;     // SID $17 — resonance + voice routing
    uint8_t     m_filterTypeBits   = 0x00;     // bits 4-6 of SID $18 (lp/bp/hp)

    // 50 Hz frame scheduling
    double      m_samplesPerFrame  = 0.0;
    double      m_frameAccumulator = 0.0;

    // Live MIDI expression (applied each frame to all sounding voices)
    static constexpr float kVibratoHz      = 6.0f;
    static constexpr float kVibratoDepth   = 0.5f;   // semitones at full mod wheel
    float       m_pitchBendRange = 2.0f;             // semitones (CC22, capped at 48)
    float       m_pitchBendSemis = 0.0f;
    float       m_modWheel       = 0.0f;             // 0..1
    float       m_vibratoPhase   = 0.0f;             // 0..2π

    // Live-CC override flags. Once a CC is touched the corresponding patch
    // macro stops writing to the chip for the remainder of the note.
    // Cleared on note-on so the next note plays the patch as designed.
    bool        m_overrideFilterCutoff = false; // CC74 took control of $15/$16
    bool        m_overrideFilterCtrl   = false; // CC71 took control of $17 + $18 mode bits
    std::array<bool, kNumVoices> m_overridePulseWidth{}; // CC75 took control of voice PW

    std::vector<int16_t> m_intBuf;

    std::atomic<bool> m_shutdown{false};
};

} // namespace sidLib
