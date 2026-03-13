#pragma once

#include "SynthType.h"
#include "jucePluginLib/processor.h"
#include "synthLib/midiTypes.h"
#ifndef CUSTOM
#  include "synthLib/deviceTypes.h"
#endif

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>

namespace akaiLib { class Device; }
namespace openWurliLib { class Device; }
namespace opl3Lib { class Device; }

namespace retromulator
{
    class HeadlessProcessor final : public pluginLib::Processor
    {
    public:
        HeadlessProcessor();
        ~HeadlessProcessor() override = default;

        // ── Synth hot-swap ──────────────────────────────────────────────────
        void setSynthType(SynthType type, const std::string& romPath = {});
        SynthType getSynthType() const { return m_synthType; }

        // ── Preset loading ──────────────────────────────────────────────────
        // Load raw SysEx data. Splits into individual messages, sends message[programIndex].
        // Copies the file to the data folder if a path is provided.
        bool loadPreset(const std::vector<uint8_t>& sysexData,
                        const std::string& sourcePath = {},
                        const std::string& patchName  = {},
                        int programIndex = 0);

        // Load SysEx from a file path. Auto-copies to data folder.
        bool loadPresetFromFile(const std::string& filePath,
                                const std::string& patchName = {},
                                int programIndex = 0);

        // Select a program within the currently loaded bank (0-based).
        // Returns false if index is out of range.
        bool selectProgram(int index);

        // ── Preset export ───────────────────────────────────────────────────
        // Export the current program as a single-patch .syx file.
        // The patch name (from m_programNames or m_patchName) is embedded using
        // the Aura editor name extension (10 ASCII bytes before F7).
        // Returns true on success.
        bool exportCurrentPresetToFile(const std::string& destPath) const;

        // Export the entire loaded bank as a .syx file.
        // Names for all programs are embedded in each message.
        bool exportCurrentBankToFile(const std::string& destPath) const;

        // ── Sound file loading (Akai S1000) ──────────────────────────────────
        // Load a sound file (SFZ/SF2/WAV/FLAC/ZBP/ZBB) into the Akai device.
        // Returns true on success. Populates program names from SF2/ZBP presets.
        bool loadSoundFile(const std::string& filePath);

        // Select a preset within a multi-preset sound file (SF2/ZBP).
        bool selectSoundPreset(int index);

        // Returns the Akai device if the current synth type is AkaiS1000, else nullptr.
        akaiLib::Device* getAkaiDevice() const;

        // Returns the OpenWurli device if the current synth type is OpenWurli, else nullptr.
        openWurliLib::Device* getOpenWurliDevice() const;

        // Returns the OPL3 device if the current synth type is OPL3, else nullptr.
        opl3Lib::Device* getOpl3Device() const;

        // ── Program bank accessors ──────────────────────────────────────────
        // m_bankStride: number of raw sysex messages per logical program (1 for most
        // synths; >1 for JE-8086 where each performance = several sub-messages).
        int getProgramCount()   const { return static_cast<int>(m_bankMessages.size()) / m_bankStride; }
        int getCurrentProgram() override { return m_currentProgram; }

        // ── Data folder helpers ─────────────────────────────────────────────
        static std::string getDataFolder();
        static std::string getSynthDataFolder(SynthType type);
        static std::string getLastLoadFolder(SynthType type);
        static void        setLastLoadFolder(SynthType type, const std::string& folder);

        // ── pluginLib::Processor pure virtuals ──────────────────────────────
        synthLib::Device* createDevice() override;
        pluginLib::Controller* createController() override;

        // ── State accessors for editor ──────────────────────────────────────
        const std::string& getRomPath()       const { return m_romPath; }
        const std::string& getSysexFilePath() const { return m_sysexFilePath; }
        const std::string& getPatchName()     const { return m_patchName; }
#ifndef CUSTOM
        // Raw device error — available in GPL builds only.
        // Use isFirmwareMissing() / hasDeviceError() from proprietary code.
        synthLib::DeviceError getDeviceError() const { return m_deviceError; }
#endif
        bool isFirmwareMissing() const;
        bool hasDeviceError() const;

        // Returns true if a valid ROM can be found for the given synth type.
        // Wraps each synth-specific ROM loader — keeps GPL headers out of the editor.
        static bool isRomValid(SynthType type);

        // Registers an additional directory for all ROM loaders to search.
        static void addRomSearchPath(const std::string& path);

        const std::vector<std::string>& getProgramNames() const { return m_programNames; }

        // ── juce::AudioProcessor overrides ──────────────────────────────────
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        const juce::String getName() const override { return "Retromulator"; }

        // State persistence (DAW save/load)
        void getStateInformation(juce::MemoryBlock& destData) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        // Called each audio block — used to resend bank message after DSP boot delay
        void processBpm(float _bpm) override;

        int getSavedEditorWidth()  const { return m_savedEditorWidth; }
        int getSavedEditorHeight() const { return m_savedEditorHeight; }
        void saveEditorSize(int w, int h) { m_savedEditorWidth = w; m_savedEditorHeight = h; }

        // Set by setStateInformation so the editor can apply the restored size
        // if it was created before the DAW called setStateInformation.
        bool consumeEditorSizeDirty()
        {
            const bool v = m_editorSizeDirty;
            m_editorSizeDirty = false;
            return v;
        }

    private:
        SynthType   m_synthType = SynthType::None;
        std::string m_romPath;

        // GUI size saved/restored across DAW sessions
        int  m_savedEditorWidth  = 0;
        int  m_savedEditorHeight = 0;
        bool m_editorSizeDirty   = false; // true after setStateInformation restores a size

        std::string m_sysexFilePath;   // path to the last loaded sysex file (in data folder)
        std::string m_patchName;       // human-readable patch name
        std::vector<uint8_t> m_sysexData; // raw sysex bytes of the loaded file

        // Split messages from the loaded bank; index into it for program selection.
        std::vector<synthLib::SysexBuffer> m_bankMessages;
        std::vector<std::string> m_programNames; // extracted name for each program slot
        int m_currentProgram = 0;
        int m_bankStride     = 1; // messages per logical program (>1 for JE-8086)

        // Send stride messages starting at index*stride to the device.
        void sendBankMessage(int index);

        // Copy a sysex file to the synth data folder. Returns the destination path.
        std::string copySysexToDataFolder(const std::string& sourcePath);

    public:
        // Detect whether a set of sysex messages are Virus ABC or TI presets.
        // Returns VirusABC, VirusTI, or None if not Virus sysex.
        static SynthType detectVirusType(const std::vector<synthLib::SysexBuffer>& messages);

        // Copy a sysex file to the *other* Virus variant's data folder.
        // Returns the destination path, or empty on failure.
        static std::string copySysexToFolder(const std::string& sourcePath, SynthType targetType);

        // Set when a bank message needs to be re-sent once the DSP has warmed up.
        // Consumed by processBpm (called each audio block) after setSynthType.
        // m_deviceBooted: once true, the boot-delay resend is never re-armed.
        std::atomic<bool> m_pendingResend{false};
        int m_resendBlocksRemaining = 0;
        bool m_deviceBooted = false;
    };
}
