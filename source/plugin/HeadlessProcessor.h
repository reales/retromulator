#pragma once

#include "SynthType.h"
#include "PlaylistOrder.h"
#include "jucePluginLib/processor.h"
#include "synthLib/midiTypes.h"
#ifndef CUSTOM
#  include "synthLib/deviceTypes.h"
#endif

#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>

namespace akaiLib { class Device; }
namespace openWurliLib { class Device; }
namespace opl3Lib { class Device; }
namespace sidLib { class Device; }
namespace ayumiLib { class Device; }
namespace emu88Lib { class HardwareDevice; }
namespace trackerLib { class Device; }

namespace retromulator
{
    class ParameterPool;

    class HeadlessProcessor final : public pluginLib::Processor
    {
    public:
        HeadlessProcessor();
        ~HeadlessProcessor() override;


        // ── Synth hot-swap ──────────────────────────────────────────────────
        void setSynthType(SynthType type, const std::string& romPath = {});
        // Async version: boots the device on a background thread, then calls
        // onComplete on the message thread. GUI stays responsive during boot.
        void setSynthTypeAsync(SynthType type, const std::string& romPath,
                               std::function<void()> onComplete);

        // Standalone only: 56k cores need a 512 sample buffer minimum, others use the device default.
        void applyStandaloneBufferSize();
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

        // Export the loaded Virus bank converted to a different model version.
        // targetVersion: 'A', 'B', or 'C'.  Remaps C-only parameters (analog
        // filters, saturation) to safe equivalents for the older model.
        // Returns the number of patches converted, or -1 on error.
        int exportConvertedVirusBank(const std::string& destPath, char targetVersion) const;

        // ── Sound file loading (Akai S1000) ──────────────────────────────────
        // Load a sound file (SFZ/SF2/WAV/FLAC/ZBP/ZBB) into the Akai device.
        // Returns true on success. Populates program names from SF2/ZBP presets.
        bool loadSoundFile(const std::string& filePath);

        // Load an Akai ISO/BIN/CUE disk image. Scans partitions and populates
        // program names with "A: ProgramName" labels. Returns true on success.
        bool loadAkaiIso(const std::string& filePath);

        // Select a program from the loaded Akai ISO.
        bool selectAkaiIsoPreset(int index);

        // Select a preset within a multi-preset sound file (SF2/ZBP).
        bool selectSoundPreset(int index);

        // Returns the Akai device if the current synth type is AkaiS1000, else nullptr.
        akaiLib::Device* getAkaiDevice() const;

        // Returns the OpenWurli device if the current synth type is OpenWurli, else nullptr.
        openWurliLib::Device* getOpenWurliDevice() const;

        // Returns the OPL3 device if the current synth type is OPL3, else nullptr.
        opl3Lib::Device* getOpl3Device() const;

        // Returns the SID device if the current synth type is SID, else nullptr.
        sidLib::Device* getSidDevice() const;

        // Returns the Ayumi device if the current synth type is Ayumi, else nullptr.
        ayumiLib::Device* getAyumiDevice() const;

        // 88emu (SC-88 family) device, or nullptr if another core is loaded.
        emu88Lib::HardwareDevice* getEmu88Device() const;
        // Board currently booted, as emu88Lib::DeviceModel, or -1.
        int getEmu88Model() const;

        // The 88emu part the editor edits, 0-15 (part 10 of the panel is index 9, the
        // drum part). Parameters and program changes address this part; the device stays
        // multitimbral on all 16 channels regardless.
        int  getEmu88Part() const;
        void setEmu88Part(int part);
        // Replay every part's saved program onto the freshly booted board and select
        // the part that was being edited. Used when restoring plugin state.
        void restoreEmu88Parts(int part);
        // Put every part back on program 0, matching a GS/GM reset.
        void resetEmu88Parts();
        // Send each part's cached program to the board on its own channel.
        void sendEmu88PartPrograms();
        // Set one part's program and send it to the board. Used by the per-part program
        // parameters, so automating a part does not depend on the Part selector.
        void setEmu88PartProgram(int part, int program);
        // Track a program change the board already received (MIDI file, external gear):
        // updates the cached view only, never echoes back to the device.
        void onEmu88ProgramChange(int part, int program);
        // CC0 / CC32 on a part, so a restore can put the variation back, not just the tone.
        void onEmu88BankSelect(int part, int cc, int value);
        // Select a variation bank on a part and send it, followed by the part's program:
        // a bank select only takes effect on the next program change.
        void setEmu88PartBank(int part, int cc, int value);
        // Cached CC0 (cc 0) or CC32 (cc 32) of a part.
        int getEmu88PartBank(int part, int cc) const;
        // True while the part is a drum part: part 10 by default, or any part a GS
        // "Use For Rhythm Part" message has switched over.
        bool isEmu88PartRhythm(int part) const;
        // Switch a part between melodic and drums, sending the GS message that does it.
        // value: 0 melodic, 1-2 a drum map.
        void setEmu88PartRhythm(int part, int value);
        // Track a GS sysex the board already received. Only the messages that move state
        // the plugin caches are read; everything else passes through untouched.
        void onEmu88Sysex(const std::vector<uint8_t>& data);
        // Name of a program on a part, from the loaded ROM: kits for part 10, tones
        // otherwise. Empty if the ROM set is missing or the index has no entry.
        std::string getEmu88ProgramName(int part, int program) const;
        // Queue a raw SysEx message for the device.
        void sendSysex(const std::vector<uint8_t>& data);

