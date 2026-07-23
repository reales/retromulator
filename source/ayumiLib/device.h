/**
 * Ayumi Device — AY-3-8910 / YM2149 PSG synth for Retromulator.
 *
 * Chip emulation: Ayumi (Peter Sovietov, MIT) — bit-exact PSG.
 * Voice/modulation engine: port of Ym2149Synth (Timothy Lamb, GPLv3), with the
 * CC1..CC11 control mapping from that project.
 *
 * Design:
 *  - 3-voice polyphony across YM channels A/B/C. Envelope + noise are chip-global
 *    (as on real hardware); synth types that use them share those settings.
 *  - CC1..CC11 apply globally to all three voices.
 *  - Two firmware clocks run off the audio sample clock: events @ 1 kHz,
 *    soft-PWM @ 22050 Hz. Ayumi renders one stereo sample per output frame.
 *  - Engine switch (YM2149 / AY-3-8910) selects the DAC table.
 *  - Patches are 11-byte CC snapshots (.ay files) browsed from a single folder.
 *  - No ROM required.
 */
#pragma once

#include "../synthLib/device.h"
#include "synthVoice.h"

extern "C" {
#include "ayumi.h"
}

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ayumiLib
{

// 11 CC-controlled parameters (CC1..CC11) — the .ay patch payload.
struct AyPatch
{
    std::array<uint8_t, 11> cc{};   // index 0 == CC1 ... index 10 == CC11
    std::string name;
};

class Device : public synthLib::Device, private IYmSink
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
    uint64_t getDspClockHz()              const override { return kYmClockHz; }

#if SYNTHLIB_DEMO_MODE == 0
    bool getState(std::vector<uint8_t>& state, synthLib::StateType type) override;
    bool setState(const std::vector<uint8_t>& state, synthLib::StateType type) override;
#endif

    // Load a .ay patch (11 CC bytes). Called by the processor on program change.
    bool loadPatchFile(const std::string& filePath);
    const std::string& getPatchName() const { return m_patchName; }

    // Ordered list of .ay file paths in the current bank folder. MIDI Program
    // Change N selects the Nth entry (0-based). Pushed by the processor.
    void setProgramList(std::vector<std::string> paths) { m_programList = std::move(paths); }
    const std::vector<std::string>& getProgramList() const { return m_programList; }

    // A MIDI Program Change latches its index here (audio thread). The processor
    // polls this on its message-thread timer and performs the actual file load,
    // keeping disk I/O off the audio thread. Returns -1 if none pending.
    int takePendingProgramChange() { return m_pendingProgram.exchange(-1); }

    // Engine (chip variant) switch: 0 = YM2149, 1 = AY-3-8910. The actual
    // reconfigure (which memsets the emulator) is deferred to the audio thread.
    void setEngine(int isAY);
    int  getEngine() const { return m_pendingIsYM.load() ? 0 : 1; }

protected:
    void readMidiOut(std::vector<synthLib::SMidiEvent>& midiOut) override;
    void processAudio(const synthLib::TAudioInputs& inputs,
                      const synthLib::TAudioOutputs& outputs, size_t samples) override;
    bool sendMidi(const synthLib::SMidiEvent& ev,
                  std::vector<synthLib::SMidiEvent>& response) override;

    // IYmSink — YM register writes routed into an internal YM register model,
    // then translated to Ayumi.
    void setTone(uint8_t voice, uint16_t value) override;
    void setVolume(uint8_t voice, uint8_t value) override;
    void setNoise(uint8_t voice, uint8_t value) override;
    void setEnv(uint8_t voice, uint8_t value) override;
    void setEnvShape(uint8_t cont, uint8_t attack, uint8_t alt, uint8_t hold) override;
    void setNote(uint8_t voice, float value) override;

private:
    void configureAyumi();
    void applyEngineIfPending();                       // audio thread only
    void applyCC(uint8_t cc, uint8_t value);          // cc is 1..11
    void onNoteOn(uint8_t note, uint8_t vel);
    void onNoteOff(uint8_t note);
    void onAllNotesOff();
    void onPitchBend(int value14);

    // Voice allocation across the 3 YM channels.
    int  allocVoice(uint8_t note);

    // YM register model → Ayumi.
    void ymWrite(uint8_t reg, uint8_t value);
    void pushMixer();

    static constexpr int    kNumVoices  = 3;
    static constexpr double kYmClockHz  = 2000000.0; // 2 MHz
    static constexpr double kEventHz    = 1000.0;    // updateEvents rate
    static constexpr double kSoftHz     = 22050.0;   // updateSoftsynth rate

    ayumi       m_ay{};
    bool        m_isYM = true;
    std::atomic<bool> m_pendingIsYM{true};   // engine request (audio thread applies)
    float       m_sampleRate = 44100.0f;

    std::array<SynthVoice, kNumVoices> m_voices{};

    // Note→voice bookkeeping for polyphony (oldest-steal).
    struct VoiceSlot { bool active = false; uint8_t note = 0; uint64_t age = 0; };
    std::array<VoiceSlot, kNumVoices> m_slots{};
    uint64_t m_ageCounter = 0;

    // Last CC values for state save + re-broadcast.
    std::array<uint8_t, 11> m_cc{};
    AyPatch     m_patch{};
    std::string m_patchName;

    std::vector<std::string> m_programList; // .ay paths for MIDI Program Change
    std::atomic<int>         m_pendingProgram{-1}; // latched PC index from audio thread

    // Tick accumulators.
    double m_eventAcc = 0.0;
    double m_softAcc  = 0.0;

    // YM register file (0x00..0x0D) as maintained by the firmware YM2149 class.
    std::array<uint8_t, 16> m_ymRegs{};

    std::atomic<bool> m_shutdown{false};
};

} // namespace ayumiLib
