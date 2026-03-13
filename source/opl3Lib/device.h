/**
 * OPL3 Device — Retromulator adapter for Nuked OPL3 (Nuke.YKT, LGPL 2.1)
 *
 * Features:
 *  - 18-channel OPL3 (YMF262) synthesis via Nuked OPL3 v1.8
 *  - SBI patch loading (11-register OPL2 format, loads into any channel)
 *  - Bank navigation via folder hierarchy: bank combo = subfolder, prog combo = .sbi files
 *  - Full MIDI: note on/off, pitch bend (±2 semitones), sustain, all-notes-off
 *  - Up to 18 simultaneous voices; oldest-voice stealing
 *  - No ROM required
 */
#pragma once

#include "../synthLib/device.h"
#include "opl3.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace opl3Lib
{

// ── SBI patch data ────────────────────────────────────────────────────────────
struct SbiPatch
{
    uint8_t modTVSKSRMult;   // 0x20 modulator
    uint8_t carTVSKSRMult;   // 0x20 carrier
    uint8_t modKSLTL;        // 0x40 modulator
    uint8_t carKSLTL;        // 0x40 carrier
    uint8_t modARDR;         // 0x60 modulator
    uint8_t carARDR;         // 0x60 carrier
    uint8_t modSLRR;         // 0x80 modulator
    uint8_t carSLRR;         // 0x80 carrier
    uint8_t modWF;           // 0xE0 modulator
    uint8_t carWF;           // 0xE0 carrier
    uint8_t fbAlg;           // 0xC0 feedback/algorithm
    std::string name;

    // Load from raw SBI file bytes. Returns true on success.
    static bool loadFromBytes(const uint8_t* data, size_t size, SbiPatch& out);
};

// ── Voice slot ────────────────────────────────────────────────────────────────
struct OplVoice
{
    bool  active     = false;
    bool  held       = false;   // key is physically down
    bool  sustained  = false;   // key up but sustain pedal held
    uint8_t  midiNote = 0;
    uint8_t  channel  = 0;      // OPL3 channel index (0–17)
    uint64_t age      = 0;
    float pitchBendSemitones = 0.0f;
};

// ── Device ────────────────────────────────────────────────────────────────────
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

    // Load a patch from an SBI file path. Called by the processor on program change.
    bool loadSbi(const std::string& filePath);

    // Access current patch name (set by loadSbi).
    const std::string& getPatchName() const { return m_patchName; }

protected:
    void readMidiOut(std::vector<synthLib::SMidiEvent>& midiOut) override;
    void processAudio(const synthLib::TAudioInputs& inputs,
                      const synthLib::TAudioOutputs& outputs, size_t samples) override;
    bool sendMidi(const synthLib::SMidiEvent& ev,
                  std::vector<synthLib::SMidiEvent>& response) override;

private:
    // OPL3 register helpers
    void writeReg(uint16_t reg, uint8_t val);
    void applyPatch(uint8_t oplCh);          // apply m_currentPatch to channel oplCh
    void keyOn (uint8_t oplCh, uint8_t note, float pitchBend);
    void keyOff(uint8_t oplCh);
    void setPitch(uint8_t oplCh, uint8_t note, float semitones);

    // MIDI dispatch
    void onNoteOn (uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t note);
    void onAllNotesOff();
    void onPitchBend(float semitones);

    // Voice allocation
    uint8_t allocVoice(uint8_t midiNote);

    // Frequency helpers
    static void noteToFnumBlock(uint8_t note, float semitones,
                                uint16_t& fnum, uint8_t& block);

    // OPL3 slot offset for channel ch: mod slot = slotBase(ch), car = slotBase(ch)+3
    static uint8_t slotBase(uint8_t ch);

    static constexpr int    kNumChannels = 18;
    static constexpr float  kPitchBendRange = 2.0f; // ±2 semitones

    opl3_chip   m_chip{};
    float       m_sampleRate = 44100.0f;
    SbiPatch    m_currentPatch{};
    std::string m_patchName;

    std::array<OplVoice, kNumChannels> m_voices{};
    uint64_t m_ageCounter = 0;

    bool  m_sustainPedal = false;
    float m_pitchBend    = 0.0f;  // current global pitch bend in semitones

    // Scratch buffer for int16 OPL output before float conversion
    std::vector<int16_t> m_intBuf;

    std::atomic<bool> m_shutdown{false};
};

} // namespace opl3Lib
