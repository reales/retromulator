#include "HeadlessProcessor.h"
#ifdef CUSTOM
#  include "../custom/RetroEditor.h"
#else
#  include "BasicEditor.h"
#endif
#include "MinimalController.h"
#include "SynthFactory.h"

#include "synthLib/deviceException.h"
#include "synthLib/midiToSysex.h"
#include "jucePluginLib/dummydevice.h"
#include "baseLib/binarystream.h"

#include "nord/n2x/n2xLib/n2xstate.h"
#include "nord/n2x/n2xLib/n2xromloader.h"
#include "mqLib/mqmiditypes.h"
#include "mqLib/romloader.h"
#include "xtLib/xtMidiTypes.h"
#include "xtLib/xtRomLoader.h"
#include "ronaldo/je8086/jeLib/state.h"
#include "ronaldo/je8086/jeLib/romloader.h"

#include "virusLib/romloader.h"
#include "virusLib/romfile.h"
#include "dx7Lib/device.h"
#include "dx7Lib/romloader.h"
#include "virusLib/deviceModel.h"
#include "virusLib/microcontrollerTypes.h"
#include "synthLib/romLoader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <unistd.h>
#  define MKDIR(p) mkdir(p, 0755)
#endif

namespace retromulator
{
    // ── Data folder helpers ───────────────────────────────────────────────────

    static void makeDirsRecursive(const std::string& path)
    {
        std::string cur;
        for(const char c : path)
        {
            cur += c;
            if(c == '/' || c == '\\')
                MKDIR(cur.c_str());
        }
        MKDIR(cur.c_str());
    }

    std::string HeadlessProcessor::getDataFolder()
    {
#if defined(_WIN32)
        const char* docs = std::getenv("USERPROFILE");
        if(!docs) docs = "";
        return std::string(docs) + "\\Documents\\discoDSP\\Retromulator\\";
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if(!home) home = "";
        return std::string(home) + "/Library/Application Support/discoDSP/Retromulator/";
#else
        const char* home = std::getenv("HOME");
        if(!home) home = "";
        return std::string(home) + "/Documents/discoDSP/Retromulator/";
#endif
    }

    std::string HeadlessProcessor::getSynthDataFolder(SynthType type)
    {
        return getDataFolder() + synthTypeName(type) + "/";
    }

    // ── GPL boundary helpers ─────────────────────────────────────────────────
    // These three functions exist solely to keep GPL-specific headers (per-synth
    // ROM loaders, synthLib::DeviceError) out of source/custom/RetroEditor.cpp.
    // HeadlessProcessor is part of the GPL source distribution; RetroEditor is not.
    // The editor calls these plain-bool / plain-string wrappers so that
    // source/custom/ has zero GPL includes and can be treated as an independent work.

    bool HeadlessProcessor::isFirmwareMissing() const
    {
        return m_deviceError == synthLib::DeviceError::FirmwareMissing;
    }

    bool HeadlessProcessor::hasDeviceError() const
    {
        return m_deviceError != synthLib::DeviceError::None;
    }

    bool HeadlessProcessor::isRomValid(SynthType type)
    {
        switch(type)
        {
        case SynthType::VirusABC: return virusLib::ROMLoader::findROM(virusLib::DeviceModel::ABC).isValid();
        case SynthType::VirusTI:  return virusLib::ROMLoader::findROM(virusLib::DeviceModel::TI).isValid();
        case SynthType::MicroQ:   return mqLib::RomLoader::findROM().isValid();
        case SynthType::XT:       return xt::RomLoader::findROM().isValid();
        case SynthType::NordN2X:  return n2x::RomLoader::findROM().isValid();
        case SynthType::JE8086:   return jeLib::RomLoader::findROM().isValid();
        case SynthType::DX7:      return dx7Emu::RomLoader::findROM().isValid();
        default:                  return false;
        }
    }

    void HeadlessProcessor::addRomSearchPath(const std::string& path)
    {
        synthLib::RomLoader::addSearchPath(path);
    }
    // ── End GPL boundary helpers ─────────────────────────────────────────────

    std::string HeadlessProcessor::copySysexToDataFolder(const std::string& sourcePath)
    {
        if(sourcePath.empty())
            return {};

        const auto sep = sourcePath.find_last_of("/\\");
        const std::string filename = (sep == std::string::npos) ? sourcePath : sourcePath.substr(sep + 1);

        const std::string destDir  = getSynthDataFolder(m_synthType);
        const std::string destPath = destDir + filename;

        // If the file is already anywhere inside the synth data folder
        // (including subfolders), don't copy it — just return the original path.
        // Use juce::File::isAChildOf for case-insensitive, separator-agnostic comparison.
        {
            const juce::File src(sourcePath);
            const juce::File dir(destDir);
            if(src.isAChildOf(dir) || src == dir)
                return sourcePath;
        }

        makeDirsRecursive(destDir);

        std::ifstream src(sourcePath, std::ios::binary);
        if(!src.is_open())
        {
            fprintf(stderr, "[Retromulator] Cannot open sysex source: %s\n", sourcePath.c_str());
            return {};
        }

        std::ofstream dst(destPath, std::ios::binary | std::ios::trunc);
        if(!dst.is_open())
        {
            fprintf(stderr, "[Retromulator] Cannot write sysex to: %s\n", destPath.c_str());
            return {};
        }

        dst << src.rdbuf();
        return destPath;
    }

    // ── Virus ABC / TI sysex detection ──────────────────────────────────────