        // Play a short built-in multitimbral phrase so the loaded board can be auditioned
        // without external MIDI. Sequenced on the audio thread with sample offsets.
        void triggerDemoSequence();

        // ── MIDI file playback ──────────────────────────────────────────────
        // Transport notes, matching the on-screen keyboard's octave numbering where
        // middle C (60) is C4, so these are C1 and D1.
        static constexpr int kMidiPlayNote = 24;
        static constexpr int kMidiStopNote = 26;
        static constexpr int kMidiPrevNote = 25;   // C#1, playlist
        static constexpr int kMidiNextNote = 27;   // D#1, playlist

        // Parse and keep a MIDI file. The bytes are held so the song survives in the
        // plugin state; .mid files are small enough to travel with the session.
        bool loadMidiFile(std::vector<uint8_t>&& data, const std::string& fileName);
        bool hasMidiFile() const { return !m_midiSongEvents.empty(); }
        std::string getMidiFileName() const { return m_midiFileName; }
        // Imported songs are copied into the 88emu data folder and listed here, newest
        // first. Copies, not the original paths: an iOS pick is security-scoped and may
        // not be reachable on the next launch.
        static std::vector<std::string> getRecentMidiFiles();
        static void addRecentMidiFile(const std::string& fileName);
        bool loadRecentMidiFile(const std::string& fileName);

        // Playlist: full paths, kept in the plugin state. More than one entry is playlist
        // mode: a song that ends starts the next one, and C#1 and D#1 step through it.
        // Folders are scanned for .mid files and .m3u/.m3u8 files are expanded. append
        // adds after the current entries and keeps the song loaded. Nothing starts playing.
        bool openMidiPaths(const std::vector<std::string>& paths, bool append = false);
        void clearMidiPlaylist();
        const std::vector<std::string>& getMidiPlaylist() const { return m_midiPlaylist; }
        int  getMidiPlaylistIndex() const { return m_midiPlaylistIndex; }
        bool isMidiPlaylistMode() const { return m_midiPlaylist.size() > 1; }
        // Loads that entry and plays it. Entries that do not load are skipped forward.
        bool playMidiPlaylistIndex(int index);
        bool stepMidiPlaylist(int delta);
        bool exportMidiPlaylist(const juce::File& m3uFile) const;
        // Same meaning and storage as the tracker's options, kept apart from them.
        bool getMidiShuffle() const { return m_midiShuffle; }
        void setMidiShuffle(bool enabled);
        bool getMidiStopAtEnd() const { return m_midiStopAtEnd; }
        void setMidiStopAtEnd(bool enabled);