    SynthType HeadlessProcessor::detectVirusType(const std::vector<synthLib::SysexBuffer>& messages)
    {
        // Access Music manufacturer: F0 00 20 33
        // Byte [4] = product (always 0x01 in practice for both ABC and TI)
        // Byte [6] = command  (0x10 = DUMP_SINGLE)
        // ABC single dump body: 256 bytes → total sysex ~267 bytes (F0 + header + 256 + cs + F7)
        // TI  single dump body: 512 bytes → total sysex ~524 bytes (F0 + header + 256 + cs + 256 + cs + F7)
        // Threshold: anything > 400 bytes is TI.

        size_t virusCount = 0;
        size_t totalSize  = 0;

        for(const auto& msg : messages)
        {
            if(msg.size() >= 9 &&
               msg[0] == 0xF0 && msg[1] == 0x00 && msg[2] == 0x20 && msg[3] == 0x33 &&
               msg[6] == 0x10) // DUMP_SINGLE
            {
                ++virusCount;
                totalSize += msg.size();
            }
        }

        if(virusCount == 0)
            return SynthType::None;

        const size_t avgSize = totalSize / virusCount;
        return (avgSize > 400) ? SynthType::VirusTI : SynthType::VirusABC;
    }

    std::string HeadlessProcessor::copySysexToFolder(const std::string& sourcePath, SynthType targetType)
    {
        if(sourcePath.empty())
            return {};

        const auto sep = sourcePath.find_last_of("/\\");
        const std::string filename = (sep == std::string::npos) ? sourcePath : sourcePath.substr(sep + 1);

        const std::string destDir  = getSynthDataFolder(targetType);
        const std::string destPath = destDir + filename;

        // Already in the target folder?
        {
            const juce::File src(sourcePath);
            const juce::File dir(destDir);
            if(src.isAChildOf(dir) || src == dir)
                return sourcePath;
        }

        makeDirsRecursive(destDir);

        std::ifstream src(sourcePath, std::ios::binary);
        if(!src.is_open()) return {};

        std::ofstream dst(destPath, std::ios::binary | std::ios::trunc);
        if(!dst.is_open()) return {};

        dst << src.rdbuf();
        return destPath;
    }

    // ── JE-8086 ROM preset extraction ────────────────────────────────────────
    // Extracts factory patches and performances from the ROM and writes them as
    // .syx files into the JE-8086 data folder so the bank combo can browse them.
    // Banks are named "!ROM Patches A.syx", "!ROM Patches B.syx", …
    // and "!ROM Performances A.syx", "!ROM Performances B.syx", …
    // The "!" prefix sorts them before any user-imported files in the bank combo.
    // Files are only written if they don't already exist.

    static void extractRomPresets(const std::string& destFolder)
    {
        const auto rom = jeLib::RomLoader::findROM();
        if(!rom.isValid())
            return;

        std::vector<std::vector<jeLib::Rom::Preset>> banks;
        rom.getPresets(banks);
        if(banks.empty())
            return;

        makeDirsRecursive(destFolder);

        // Determine how many patch banks vs performance banks there are.
        // Patches: 64 per bank; performances: 64 per bank, always come after patches.
        // Rack has 8 patch banks (512 patches) + 4 perf banks (256 perfs).
        // Keyboard has 2 patch banks (128 patches) + 1 perf bank (64 perfs).
        const bool rack = (rom.getDeviceType() == jeLib::DeviceType::Rack);
        const int patchBanks = rack ? 8 : 2;

        for(int b = 0; b < static_cast<int>(banks.size()); ++b)
        {
            const auto& bank = banks[static_cast<size_t>(b)];

            const bool isPerf = (b >= patchBanks);
            const int  letter = b - (isPerf ? patchBanks : 0);
            const char suffix = static_cast<char>('A' + letter);

            const std::string filename = destFolder
                + (isPerf ? "!ROM Performances " : "!ROM Patches ")
                + suffix + ".syx";

            // Skip if already extracted.
            {
                std::ifstream check(filename, std::ios::binary);
                if(check.is_open())
                    continue;
            }

            std::ofstream f(filename, std::ios::binary | std::ios::trunc);
            if(!f.is_open())
            {
                fprintf(stderr, "[Retromulator] Cannot write ROM bank: %s\n", filename.c_str());
                continue;
            }

            for(const auto& preset : bank)
            {
                for(const auto& msg : preset)
                {
                    if(!msg.empty())
                        f.write(reinterpret_cast<const char*>(msg.data()),
                                static_cast<std::streamsize>(msg.size()));
                }
            }
        }
    }

    // ── Virus ABC/TI ROM preset extraction ───────────────────────────────────
    // Extracts factory singles from the Virus ROM into per-bank .syx files.
    // Banks are named "!ROM Bank A.syx", "!ROM Bank B.syx", etc.
    // Each message is a standard Virus DUMP_SINGLE sysex targeting bank N,
    // so the existing sendBankMessage EditBuffer redirect plays it immediately.
    // Files are only written if they don't already exist.