        // ── Offline render ──────────────────────────────────────────────────
        // Renders the loaded song on a background thread, 48 kHz stereo: WAV at 24 bit
        // or MP3 at 320 kbps. Live audio is suspended for the duration: there is one
        // board, and it cannot be at two transport positions at once.
        // destUrl is what the save dialog returned. The render writes to a file the app
        // owns and copies out at the end: a chosen iOS location is security-scoped and
        // cannot be written to directly.
        enum class RenderFormat : uint8_t { Wav, Mp3 };
        bool startMidiRender(const juce::URL& destUrl, RenderFormat format);
        // Every playlist entry into that folder, named "01 song.wav" in playlist order.
        bool startMidiPlaylistRender(const juce::File& folder, RenderFormat format);
        void cancelMidiRender();
        bool isMidiRendering() const { return m_renderActive.load(); }
        // 0..1, for the editor's progress display.
        float getMidiRenderProgress() const { return m_renderProgress.load(); }
        // Both are safe from any thread; the audio thread picks the request up.
        void playMidiFile();
        void stopMidiFile();
        bool isMidiFilePlaying() const { return m_midiPlayState.load() != MidiPlayState::Stopped; }
        // 0..1 activity for one channel, for the editor's meter. The audio thread owns
        // the decay, so reading this is free and the fall rate is independent of how
        // often the editor repaints.
        float getMidiChannelLevel(int channel) const;
        // 16 MIDI channels, or one bar per module channel for the tracker (1..16).
        int getMidiMeterBars() const;

        // Message thread, periodic: mirrors the edited part and program into their
        // host parameters so automation lanes show the real state.
        void pollEmu88Params();

        // Ayumi chip variant: 0 = YM2149, 1 = AY-3-8910. No-op if not Ayumi.
        int  getAyumiEngine() const;
        void setAyumiEngine(int isAY);

        // Push the sorted .ay file list of the given folder to the Ayumi device
        // so MIDI Program Change 0..N selects file 1..N+1. No-op if not Ayumi.
        void updateAyumiProgramList(const std::string& bankFolder);

        // Poll a latched MIDI Program Change from the Ayumi device and load the
        // corresponding .ay file on the message thread. Call periodically from the
        // editor timer. No-op if not Ayumi or nothing pending.
        void pollAyumiProgramChange();

        // ── Program bank accessors ──────────────────────────────────────────
        // m_bankStride: number of raw sysex messages per logical program (1 for most
        // synths; >1 for JE-8086 where each performance = several sub-messages).
        int getProgramCount()   const
        {
            // SID has no sysex bank; instrument count lives in m_programNames.
            if(m_bankMessages.empty() && !m_programNames.empty())
                return static_cast<int>(m_programNames.size());
            return static_cast<int>(m_bankMessages.size()) / m_bankStride;
        }
        int getCurrentProgram() override { return m_currentProgram; }
        int getNumPrograms() override { return std::max(1, getProgramCount()); }
        void setCurrentProgram(int index) override
        {
            if(index != m_currentProgram)
                selectProgram(index);
        }
        const juce::String getProgramName(int index) override
        {
            if(index >= 0 && index < static_cast<int>(m_programNames.size()))
                return m_programNames[static_cast<size_t>(index)];
            return index >= 0 && index < getProgramCount() ? juce::String("Program ") + juce::String(index + 1) : juce::String();
        }

        // 128 fixed host parameters, rebound per core. Null until the constructor finishes.
        ParameterPool* getParameterPool() const { return m_paramPool.get(); }

        // ── Data folder helpers ─────────────────────────────────────────────
        static std::string getDataFolder();

        // Creates a data subfolder. A 0-byte plain file at that path (left by a zip
        // directory entry written as a file) is removed first, since it blocks the folder.
        static bool ensureDataDirectory(const juce::File& dir);
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

        // Fill m_programNames with the booted board's capital tones read from its ROM.
        void loadEmu88ToneNames();

        // Registers an additional directory for all ROM loaders to search.
        static void addRomSearchPath(const std::string& path, bool recursive = false);

        const std::vector<std::string>& getProgramNames() const { return m_programNames; }

        const std::string& getAkaiBrowseFolder() const { return m_akaiBrowseFolder; }
        void setAkaiBrowseFolder(const std::string& f)  { m_akaiBrowseFolder = f; }

        bool isAkaiIsoMode() const { return m_akaiIsoMode; }
        const std::string& getAkaiIsoPath() const { return m_akaiIsoPath; }

        int  getAkaiSliceCount() const     { return m_akaiSliceCount; }
        void setAkaiSliceCount(int count)  { m_akaiSliceCount = count; }
        int  getAkaiTuneCents() const      { return m_akaiTuneCents; }
        void setAkaiTuneCents(int cents)   { m_akaiTuneCents = cents; }

        // ── juce::AudioProcessor overrides ──────────────────────────────────
        bool hasEditor() const override { return true; }
        juce::AudioProcessorEditor* createEditor() override;

        const juce::String getName() const override { return "Retromulator"; }