    static void extractVirusRomPresets(const std::string& destFolder,
                                       const virusLib::DeviceModel model)
    {
        const auto rom = virusLib::ROMLoader::findROM(model);
        if(!rom.isValid())
            return;

        makeDirsRecursive(destFolder);

        const uint32_t bankCount    = virusLib::ROMFile::getRomBankCount(model);
        const uint32_t presetsPerBank = rom.getPresetsPerBank();
        const uint32_t presetSize   = rom.getSinglePresetSize();
        const bool     isTI         = rom.isTIFamily();

        // Virus sysex single dump header: F0 00 20 33 01 <devId> 10 <bank> <prog>
        // Checksum covers bytes [5..end-1], masked to 0x7F.
        // ABC:  256-byte preset → 1 block, 1 checksum
        // TI:   512-byte preset → 2 × 256-byte blocks, each with its own checksum
        const auto calcCs = [](const synthLib::SysexBuffer& s) -> uint8_t
        {
            uint8_t cs = 0;
            for(size_t i = 5; i < s.size(); ++i)
                cs += s[i];
            return cs & 0x7f;
        };

        for(uint32_t b = 0; b < bankCount; ++b)
        {
            const char letter = static_cast<char>('A' + b);
            const std::string filename = destFolder + "!ROM Bank " + letter + ".syx";

            {
                std::ifstream check(filename, std::ios::binary);
                if(check.is_open())
                    continue;
            }

            std::ofstream f(filename, std::ios::binary | std::ios::trunc);
            if(!f.is_open())
            {
                fprintf(stderr, "[Retromulator] Cannot write Virus ROM bank: %s\n", filename.c_str());
                continue;
            }

            bool anyWritten = false;
            for(uint32_t p = 0; p < presetsPerBank; ++p)
            {
                virusLib::ROMFile::TPreset preset{};
                if(!rom.getSingle(static_cast<int>(b), static_cast<int>(p), preset))
                    continue;

                // bank MIDI byte: EditBuffer=0, A=1, B=2, ...
                const uint8_t bankByte    = static_cast<uint8_t>(b + 1);
                const uint8_t programByte = static_cast<uint8_t>(p & 0x7f);

                synthLib::SysexBuffer sysex = {
                    0xf0, 0x00, 0x20, 0x33, 0x01,
                    virusLib::OMNI_DEVICE_ID,
                    0x10,        // DUMP_SINGLE
                    bankByte,
                    programByte
                };

                if(isTI)
                {
                    // Two 256-byte halves, each followed by its own checksum
                    for(size_t j = 0; j < 256; ++j)
                        sysex.push_back(preset[j]);
                    sysex.push_back(calcCs(sysex));
                    for(size_t j = 256; j < presetSize; ++j)
                        sysex.push_back(preset[j]);
                    sysex.push_back(calcCs(sysex));
                }
                else
                {
                    for(size_t j = 0; j < presetSize; ++j)
                        sysex.push_back(preset[j]);
                    sysex.push_back(calcCs(sysex));
                }

                sysex.push_back(0xf7);

                f.write(reinterpret_cast<const char*>(sysex.data()),
                        static_cast<std::streamsize>(sysex.size()));
                anyWritten = true;
            }

            if(!anyWritten)
            {
                // ROM had no presets for this bank — remove the empty file
                f.close();
                std::remove(filename.c_str());
            }
        }
    }

    // ── Constructor ───────────────────────────────────────────────────────────