        // ── Trackermeister (tracker module player) ─────────────────────────
        // The device owns the transport: note 12 plays, 14 stops, 13 and 15 step a
        // playlist, and 24 upward start the song at order (note - 24). These mirror that
        // for the editor.
        trackerLib::Device* getTrackerDevice() const;
        bool loadTrackerModule(std::vector<uint8_t>&& data, const std::string& fileName);
        bool hasTrackerModule() const;
        std::string getTrackerModuleName() const { return m_trackerFileName; }
        // Modules are read from where they are; only the loaded one travels, in the plugin state.
        bool loadTrackerModuleFile(const std::string& filePath, bool addToRecent = true);
        // Playlist: full paths, kept in the plugin state. More than one entry is playlist
        // mode, where a song stops at its end and the next one starts, wrapping around
        // unless Stop at Playlist End is on.
        // Folders are scanned for modules and .m3u/.m3u8 files are expanded. Nothing
        // starts playing; false if no entry could be loaded.
        bool openTrackerPaths(const std::vector<std::string>& paths);
        const std::vector<std::string>& getTrackerPlaylist() const { return m_trackerPlaylist; }
        int  getTrackerPlaylistIndex() const { return m_trackerPlaylistIndex; }
        bool isTrackerPlaylistMode() const { return m_trackerPlaylist.size() > 1; }
        // Loads that entry and plays it. Entries that do not load are skipped forward.
        bool playTrackerPlaylistIndex(int index);
        bool stepTrackerPlaylist(int delta);
        bool exportTrackerPlaylist(const juce::File& m3uFile) const;
        // .m3u or .m3u8
        static bool isPlaylistFile(const juce::File& file);
        // Full paths, newest first.
        static std::vector<std::string> getRecentTrackerModules();
        static void addRecentTrackerModule(const std::string& filePath);
        void playTracker();
        void stopTracker();
        // A host playhead tempo or incoming MIDI clock. The standalone app only has the latter.
        bool hasTrackerHostTempo() const;
        bool isTrackerPlaying() const;
        // Scales the song so its initial BPM lands on the host tempo.
        bool getTrackerTempoSync() const { return m_trackerTempoSync; }
        void setTrackerTempoSync(bool enabled);
        // Off: a single song loops and a playlist wraps around. On: both stop at the end.
        // The last choice is the default for new instances, the plugin state keeps its own.
        bool getTrackerStopAtEnd() const { return m_trackerStopAtEnd; }
        void setTrackerStopAtEnd(bool enabled);
        // Previous, next and the end of a song follow a shuffled order. Saved like Stop at End.
        bool getTrackerShuffle() const { return m_trackerShuffle; }
        void setTrackerShuffle(bool enabled);
        // Same thread, progress and cancel as the MIDI render. 512 tap sinc, song end stops it.
        bool startTrackerRender(const juce::URL& destUrl, RenderFormat format);
        // Every playlist entry into that folder, named "01 song.wav" in playlist order.
        bool startTrackerPlaylistRender(const juce::File& folder, RenderFormat format);
        void processBpm(float bpm) override;

        // State persistence (DAW save/load)
        void getStateInformation(juce::MemoryBlock& destData) override;
        void setStateInformation(const void* data, int sizeInBytes) override;
        // Logic's AU host calls these as well as the pair above, and the base class
        // answers them from the device's own state. This processor keeps all of its
        // state in the block above, so the per-program pair would hand the device a
        // foreign payload on top of the board that was just booted. No-ops.
        void getCurrentProgramStateInformation(juce::MemoryBlock&) override {}
        void setCurrentProgramStateInformation(const void*, int) override {}

        // The boot-settled replay (bank message, 88emu part programs, touched params).
        // Driven from processBlock, not processBpm: the host only calls processBpm when it
        // supplies a playhead with a BPM, and the standalone app has no playhead at all,
        // so anything deferred to it never ran there.
        void processDeferredResend();
        // Watches program changes so the cached per-part programs follow the board.
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

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
        std::unique_ptr<ParameterPool> m_paramPool;

        // GUI size saved/restored across DAW sessions and settings.xml
        int  m_savedEditorWidth  = 0;
        int  m_savedEditorHeight = 0;
        bool m_editorSizeDirty   = false; // true after setStateInformation restores a size

        // Board this instance's session asked for, -1 when it did not name one. Held per
        // instance so a restore never writes SynthFactory's shared board: an AU host
        // restores several instances at once and they would overwrite each other's.
        int m_restoredEmu88Model = -1;

        void loadEditorSizeFromSettings();
        void saveEditorSizeToSettings(int w, int h);