    HeadlessProcessor::HeadlessProcessor()
        : pluginLib::Processor(
            BusesProperties()
                .withInput ("Input",  juce::AudioChannelSet::stereo(), false)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true),
            pluginLib::Processor::Properties{
                "Retromulator",   // name
                "discoDSP",       // vendor
                true,             // isSynth
                true,             // wantsMidiInput
                false,            // producesMidiOut
                false,            // isMidiEffect
                "RtMU",           // plugin4CC
                "",               // lv2Uri
                {}                // binaryData (no embedded resources)
            })
    {
        getController();

        // Pre-initialize m_plugin with a silent DummyDevice so that prepareToPlay
        // never triggers getPlugin()'s lazy-init, which would call createDevice(),
        // throw for SynthType::None, and show a "firmware missing" dialog on startup.
        // We set m_synthType to a non-None sentinel temporarily so createDevice()
        // doesn't throw, then install the DummyDevice ourselves.
        m_device.reset(new pluginLib::DummyDevice({}));
        m_plugin.reset(new synthLib::Plugin(m_device.get(), {}));
    }

    // ── Synth hot-swap ────────────────────────────────────────────────────────

    void HeadlessProcessor::setSynthType(SynthType type, const std::string& romPath)
    {
        m_synthType   = type;
        m_romPath     = romPath;
        m_deviceError = synthLib::DeviceError::None;
        m_deviceBooted = false;
        m_pendingResend.store(false);
        m_resendBlocksRemaining = 0;

        // Clear preset state so the editor sees a clean slate for the new synth.
        // Without this, m_sysexFilePath etc. still hold the previous synth's values,
        // causing updateStatus() to skip prog/bank combo resets.
        m_sysexFilePath.clear();
        m_patchName.clear();
        m_sysexData.clear();
        m_bankMessages.clear();
        m_programNames.clear();
        m_currentProgram = 0;
        m_bankStride     = 1;

        suspendProcessing(true);

        if(type == SynthType::None)
        {
            // Swap in a silent DummyDevice directly — rebootDevice() would show an
            // error dialog when createDevice() throws for None, leaving the old device running.
            auto* dummy = new pluginLib::DummyDevice({});
            getPlugin().setDevice(dummy);
            (void)m_device.release();
            m_device.reset(dummy);
        }
        else
        {
            if(!rebootDevice())
                m_deviceError = synthLib::DeviceError::FirmwareMissing;

            if(m_deviceError == synthLib::DeviceError::None)
            {
                // Extract factory presets from the ROM into .syx files so they appear
                // in the bank combo. Files are only written if they don't already exist.
                if(type == SynthType::JE8086)
                    extractRomPresets(getSynthDataFolder(SynthType::JE8086));
                else if(type == SynthType::VirusABC)
                    extractVirusRomPresets(getSynthDataFolder(SynthType::VirusABC),
                                          virusLib::DeviceModel::ABC);
                else if(type == SynthType::VirusTI)
                    extractVirusRomPresets(getSynthDataFolder(SynthType::VirusTI),
                                          virusLib::DeviceModel::TI);
            }
        }

        // JE-8086 runs synchronously on the audio thread (JeThread falls back to
        // processJob inline when m_currentLatency==0), so the default extra latency
        // block adds ~11ms of MIDI delay for no benefit. Set it to zero.
        setLatencyBlocks(type == SynthType::JE8086 ? 0 : 1);

        suspendProcessing(false);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    }

    // ── Patch name extraction ─────────────────────────────────────────────────

    static std::string extractPatchName(SynthType type, const synthLib::SysexBuffer& msg)
    {
        auto readAscii = [](const synthLib::SysexBuffer& buf, size_t offset, size_t len) -> std::string
        {
            if(buf.size() < offset + len) return {};
            std::string name(reinterpret_cast<const char*>(buf.data() + offset), len);
            // Trim trailing spaces and nulls
            const auto end = name.find_last_not_of(" \x00");
            return (end == std::string::npos) ? std::string{} : name.substr(0, end + 1);
        };

        switch(type)
        {
        case SynthType::NordN2X:
            return n2x::State::extractPatchName(msg);

        case SynthType::MicroQ:
            return readAscii(msg, mqLib::mq::g_singleNameOffset, mqLib::mq::g_singleNameLength);

        case SynthType::XT:
            if(msg.size() >= xt::mw2::g_singleNamePosition + xt::mw2::g_singleNameLength)
                return readAscii(msg, xt::mw2::g_singleNamePosition, xt::mw2::g_singleNameLength);
            return readAscii(msg, xt::Mw1::g_singleNamePosition, xt::Mw1::g_singleNameLength);

        case SynthType::JE8086:
        {
            const auto name = jeLib::State::getName(msg);
            return name ? *name : std::string{};
        }

        case SynthType::VirusABC:
        case SynthType::VirusTI:
            return readAscii(msg, 9 + 240, 10);

        case SynthType::DX7:
            return dx7Emu::Device::extractPatchName(msg.data(), msg.size());

        default:
            return {};
        }
    }

    // ── Preset loading ────────────────────────────────────────────────────────

    bool HeadlessProcessor::loadPreset(const std::vector<uint8_t>& sysexData,
                                       const std::string& sourcePath,
                                       const std::string& patchName,
                                       int programIndex)
    {
        if(!getPlugin().isValid())
            return false;

        // Split the raw bytes into individual SysEx messages.
        // A bank .syx contains one message per patch; a single-patch .syx has exactly one.
        synthLib::SysexBufferList messages;
        const synthLib::SysexBuffer sysexBuf(sysexData.begin(), sysexData.end());
        synthLib::MidiToSysex::extractSysexFromData(messages, sysexBuf);

        if(messages.empty())
            return false;

        m_bankMessages = std::move(messages);
        m_sysexData    = sysexData;
        m_patchName    = patchName;

        // DX7 bulk voice dump: single 4104-byte sysex containing 32 packed voices (128 bytes each).
        // Split into 32 entries so the program browser shows individual voices.
        // Each entry stores the raw packed voice data (128 bytes) for name extraction.
        // The original bulk dump is kept in m_sysexData for sending to the device.
        if(m_synthType == SynthType::DX7 && m_bankMessages.size() == 1)
        {
            const auto& bulk = m_bankMessages[0];
            if(bulk.size() == 4104 &&
               bulk[0] == 0xF0 && bulk[1] == 0x43 && bulk[3] == 0x09 &&
               bulk[4] == 0x20 && bulk[5] == 0x00)
            {
                synthLib::SysexBufferList voiceEntries;
                voiceEntries.reserve(32);
                for(int v = 0; v < 32; v++)
                {
                    // Each packed voice is 128 bytes starting at offset 6 in the sysex
                    synthLib::SysexBuffer voice(bulk.begin() + 6 + v * 128,
                                                bulk.begin() + 6 + (v + 1) * 128);
                    voiceEntries.push_back(std::move(voice));
                }
                m_bankMessages = std::move(voiceEntries);
            }
        }

        // Detect JE-8086 UserPerformance banks: Roland DT1 (0x41 … 0x12), area 0x03.
        // Each performance is split across several sub-messages (PerformanceCommon,
        // VoiceModulator, PartUpper/Lower, PatchUpper/Lower …) that share the same
        // slot byte at position [7].  Count consecutive messages with the same [7] to
        // get the stride so getProgramCount() returns the number of performances.
        m_bankStride = 1;
        if(!m_bankMessages.empty())
        {
            const auto& first = m_bankMessages[0];
            if(first.size() >= 7 &&
               first[1] == 0x41 && first[3] == 0x00 && first[4] == 0x06 &&
               first[5] == 0x12 && first[6] == 0x03)
            {
                const uint8_t firstSlot = first[7];
                int stride = 1;
                while(stride < static_cast<int>(m_bankMessages.size()))
                {
                    const auto& m = m_bankMessages[static_cast<size_t>(stride)];
                    if(m.size() >= 8 && m[7] == firstSlot)
                        ++stride;
                    else
                        break;
                }
                m_bankStride = stride;
            }
        }

        const int progCount = getProgramCount();
        m_currentProgram = std::max(0, std::min(programIndex, progCount - 1));

        // Pre-extract all program names so the editor can populate the full combo list.
        m_programNames.resize(static_cast<size_t>(progCount));
        for(int i = 0; i < progCount; ++i)
        {
            const auto& msg = m_bankMessages[static_cast<size_t>(i * m_bankStride)];
            m_programNames[static_cast<size_t>(i)] = extractPatchName(m_synthType, msg);
        }

        if(!sourcePath.empty())
        {
            const juce::File srcFile(sourcePath);
            const juce::File dataDir(getSynthDataFolder(m_synthType));
            if(srcFile.isAChildOf(dataDir) || srcFile == dataDir)
                m_sysexFilePath = sourcePath;  // already in data folder, use as-is
            else
            {
                const std::string dest = copySysexToDataFolder(sourcePath);
                m_sysexFilePath = dest.empty() ? sourcePath : dest;
            }
        }

        // Send only the selected program, not the entire bank dump.
        sendBankMessage(m_currentProgram);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }

    void HeadlessProcessor::sendBankMessage(int index)
    {
        const int rawStart = index * m_bankStride;

        if(rawStart < 0 || rawStart >= static_cast<int>(m_bankMessages.size()))
            return;

        // Use pre-extracted name from m_programNames (populated in loadPreset)
        if(index >= 0 && index < static_cast<int>(m_programNames.size()))
            m_patchName = m_programNames[static_cast<size_t>(index)];

        // If prepareToPlay has not been called yet (AU XPC: UI fires before the audio
        // engine starts), don't push into the DSP — the device is not ready to process
        // MIDI and will crash. Arm the deferred-resend mechanism instead; processBpm
        // will replay once the first audio block arrives.
        if(getHostSamplerate() == 0.0f)
        {
            if(!m_pendingResend.load())
            {
                m_resendBlocksRemaining = 100;
                m_pendingResend.store(true);
            }
            return;
        }

        // DX7 bulk voice dump: m_bankMessages contains 32 x 128-byte packed voice entries
        // (split from the original 4104-byte bulk dump in loadPreset).
        // Send the original bulk dump sysex to load all 32 voices into the DX7 firmware,
        // then a MIDI program change to select the specific voice.
        if(m_synthType == SynthType::DX7 && m_bankMessages.size() == 32 &&
           m_sysexData.size() >= 4104)
        {
            // Re-send original bulk dump from m_sysexData
            synthLib::SMidiEvent bulkEv(synthLib::MidiEventSource::Editor);
            bulkEv.sysex.assign(m_sysexData.begin(), m_sysexData.begin() + 4104);
            getPlugin().addMidiEvent(bulkEv);

            // Force firmware to reload voice parameters by first selecting a
            // different voice, then the target. Without this, the firmware may
            // skip reloading if the current voice number already matches.
            const uint8_t dummy = static_cast<uint8_t>(((index & 0x1f) + 1) % 32);
            synthLib::SMidiEvent pcDummy(synthLib::MidiEventSource::Editor,
                0xC0, dummy, 0x00);
            getPlugin().addMidiEvent(pcDummy);

            synthLib::SMidiEvent pc(synthLib::MidiEventSource::Editor,
                0xC0, static_cast<uint8_t>(index & 0x1f), 0x00);
            getPlugin().addMidiEvent(pc);

            if(!m_deviceBooted && !m_pendingResend.load())
            {
                m_resendBlocksRemaining = 100;
                m_pendingResend.store(true);
            }
            return;
        }

        synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
        ev.sysex = m_bankMessages[static_cast<size_t>(rawStart)];

        // n2x single dump: F0 33 <device> 04 <bank> <prog> ...
        // Bank dumps (bank != 0x00) target stored slots — the synth stores them
        // but doesn't play them. Redirect to the edit buffer (bank=0x00, part=0x00)
        // so the patch becomes active immediately, same as n2xController::activatePatch.
        //
        // Sysex layout (0-based from F0):
        //   [0]=F0  [1]=0x33(Clavia)  [2]=device  [3]=0x04(N2X)
        //   [4]=msgType(bank)  [5]=msgSpec(program slot)
        //
        // Single dump banks: 0x00=EditBuffer, 0x01-0x04=BankA-D
        // We detect: manufacturer=0x33, model=0x04, msgType in 0x01..0x04 (bank single)
        if(ev.sysex.size() >= 6 &&
           ev.sysex[1] == 0x33 && ev.sysex[3] == 0x04 &&
           ev.sysex[4] >= 0x01 && ev.sysex[4] <= 0x04)
        {
            ev.sysex[2] = 0x0f; // DefaultDeviceId
            ev.sysex[4] = 0x00; // SingleDumpBankEditBuffer
            ev.sysex[5] = 0x00; // part 0 (edit buffer slot)
        }

        // Virus ABC/TI single dump: F0 00 20 33 <product> <deviceId> 0x10 <bank> <prog> ...
        // DUMP_SINGLE (0x10) with bank != 0x00 (EditBuffer) stores into RAM bank but
        // does NOT send to the DSP — the patch is silently ignored. Redirect to
        // EditBuffer (bank=0x00) + SINGLE part (0x40) so it plays immediately.
        //
        // Sysex layout (0-based from F0):
        //   [0]=F0  [1]=0x00  [2]=0x20  [3]=0x33 (Access manufacturer)
        //   [4]=product  [5]=deviceId  [6]=cmd(0x10=DUMP_SINGLE)
        //   [7]=bank  [8]=program
        //
        // Bank values: 0x00=EditBuffer, 0x01=BankA, 0x02=BankB, ...
        // SINGLE part = 0x40 (single-mode edit buffer slot)
        if(ev.sysex.size() >= 9 &&
           ev.sysex[1] == 0x00 && ev.sysex[2] == 0x20 && ev.sysex[3] == 0x33 &&
           ev.sysex[6] == 0x10 && ev.sysex[7] != 0x00)
        {
            ev.sysex[7] = 0x00; // EditBuffer
            ev.sysex[8] = 0x40; // SINGLE part
        }

        // JE-8086 UserPatch dump: area 0x02 stores the patch to a user slot.
        // The sysex address bytes [7][8][9] already encode the exact target slot.
        // We send the sysex as-is so the firmware stores it, then send a Program Change
        // matching that slot number so the firmware selects and plays it immediately.
        // Roland sysex layout: F0 41 <dev> 00 06 12 <a0> <a1> <a2> <a3> <data> <cs> F7
        //   [6]=0x02 (UserPatch area), [7]=bank (0=A,1=B), [8]=slot*2, [9]=0x00
        if(m_bankStride == 1 &&
           ev.sysex.size() >= 10 &&
           ev.sysex[1] == 0x41 && ev.sysex[3] == 0x00 && ev.sysex[4] == 0x06 &&
           ev.sysex[5] == 0x12 && ev.sysex[6] == 0x02)
        {
            // The sysex already targets the correct UserPatch slot encoded in [7][8][9].
            // Keep the address unchanged so each patch lands in its own slot.
            // Derive the Program Change number from the slot address:
            //   [7]=0x00 (bank A, slots 0-63):  PC = [8] / 2
            //   [7]=0x01 (bank B, slots 64-127): PC = 64 + [8] / 2
            const uint8_t addrBank   = ev.sysex[7];
            const uint8_t addrOffset = ev.sysex[8];
            const int programNumber  = (addrBank == 0x00)
                ? static_cast<int>(addrOffset / 2)
                : 64 + static_cast<int>(addrOffset / 2);

            getPlugin().addMidiEvent(ev);

            // MIDI Program Change ch1 → selects the UserPatch slot we just wrote
            synthLib::SMidiEvent pc(synthLib::MidiEventSource::Editor,
                0xC0, // Program Change, channel 1
                static_cast<uint8_t>(programNumber & 0x7f),
                0x00);
            getPlugin().addMidiEvent(pc);

            if(!m_deviceBooted && !m_pendingResend.load())
            {
                m_resendBlocksRemaining = 100;
                m_pendingResend.store(true);
            }
            return;
        }

        // JE-8086 UserPerformance dump: each performance = m_bankStride sub-messages.
        // Area 0x03 = UserPerformance — stores to user slot but doesn't play.
        // For each sub-message: send original (to store), then a PerformanceTemp copy
        // (area 0x01) keeping the sub-area offset bytes [7..9] so each component
        // (PerformanceCommon, VoiceModulator, PartUpper, PatchUpper/Lower …) lands in
        // the right slot inside the temp performance buffer.
        // Roland checksum = (128 - (sum of addr+data bytes mod 128)) & 0x7F
        if(m_bankStride > 1)
        {
            // Helper: recalculate Roland checksum in-place
            const auto recomputeChecksum = [](synthLib::SysexBuffer& s)
            {
                const int csIdx = static_cast<int>(s.size()) - 2;
                uint8_t sum = 0;
                for(int i = 6; i < csIdx; ++i)
                    sum += s[static_cast<size_t>(i)];
                s[static_cast<size_t>(csIdx)] = (128 - (sum & 0x7f)) & 0x7f;
            };

            // Send each sub-component as a PerformanceTemp message (area 0x01).
            // addMidiEvent queues for the DSP via the rate limiter (21ms/msg per JP-8080 manual).
            // We also arm a deferred resend: the JE-8086 firmware ignores MIDI events during the
            // first ~0.14s of boot (now < 12776184 cycles), so if this is called at startup the
            // events are silently dropped.  processBlock resends once the DSP has warmed up.
            for(int s = 0; s < m_bankStride; ++s)
            {
                synthLib::SMidiEvent tmp(synthLib::MidiEventSource::Editor);
                tmp.sysex = m_bankMessages[static_cast<size_t>(rawStart + s)];
                tmp.sysex[6] = 0x01; // PerformanceTemp area
                tmp.sysex[7] = 0x00; // no slot in temp buffer; sub-component stays in [8]

                // Force MIDI channels to ch1 (Upper) and ch2 (Lower) so the emulator's
                // default MIDI input reaches the parts.  Part data layout: the MidiChannel
                // param is at offset 2 within the part block, which is sysex data byte [12]
                // (header=10 bytes + 2 bytes into part data).
                // PartUpper: [8]=0x10, PartLower: [8]=0x11
                if(tmp.sysex.size() >= 13 &&
                   (tmp.sysex[8] == 0x10 || tmp.sysex[8] == 0x11))
                {
                    tmp.sysex[12] = (tmp.sysex[8] == 0x10) ? 0x00 : 0x01;
                }

                recomputeChecksum(tmp.sysex);
                getPlugin().addMidiEvent(tmp);
            }
            // Schedule deferred resend ~0.5s after audio starts (44100/512 ≈ 86 blocks).
            // Only arm during boot delay; once booted, the performance loads immediately.
            if(!m_deviceBooted && !m_pendingResend.load())
            {
                m_resendBlocksRemaining = 100;
                m_pendingResend.store(true);
            }
            return;
        }

        getPlugin().addMidiEvent(ev);
    }

    bool HeadlessProcessor::selectProgram(int index)
    {
        if(index < 0 || index >= getProgramCount())
            return false;

        m_currentProgram = index;
        sendBankMessage(index);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }

    // ── JP-8000 .pfm (Performance Manager) to sysex conversion ─────────────
    // .pfm layout: 128-byte header ("JP-8000 USER PERFORMANCE0001…")
    //              + 64 performances × 528 bytes each.
    // Each 528-byte performance block (already 7-bit sysex-encoded):
    //   [0..35]    PerformanceCommon (36 bytes, incl. 16-byte name)
    //   [36..42]   PartUpper  (7 bytes = Part::DataLengthKeyboard)
    //   [43..49]   PartLower  (7 bytes)
    //   [50..288]  PatchUpper (239 bytes = Patch::DataLengthKeyboard, incl. 16-byte name)
    //   [289..527] PatchLower (239 bytes)
    // We wrap each sub-block with a Roland DT1 sysex header so the result is
    // identical to the ROM-extracted performance .syx files.

    static bool convertPfmToSysex(const std::vector<uint8_t>& pfmData,
                                  std::vector<uint8_t>& sysexOut)
    {
        constexpr size_t kHeaderSize = 128;
        constexpr size_t kPerfSize   = 528;
        constexpr size_t kPerfCount  = 64;

        if(pfmData.size() < kHeaderSize + kPerfCount * kPerfSize)
            return false;

        // Verify it looks like a JP-8000 .pfm
        if(pfmData.size() < 8 || std::memcmp(pfmData.data(), "JP-8000 ", 8) != 0)
            return false;

        // Sub-block sizes (keyboard model, matching DataLengthKeyboard values)
        constexpr size_t kPerfCommonSize = 36;  // PerformanceCommon::DataLengthKeyboard
        constexpr size_t kPartSize       =  7;  // Part::DataLengthKeyboard
        constexpr size_t kPatchSize      = 239; // Patch::DataLengthKeyboard

        sysexOut.clear();
        sysexOut.reserve(kPerfCount * 5 * 256); // rough estimate

        for(size_t p = 0; p < kPerfCount; ++p)
        {
            const uint8_t* perf = pfmData.data() + kHeaderSize + p * kPerfSize;

            // Base address for UserPerformance slot p
            const uint32_t addrBase = static_cast<uint32_t>(jeLib::AddressArea::UserPerformance)
                | (static_cast<uint32_t>(jeLib::UserPerformanceArea::UserPerformance01)
                   + static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockSize)
                     * static_cast<uint32_t>(p));

            struct Block {
                uint32_t addressOffset; // OR'd with addrBase
                const uint8_t* data;
                size_t size;
            };

            const Block blocks[] = {
                { 0,                                                               perf,       kPerfCommonSize },
                { static_cast<uint32_t>(jeLib::PerformanceData::PartUpper),         perf + 36,  kPartSize       },
                { static_cast<uint32_t>(jeLib::PerformanceData::PartLower),         perf + 43,  kPartSize       },
                { static_cast<uint32_t>(jeLib::PerformanceData::PatchUpper),        perf + 50,  kPatchSize      },
                { static_cast<uint32_t>(jeLib::PerformanceData::PatchLower),        perf + 289, kPatchSize      },
            };

            for(const auto& blk : blocks)
            {
                const uint32_t addr = addrBase | blk.addressOffset;
                auto addr4 = jeLib::State::toAddress(addr);
                auto syx = jeLib::State::createHeader(
                    jeLib::SysexByte::CommandIdDataSet1,
                    jeLib::SysexByte::DeviceIdDefault, addr4);

                syx.insert(syx.end(), blk.data, blk.data + blk.size);
                jeLib::State::createFooter(syx);

                sysexOut.insert(sysexOut.end(), syx.begin(), syx.end());
            }
        }

        return true;
    }

    static bool hasSuffix(const std::string& s, const char* suffix)
    {
        const auto len = std::strlen(suffix);
        if(s.size() < len) return false;
        for(size_t i = 0; i < len; ++i)
            if(std::tolower(static_cast<unsigned char>(s[s.size() - len + i])) !=
               std::tolower(static_cast<unsigned char>(suffix[i])))
                return false;
        return true;
    }

    bool HeadlessProcessor::loadPresetFromFile(const std::string& filePath,
                                               const std::string& patchName,
                                               int programIndex)
    {
        std::ifstream f(filePath, std::ios::binary);
        if(!f.is_open())
        {
            fprintf(stderr, "[Retromulator] Cannot open sysex file: %s\n", filePath.c_str());
            return false;
        }

        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());

        // JP-8000 Performance Manager files need conversion to sysex
        if(hasSuffix(filePath, ".pfm"))
        {
            std::vector<uint8_t> sysexData;
            if(!convertPfmToSysex(data, sysexData))
            {
                fprintf(stderr, "[Retromulator] Invalid .pfm file: %s\n", filePath.c_str());
                return false;
            }
            data = std::move(sysexData);
        }

        const auto sep = filePath.find_last_of("/\\");
        const std::string autoName = patchName.empty()
            ? ((sep == std::string::npos) ? filePath : filePath.substr(sep + 1))
            : patchName;

        return loadPreset(data, filePath, autoName, programIndex);
    }

    // ── Preset export ────────────────────────────────────────────────────────────

    // Write a name into an N2X sysex message using the Aura extension.
    // For non-N2X synths the buffer is returned unchanged.
    static synthLib::SysexBuffer embedName(SynthType type,
                                           const synthLib::SysexBuffer& msg,
                                           const std::string& name)
    {
        if(type == SynthType::NordN2X && !name.empty() &&
           (n2x::State::isSingleDump(msg) || n2x::State::isMultiDump(msg)))
            return n2x::State::writePatchName(msg, name);
        return msg;
    }

    bool HeadlessProcessor::exportCurrentPresetToFile(const std::string& destPath) const
    {
        if(m_bankMessages.empty())
            return false;

        const int progCount = getProgramCount();
        if(m_currentProgram < 0 || m_currentProgram >= progCount)
            return false;

        const int rawIdx = m_currentProgram * m_bankStride;
        const auto& msg = m_bankMessages[static_cast<size_t>(rawIdx)];

        const std::string name = (m_currentProgram < static_cast<int>(m_programNames.size()))
            ? m_programNames[static_cast<size_t>(m_currentProgram)]
            : m_patchName;

        const auto out = embedName(m_synthType, msg, name);

        std::ofstream f(destPath, std::ios::binary | std::ios::trunc);
        if(!f.is_open())
        {
            fprintf(stderr, "[Retromulator] Cannot write preset: %s\n", destPath.c_str());
            return false;
        }
        f.write(reinterpret_cast<const char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
        return f.good();
    }

    bool HeadlessProcessor::exportCurrentBankToFile(const std::string& destPath) const
    {
        if(m_bankMessages.empty())
            return false;

        std::ofstream f(destPath, std::ios::binary | std::ios::trunc);
        if(!f.is_open())
        {
            fprintf(stderr, "[Retromulator] Cannot write bank: %s\n", destPath.c_str());
            return false;
        }

        const int progCount = getProgramCount();
        for(int p = 0; p < progCount; ++p)
        {
            const std::string name = (p < static_cast<int>(m_programNames.size()))
                ? m_programNames[static_cast<size_t>(p)]
                : std::string{};

            for(int s = 0; s < m_bankStride; ++s)
            {
                const auto& msg = m_bankMessages[static_cast<size_t>(p * m_bankStride + s)];
                // Only embed name in the first sub-message of each stride (the main patch data)
                const auto out = (s == 0) ? embedName(m_synthType, msg, name) : msg;
                f.write(reinterpret_cast<const char*>(out.data()),
                        static_cast<std::streamsize>(out.size()));
            }
        }
        return f.good();
    }

    // ── processBpm (deferred resend: pre-audio guard + JE-8086 boot delay) ──────

    void HeadlessProcessor::processBpm(float /*_bpm*/)
    {
        // Resend the current bank message after the DSP is ready.
        // Two cases arm m_pendingResend:
        //   1. AU XPC: prepareToPlay not yet called when UI fires sendBankMessage.
        //   2. JE-8086: firmware silently discards MIDI in the first ~0.14s of emulation.
        if(m_pendingResend.load())
        {
            if(--m_resendBlocksRemaining <= 0)
            {
                m_pendingResend.store(false);
                m_deviceBooted = true;  // boot delay done; no more auto-resends
                if(m_currentProgram >= 0 && m_currentProgram < getProgramCount())
                    sendBankMessage(m_currentProgram);
            }
        }
    }

    // ── Editor ────────────────────────────────────────────────────────────────

    juce::AudioProcessorEditor* HeadlessProcessor::createEditor()
    {
#ifdef CUSTOM
        return new RetroEditor(*this);
#else
        return new BasicEditor(*this);
#endif
    }

    // ── pluginLib::Processor pure virtuals ────────────────────────────────────

    synthLib::Device* HeadlessProcessor::createDevice()
    {
        if(m_synthType == SynthType::None)
            throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "No synth selected");

        // Let DeviceException propagate — caller (rebootDevice or getPlugin) handles it.
        return SynthFactory::create(m_synthType, m_romPath);
    }

    pluginLib::Controller* HeadlessProcessor::createController()
    {
        return new MinimalController(*this);
    }

    // ── State persistence ─────────────────────────────────────────────────────
    //
    // Format (all little-endian int32 lengths):
    //   [synthType:int32]
    //   [romPathLen:int32][romPath:chars]
    //   [sysexFilePathLen:int32][sysexFilePath:chars]
    //   [patchNameLen:int32][patchName:chars]
    //   [sysexDataLen:int32][sysexData:bytes]

    static void appendInt32(juce::MemoryBlock& out, int32_t v)
    {
        out.append(&v, sizeof(v));
    }

    static void appendString(juce::MemoryBlock& out, const std::string& s)
    {
        const int32_t len = static_cast<int32_t>(s.size());
        out.append(&len, sizeof(len));
        if(len > 0)
            out.append(s.data(), static_cast<size_t>(len));
    }

    static void appendBytes(juce::MemoryBlock& out, const std::vector<uint8_t>& v)
    {
        const int32_t len = static_cast<int32_t>(v.size());
        out.append(&len, sizeof(len));
        if(len > 0)
            out.append(v.data(), static_cast<size_t>(len));
    }

    void HeadlessProcessor::getStateInformation(juce::MemoryBlock& destData)
    {
        destData.setSize(0);
        appendInt32 (destData, static_cast<int32_t>(m_synthType));
        appendString(destData, m_romPath);
        appendString(destData, m_sysexFilePath);
        appendString(destData, m_patchName);
        appendBytes (destData, m_sysexData);
        appendInt32 (destData, static_cast<int32_t>(m_currentProgram));
        appendInt32 (destData, static_cast<int32_t>(m_savedEditorWidth));
        appendInt32 (destData, static_cast<int32_t>(m_savedEditorHeight));
    }

    static bool readInt32(const uint8_t* bytes, int total, int& offset, int32_t& out)
    {
        if(offset + 4 > total) return false;
        std::memcpy(&out, bytes + offset, 4);
        offset += 4;
        return true;
    }

    static bool readString(const uint8_t* bytes, int total, int& offset, std::string& out)
    {
        int32_t len = 0;
        if(!readInt32(bytes, total, offset, len)) return false;
        if(len < 0 || offset + len > total) return false;
        out.assign(reinterpret_cast<const char*>(bytes + offset), static_cast<size_t>(len));
        offset += len;
        return true;
    }

    static bool readBytes(const uint8_t* bytes, int total, int& offset, std::vector<uint8_t>& out)
    {
        int32_t len = 0;
        if(!readInt32(bytes, total, offset, len)) return false;
        if(len < 0 || offset + len > total) return false;
        out.assign(bytes + offset, bytes + offset + len);
        offset += len;
        return true;
    }

    void HeadlessProcessor::setStateInformation(const void* data, int sizeInBytes)
    {
        if(sizeInBytes < 4)
            return;

        const auto* bytes = static_cast<const uint8_t*>(data);
        int offset = 0;

        int32_t synthTypeInt = 0;
        if(!readInt32(bytes, sizeInBytes, offset, synthTypeInt)) return;

        std::string romPath, sysexFilePath, patchName;
        std::vector<uint8_t> sysexData;

        readString(bytes, sizeInBytes, offset, romPath);
        readString(bytes, sizeInBytes, offset, sysexFilePath);
        readString(bytes, sizeInBytes, offset, patchName);
        readBytes (bytes, sizeInBytes, offset, sysexData);

        // currentProgram + editor size are appended after sysexData (optional, backwards compat).
        int32_t savedProgram = 0;
        readInt32(bytes, sizeInBytes, offset, savedProgram);

        int32_t editorW = 0, editorH = 0;
        readInt32(bytes, sizeInBytes, offset, editorW);
        readInt32(bytes, sizeInBytes, offset, editorH);
        if(editorW > 0 && editorH > 0)
        {
            m_savedEditorWidth  = static_cast<int>(editorW);
            m_savedEditorHeight = static_cast<int>(editorH);
            m_editorSizeDirty   = true;
        }

        const auto newType = static_cast<SynthType>(synthTypeInt);
        setSynthType(newType, romPath);

        // loadPreset splits the bank, stores messages, and sends message[savedProgram].
        // We then restore the original sysexFilePath (loadPreset would set it via copySysexToDataFolder).
        if(!sysexData.empty())
            loadPreset(sysexData, {}, patchName, static_cast<int>(savedProgram));

        m_sysexFilePath = sysexFilePath;
    }
}