        std::string m_sysexFilePath;   // path to the last loaded sysex file (in data folder)
        std::string m_patchName;       // human-readable patch name
        std::string m_akaiBrowseFolder; // Akai browse-folder path (empty = not in browse mode)
        int m_akaiSliceCount = 0;       // 0 = root play, 4/8/16/32 = auto-sliced
        int m_akaiTuneCents  = 0;       // CC20 global tuning in cents
        bool m_akaiIsoMode   = false;   // true when an Akai ISO is loaded
        std::string m_akaiIsoPath;      // path to loaded ISO/BIN/CUE file
        std::vector<uint8_t> m_sysexData; // raw sysex bytes of the loaded file

        // Split messages from the loaded bank; index into it for program selection.
        std::vector<synthLib::SysexBuffer> m_bankMessages;
        std::vector<std::string> m_programNames; // extracted name for each program slot
        int m_currentProgram = 0;
        int m_bankStride     = 1; // messages per logical program (>1 for JE-8086)

        // 88emu is multitimbral: each part keeps its own tone, so switching parts must
        // restore that part's program rather than reset the view to 0.
        std::array<int, 16> m_emu88PartPrograms{};
        // Bank select per part, as last seen on the wire. A GS variation tone is a bank,
        // not a program, so restoring the program alone brings back the capital tone.
        std::array<int, 16> m_emu88PartBankMsb{};
        std::array<int, 16> m_emu88PartBankLsb{};
        // GS "Use For Rhythm Part" (DT1 at 40 1x 15) per part: 0 melodic, 1-2 a drum map.
        // Part 10 boots as a drum part, the rest melodic, and a song may move that around,
        // so the tone list follows this rather than assuming channel 10.
        // Index 9 is part 10, which boots as the drum part on every GS board.
        std::array<int, 16> m_emu88PartRhythm{0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0};

        // Demo sequence. Armed from the message thread, consumed by the audio thread.
        // The programs the borrowed parts had when it was armed, one per demo channel,
        // snapshotted so the audio thread never reads the message thread's cache.
        // One entry per channel in g_demoChannels, in the same order.
        std::array<int, 5> m_demoSeqRestore{};
        std::atomic<bool> m_demoSeqArmed{false};
        bool     m_demoSeqRunning = false;
        uint64_t m_demoSeqSamples = 0;   // samples elapsed since the sequence started
        size_t   m_demoSeqIndex   = 0;
        void processDemoSequence(uint32_t numSamples, double sampleRate);
        // Hands the parts the demo borrowed back to their cached programs.
        void restoreDemoSequenceParts(uint32_t offset);

        // ── MIDI file playback ──────────────────────────────────────────────
        // The file's own bytes, kept for the plugin state, and the parsed events the
        // audio thread plays. Both are written on the message thread only while
        // playback is stopped, so the audio thread can read them without a lock.
        std::vector<uint8_t> m_midiFileData;
        std::string          m_midiFileName;
        struct MidiSongEvent
        {
            double seconds;
            std::vector<uint8_t> bytes;
            uint8_t port;
        };
        std::vector<MidiSongEvent> m_midiSongEvents;

        enum class MidiPlayState : uint8_t { Stopped, LeadIn, Playing };
        std::atomic<MidiPlayState> m_midiPlayState{MidiPlayState::Stopped};
        std::atomic<bool> m_midiPlayRequest{false};
        std::atomic<bool> m_midiStopRequest{false};
        // Negative while the lead-in runs, so a song's opening reset has time to settle
        // before its first event. See resetMidiModule.
        double m_midiPlayPos   = 0.0;
        size_t m_midiEventIndex = 0;
        void processMidiFile(uint32_t numSamples, double sampleRate);
        // Per-channel note activity for the editor's meter, 0..1 in fixed point so the
        // whole row is lock-free. Written by the audio thread only.
        std::array<std::atomic<uint16_t>, 16> m_midiChannelLevels{};

        // Offline render, on its own thread with live audio suspended.
        std::unique_ptr<std::thread> m_renderThread;
        std::atomic<bool>  m_renderActive{false};
        std::atomic<bool>  m_renderCancel{false};
        std::atomic<float> m_renderProgress{0.0f};
        void renderMidiToWav(const juce::URL& destUrl, RenderFormat format);
        void renderMidiPlaylist(const juce::File& folder, RenderFormat format);
        void renderMidiSong(const std::vector<MidiSongEvent>& events, const juce::URL& destUrl, RenderFormat format,
                            float progressStart, float progressSpan);
        void beginMidiRender();
        void endMidiRender();
        void renderTrackerToWav(const juce::URL& destUrl, RenderFormat format);
        void renderTrackerPlaylist(const juce::File& folder, RenderFormat format);
        // One song of a render. The caller brackets it with begin/endTrackerRender.
        void renderTrackerSong(const juce::URL& destUrl, RenderFormat format, float progressStart, float progressSpan);
        bool beginTrackerRender();
        void endTrackerRender();
        bool m_trackerRenderSync = false;
        // Encodes if asked, then copies the finished render out to the picked location.
        void deliverRender(const juce::File& wavFile, const juce::URL& destUrl, RenderFormat format);

        std::vector<uint8_t> m_trackerFileData;
        std::string          m_trackerFileName;
        bool                 m_trackerTempoSync = false;
        bool                 m_trackerStopAtEnd = false;
        bool                 m_trackerShuffle = false;
        void reloadTrackerModule();
        void applyTrackerStopAtEnd();

        std::vector<std::string> m_trackerPlaylist;
        int                      m_trackerPlaylistIndex = 0;
        PlaylistOrder            m_trackerOrder;
        // index < 0: the first entry of the play order, random when shuffled.
        void setTrackerPlaylist(std::vector<std::string>&& paths, int index);
        // A position in the play order. Entries that fail to load are skipped in direction.
        bool loadTrackerPlaylistPosition(int position, int direction);
        std::vector<std::string> m_midiPlaylist;
        int                      m_midiPlaylistIndex = 0;
        PlaylistOrder            m_midiOrder;
        bool                     m_midiShuffle = false;
        bool                     m_midiStopAtEnd = false;
        std::atomic<bool>        m_midiPlaylistActive{false};   // read by the audio thread
        std::atomic<bool>        m_midiSongFinished{false};
        std::atomic<int>         m_midiPlaylistStep{0};
        void setMidiPlaylist(std::vector<std::string>&& paths, int index);
        bool loadMidiPlaylistPosition(int position, int direction);

        // Message thread: moves either playlist on when its song ends or a note asks.
        struct PlaylistTimer;
        std::unique_ptr<PlaylistTimer> m_playlistTimer;
        void onPlaylistTimer();
        void updatePlaylistTimer();
        // JUCE has no MP3 encoder (MP3AudioFormat::createWriterFor is a stub), so the
        // rendered WAV is converted by libmp3lame, compiled in. See Mp3Encoder.h.
        // All-notes-off, controller reset, audible volume and a GS reset on every
        // channel. A song's tail can leave the board muted, so note-offs alone are not
        // enough to hand it back playable.
        void resetMidiModule(uint32_t offset);

        // Both name lists of the loaded board, cached because the host asks for parameter
        // text far too often to re-read the ROM each time. Rebuilt on boot and ROM switch.
        std::vector<std::string> m_emu88ToneNames;
        std::vector<std::string> m_emu88KitNames;
        // Board the two lists above were read from, or -1 when nothing is cached. An
        // async boot names the board before its device exists, so the cache is keyed by
        // model rather than by "not empty": a switch must not leave the old board's
        // names in place.
        int m_emu88NamesModel = -1;
        void cacheEmu88Names();

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
        // Consumed by processDeferredResend (called each audio block) after setSynthType.
        // m_deviceBooted: once true, the boot-delay resend is never re-armed.
        std::atomic<bool> m_isBooting{false};
        std::atomic<bool> m_shuttingDown{false};
        std::unique_ptr<std::thread> m_bootThread;
        void joinBootThread();
        std::atomic<bool> m_pendingResend{false};
        int m_resendBlocksRemaining = 0;
        bool m_deviceBooted = false;

        // 88emu: replay the per-part programs once the freshly booted board accepts MIDI.
        std::atomic<bool> m_pendingEmu88PartResend{false};
        // Set on the audio thread when incoming MIDI changed a part's program; the UI
        // timer picks it up and refreshes the parameters.
        std::atomic<bool> m_emu88PartProgramDirty{false};
        // The audio thread changed what the shown part's programs are called: it became a
        // drum part (or stopped being one), or its variation moved, so the tone list is
        // rebuilt on the message thread, which is the only one allowed to touch it.
        std::atomic<bool> m_emu88ToneListDirty{false};
    };
}
