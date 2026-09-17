#include "HeadlessProcessor.h"
#ifdef CUSTOM
#  include "../custom/RetroEditor.h"
#else
#  include "BasicEditor.h"
#endif
#include "MinimalController.h"
#include "SynthFactory.h"
#include "ParameterPool.h"
#include "BinaryData.h"

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
#include "ronaldo/88emu/88lib/romloader.h"
#include "ronaldo/88emu/88lib/hardwareDevice.h"
#include "ronaldo/88emu/88emuplayer/midifile.hpp"
#include "Emu88Tones.h"
#include "Mp3Encoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include "virusLib/romloader.h"
#include "virusLib/romfile.h"
#include "dx7Lib/device.h"
#include "dx7Lib/romloader.h"
#include "virusLib/deviceModel.h"
#include "virusLib/microcontrollerTypes.h"
#ifdef CUSTOM
#include "virusLib/presetConverter.h"
#endif
#include "synthLib/romLoader.h"

#include "akaiLib/device.h"
#include "openWurliLib/device.h"
#include "opl3Lib/device.h"
#include "sidLib/device.h"
#include "ayumiLib/device.h"

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

    bool HeadlessProcessor::ensureDataDirectory(const juce::File& dir)
    {
        if(dir.isDirectory())
            return true;
        if(dir.existsAsFile() && dir.getSize() == 0)
            dir.deleteFile();
        return dir.createDirectory();
    }

    std::string HeadlessProcessor::getSynthDataFolder(SynthType type)
    {
        return getDataFolder() + synthTypeName(type) + "/";
    }

    static juce::File getSettingsFile()
    {
        return juce::File(juce::String(HeadlessProcessor::getDataFolder()) + "settings.xml");
    }

    static juce::String makeSettingsKey(SynthType type)
    {
        return juce::String("lastLoadFolder_" + juce::String(synthTypeName(type)))
            .replaceCharacter(' ', '_');
    }

    std::string HeadlessProcessor::getLastLoadFolder(SynthType type)
    {
        const auto file = getSettingsFile();
        if(!file.existsAsFile()) return {};

        if(const auto xml = juce::XmlDocument::parse(file))
        {
            const auto val = xml->getStringAttribute(makeSettingsKey(type));
            if(val.isNotEmpty() && juce::File(val).isDirectory())
                return val.toStdString();
        }
        return {};
    }

    void HeadlessProcessor::setLastLoadFolder(SynthType type, const std::string& folder)
    {
        const auto file = getSettingsFile();
        std::unique_ptr<juce::XmlElement> xml;

        if(file.existsAsFile())
            xml = juce::XmlDocument::parse(file);

        if(!xml)
            xml = std::make_unique<juce::XmlElement>("RetromulatorSettings");

        xml->setAttribute(makeSettingsKey(type), juce::String(folder));
        xml->writeTo(file);
    }

    // ── Persistent editor size (settings.xml) ───────────────────────────────

    void HeadlessProcessor::loadEditorSizeFromSettings()
    {
        const auto file = getSettingsFile();
        if(!file.existsAsFile()) return;

        if(const auto xml = juce::XmlDocument::parse(file))
        {
            const int w = xml->getIntAttribute("editorWidth", 0);
            const int h = xml->getIntAttribute("editorHeight", 0);
            if(w > 0 && h > 0)
            {
                m_savedEditorWidth  = w;
                m_savedEditorHeight = h;
            }
        }
    }

    void HeadlessProcessor::saveEditorSizeToSettings(int w, int h)
    {
        const auto file = getSettingsFile();
        std::unique_ptr<juce::XmlElement> xml;

        if(file.existsAsFile())
            xml = juce::XmlDocument::parse(file);

        if(!xml)
            xml = std::make_unique<juce::XmlElement>("RetromulatorSettings");

        xml->setAttribute("editorWidth",  w);
        xml->setAttribute("editorHeight", h);
        xml->writeTo(file);
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
        case SynthType::DX7:       return dx7Emu::RomLoader::findROM().isValid();
        case SynthType::Emu88:
        {
            // Content-identified sets; rescan so a dump the user just imported is seen.
            const auto inventory = emu88Lib::RomLoader::rescan();
            for(uint32_t i = 0; i < emu88Lib::deviceModelCount(); ++i)
            {
                if(inventory.isComplete(emu88Lib::RomLoader::toRomDevice(static_cast<emu88Lib::DeviceModel>(i))))
                    return true;
            }
            return false;
        }
        case SynthType::AkaiS1000: return true; // No ROM needed
        default:                   return false;
        }
    }

    void HeadlessProcessor::addRomSearchPath(const std::string& path, const bool recursive)
    {
        synthLib::RomLoader::addSearchPath(path, recursive);
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
                pluginLib::Processor::BinaryDataRef{   // parameter descriptions
                    BinaryData::namedResourceListSize,
                    BinaryData::originalFilenames,
                    BinaryData::namedResourceList,
                    BinaryData::getNamedResource
                }
            })
    {
        getController();

        // Host parameters must exist before the host queries the tree.
        m_paramPool = std::make_unique<ParameterPool>(*this);
        m_paramPool->setCore(SynthType::None);

        // Seed the ROM search path with the shared data folder so all loaders
        // can find firmware files placed there.
        ensureDataDirectory(juce::File(getDataFolder() + "ROM/"));
        addRomSearchPath(getDataFolder() + "ROM/");
        // 88emu identifies its ROM sets by content, so its folder is searched recursively.
        // Registered here, not in SynthFactory, because isRomValid() runs before any device exists.
        ensureDataDirectory(juce::File(getDataFolder() + "88emu/"));
        addRomSearchPath(getDataFolder() + "88emu/", true);

        // Pre-initialize m_plugin with a silent DummyDevice so that prepareToPlay
        // never triggers getPlugin()'s lazy-init, which would call createDevice(),
        // throw for SynthType::None, and show a "firmware missing" dialog on startup.
        // We set m_synthType to a non-None sentinel temporarily so createDevice()
        // doesn't throw, then install the DummyDevice ourselves.
        m_device.reset(new pluginLib::DummyDevice({}));
        m_plugin.reset(new synthLib::Plugin(m_device.get(), {}));

        loadEditorSizeFromSettings();
    }

    HeadlessProcessor::~HeadlessProcessor()
    {
        // Must join before any member is destroyed: ~thread on a joinable
        // thread calls std::terminate.
        m_shuttingDown.store(true);
        joinBootThread();

        // A render in flight holds the device, so it has to finish before anything below.
        m_renderCancel.store(true);
        if(m_renderThread && m_renderThread->joinable())
            m_renderThread->join();

        if(m_savedEditorWidth > 0 && m_savedEditorHeight > 0)
            saveEditorSizeToSettings(m_savedEditorWidth, m_savedEditorHeight);

        suspendProcessing(true);
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

        if(m_paramPool)
            m_paramPool->setCore(type);

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
                else if(type == SynthType::OPL3)
                    juce::File(getSynthDataFolder(SynthType::OPL3)).createDirectory();
                else if(type == SynthType::Emu88)
                    restoreEmu88Parts(getEmu88Part());
                else if(type == SynthType::SID)
                    juce::File(getSynthDataFolder(SynthType::SID)).createDirectory();
                else if(type == SynthType::Ayumi)
                {
                    // Two banks: Factory (read-only presets) and User (imports).
                    // Keeping imports out of Factory means Factory's MIDI Program
                    // Change numbers never shift.
                    const juce::File root(getSynthDataFolder(SynthType::Ayumi));
                    root.createDirectory();
                    root.getChildFile("Factory").createDirectory();
                    root.getChildFile("User").createDirectory();
                }
            }
        }

        // JE-8086, Akai, OpenWurli, OPL3, SID, Ayumi run synchronously — no extra latency block needed.
        setLatencyBlocks((type == SynthType::JE8086 || type == SynthType::AkaiS1000
                       || type == SynthType::OpenWurli || type == SynthType::OPL3
                       || type == SynthType::SID || type == SynthType::Ayumi) ? 0 : 1);

        suspendProcessing(false);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
    }

    void HeadlessProcessor::joinBootThread()
    {
        if(m_bootThread && m_bootThread->joinable())
            m_bootThread->join();
        m_bootThread.reset();
    }

    void HeadlessProcessor::setSynthTypeAsync(SynthType type, const std::string& romPath,
                                              std::function<void()> onComplete)
    {
        // Wait for any previous async boot to finish.
        joinBootThread();

        if(type == SynthType::None)
        {
            // SynthType::None is instant — no need for a background thread.
            setSynthType(type, romPath);
            if(onComplete)
                onComplete();
            return;
        }

        // ── Prepare state on the calling (GUI) thread ────────────────────────
        m_isBooting.store(true);
        m_synthType   = type;
        m_romPath     = romPath;
        m_deviceError = synthLib::DeviceError::None;
        m_deviceBooted = false;
        m_pendingResend.store(false);
        m_resendBlocksRemaining = 0;

        m_sysexFilePath.clear();
        m_patchName.clear();
        m_sysexData.clear();
        m_bankMessages.clear();
        m_programNames.clear();
        m_currentProgram = 0;
        m_bankStride     = 1;

        if(m_paramPool)
            m_paramPool->setCore(type);

        suspendProcessing(true);

        // Detach the old device from Plugin without deleting it — device destructors
        // join DSP threads which can block for a long time. We'll delete on the boot thread.
        getPlugin().releaseDevice();
        std::unique_ptr<synthLib::Device> oldDevice(m_device.release());

        // Install a DummyDevice so the audio callback has something safe while booting.
        auto* dummy = new pluginLib::DummyDevice({});
        getPlugin().setDevice(dummy);
        m_device.reset(dummy);

        suspendProcessing(false);

        // Delete old device on a fire-and-forget thread — its DSP thread join
        // can block for a long time and must not delay the new device boot.
        auto oldDeviceReady = std::make_shared<std::atomic<bool>>(false);
        if(oldDevice)
        {
            std::thread([dev = std::move(oldDevice), ready = oldDeviceReady]() mutable {
                fprintf(stderr, "[Boot] destroying old device...\n");
                dev.reset();
                fprintf(stderr, "[Boot] old device destroyed\n");
                ready->store(true);
            }).detach();
        }
        else
        {
            oldDeviceReady->store(true);
        }

        // ── Boot on a background thread ──────────────────────────────────────
        m_bootThread = std::make_unique<std::thread>([this, type, romPath,
                                                      oldDeviceReady,
                                                      onComplete = std::move(onComplete)]()
        {
            // Wait for old device destruction to finish before allocating new
            // DSP resources — MemoryBuffer temp files must not overlap.
            // Timeout after 5 seconds to avoid hanging if the old DSP thread is stuck.
            for(int i = 0; i < 500 && !oldDeviceReady->load() && !m_shuttingDown.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(m_shuttingDown.load())
                return;
            if(!oldDeviceReady->load())
                fprintf(stderr, "[Boot] WARNING: old device destruction timed out, proceeding anyway\n");

            fprintf(stderr, "[Boot] async: suspendProcessing(true)\n");
            suspendProcessing(true);

            fprintf(stderr, "[Boot] async: rebootDevice for type %d\n", static_cast<int>(type));
            bool ok = rebootDevice();
            fprintf(stderr, "[Boot] async: rebootDevice returned %d\n", ok);

            // Set latency blocks AFTER rebootDevice so the new device's
            // samplerate is used for the latency calculation.
            {
                const bool isSynchronous = (type == SynthType::JE8086 || type == SynthType::AkaiS1000
                                         || type == SynthType::OpenWurli || type == SynthType::OPL3
                                         || type == SynthType::Ayumi || type == SynthType::None);
                const bool isN2X = (type == SynthType::NordN2X);
                setLatencyBlocks(isSynchronous ? 0 : (isN2X ? 1 : 3));
            }
            if(!ok)
                m_deviceError = synthLib::DeviceError::FirmwareMissing;

            if(m_deviceError == synthLib::DeviceError::None)
            {
                fprintf(stderr, "[Boot] async: extracting presets\n");
                if(type == SynthType::JE8086)
                    extractRomPresets(getSynthDataFolder(SynthType::JE8086));
                else if(type == SynthType::VirusABC)
                    extractVirusRomPresets(getSynthDataFolder(SynthType::VirusABC),
                                          virusLib::DeviceModel::ABC);
                else if(type == SynthType::VirusTI)
                    extractVirusRomPresets(getSynthDataFolder(SynthType::VirusTI),
                                          virusLib::DeviceModel::TI);
                else if(type == SynthType::OPL3)
                    juce::File(getSynthDataFolder(SynthType::OPL3)).createDirectory();
                else if(type == SynthType::Emu88)
                    restoreEmu88Parts(getEmu88Part());
                else if(type == SynthType::SID)
                    juce::File(getSynthDataFolder(SynthType::SID)).createDirectory();
                else if(type == SynthType::Ayumi)
                {
                    // Two banks: Factory (read-only presets) and User (imports).
                    // Keeping imports out of Factory means Factory's MIDI Program
                    // Change numbers never shift.
                    const juce::File root(getSynthDataFolder(SynthType::Ayumi));
                    root.createDirectory();
                    root.getChildFile("Factory").createDirectory();
                    root.getChildFile("User").createDirectory();
                }
            }

            fprintf(stderr, "[Boot] async: suspendProcessing(false)\n");
            suspendProcessing(false);
            m_isBooting.store(false);

            // Skip the callback during shutdown: it would run after `this` is gone.
            if(m_shuttingDown.load())
                return;

            // Notify the message thread that boot is complete.
            juce::MessageManager::callAsync([this, onComplete]()
            {
                applyStandaloneBufferSize();
                updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
                if(onComplete)
                    onComplete();
            });
        });
    }

    void HeadlessProcessor::applyStandaloneBufferSize()
    {
#if JucePlugin_Build_Standalone && JUCE_IOS
        if(wrapperType != wrapperType_Standalone)
            return;

        const auto type = m_synthType;
        juce::MessageManager::callAsync([type]()
        {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            if(!holder)
                return;
            auto& dm = holder->deviceManager;
            auto* dev = dm.getCurrentAudioDevice();
            if(!dev)
                return;

            auto setup = dm.getAudioDeviceSetup();
            const int target = isJitCore(type) ? std::max(512, setup.bufferSize) : dev->getDefaultBufferSize();
            if(setup.bufferSize == target)
                return;

            setup.bufferSize = target;
            dm.setAudioDeviceSetup(setup, true);
            fprintf(stderr, "[iOS] Standalone buffer size set to %d (actual %d)\n", target, dev->getCurrentBufferSizeSamples());
        });
#endif
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
        if(m_paramPool)
            m_paramPool->clearTouched();
        sendBankMessage(m_currentProgram);
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                              .withNonParameterStateChanged(true)
                              .withProgramChanged(true));
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
        // MIDI and will crash. Arm the deferred-resend mechanism instead; it replays
        // once the first audio block arrives.
        if(getHostSamplerate() == 0.0f)
        {
            // Standalone: prepareToPlay hasn't been called yet.  Schedule a
            // resend once the message loop returns — by that time the audio
            // engine will be running and the samplerate will be set.
            const int prog = index;
            juce::MessageManager::callAsync([this, prog]()
            {
                if(m_currentProgram >= 0 && m_currentProgram < getProgramCount())
                    sendBankMessage(prog);
            });
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

        // Waldorf XT single dump: F0 3E <machine> <devId> 10 <bank> <prog> ...
        // SingleDump (0x10) to BankA (0x00) or BankB (0x01) stores to RAM but doesn't
        // activate the sound. Redirect to SingleEditBufferSingleMode (0x20) so the
        // patch plays immediately.
        //
        // Sysex layout (0-based from F0):
        //   [0]=F0  [1]=0x3E(Waldorf)  [2]=machine  [3]=deviceId
        //   [4]=command(0x10=SingleDump)  [5]=bank  [6]=program
        //
        // Bank values: 0x00=BankA, 0x01=BankB, 0x20=SingleEditBufferSingleMode
        if(ev.sysex.size() >= 7 &&
           ev.sysex[1] == 0x3e &&
           ev.sysex[4] == 0x10 &&
           (ev.sysex[5] == 0x00 || ev.sysex[5] == 0x01))
        {
            ev.sysex[5] = 0x20; // SingleEditBufferSingleMode
            ev.sysex[6] = 0x00; // program 0 (edit buffer slot)
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

        // The new patch is now the source of truth; drop stale slot edits.
        if(m_paramPool)
            m_paramPool->clearTouched();

        // Akai S1000: preset switching is handled by the SFZero subsound system,
        // not by sysex bank messages. Route to the correct selector.
        if(m_synthType == SynthType::AkaiS1000)
        {
            if(m_akaiIsoMode)
                return selectAkaiIsoPreset(index);
            else
                return selectSoundPreset(index);
        }

        if(m_synthType == SynthType::Emu88)
        {
            m_currentProgram = index;
            if(index < static_cast<int>(m_programNames.size()))
                m_patchName = m_programNames[static_cast<size_t>(index)];

            // The part's own bank, not 0: a GS variation lives in the bank, so forcing 0
            // here would drop the part back to its capital tone every time a tone is
            // picked. A drum part ignores the bank and reads the program as a kit number,
            // so only the program change is sent there; the tone list then names kits.
            const uint8_t ch = m_paramPool ? m_paramPool->getPartChannel() : 0;
            m_emu88PartPrograms[ch] = index;
            if(!isEmu88PartRhythm(ch))
            {
                const synthLib::SMidiEvent bankMsb(synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 0,
                    static_cast<uint8_t>(m_emu88PartBankMsb[ch] & 0x7f));
                const synthLib::SMidiEvent bankLsb(synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 32,
                    static_cast<uint8_t>(m_emu88PartBankLsb[ch] & 0x7f));
                addMidiEvent(bankMsb);
                addMidiEvent(bankLsb);
            }
            const synthLib::SMidiEvent pc(synthLib::MidiEventSource::Editor,
                static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | ch), static_cast<uint8_t>(index & 0x7f), 0);
            addMidiEvent(pc);
            return true;
        }

        m_currentProgram = index;

        // SID: route to the device's instrument selector; no sysex bank involved.
        if(m_synthType == SynthType::SID)
        {
            if(auto* dev = getSidDevice())
            {
                dev->selectInstrument(index + 1); // 1-based on the device side
                m_patchName = dev->getCurrentPatchName();
            }
        }
        else
        {
            sendBankMessage(index);
        }

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
        // Ayumi: .ay patch files go directly to the device — no sysex involved.
        if(m_synthType == SynthType::Ayumi && hasSuffix(filePath, ".ay"))
        {
            auto* dev = getAyumiDevice();
            if(!dev) return false;

            if(!dev->loadPatchFile(filePath)) return false;

            m_sysexFilePath  = filePath;
            m_patchName      = patchName.empty() ? dev->getPatchName() : patchName;
            m_sysexData.clear();
            m_bankMessages.clear();
            m_programNames.clear();
            m_bankStride     = 1;
            m_currentProgram = 0;

            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                .withNonParameterStateChanged(true));
            return true;
        }

        // OPL3: .sbi files go directly to the device — no sysex involved.
        if(m_synthType == SynthType::OPL3 && hasSuffix(filePath, ".sbi"))
        {
            auto* dev = getOpl3Device();
            if(!dev) return false;

            if(!dev->loadSbi(filePath)) return false;

            m_sysexFilePath  = filePath;
            m_patchName      = patchName.empty()
                ? dev->getPatchName()
                : patchName;
            m_sysexData.clear();
            m_bankMessages.clear();
            m_programNames.clear();
            m_bankStride     = 1;
            m_currentProgram = 0;

            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                .withNonParameterStateChanged(true));
            return true;
        }

        // SID: .sng / .ins / .sid are multi-instrument banks loaded directly into the device.
        if(m_synthType == SynthType::SID
            && (hasSuffix(filePath, ".sng") || hasSuffix(filePath, ".ins") || hasSuffix(filePath, ".sid")))
        {
            auto* dev = getSidDevice();
            if(!dev) return false;

            if(!dev->loadBankFile(filePath)) return false;

            const int count = dev->getInstrumentCount();
            m_programNames.clear();
            m_programNames.reserve(static_cast<size_t>(count));
            for(int i = 1; i <= count; ++i)
                m_programNames.push_back(dev->getInstrumentName(i));

            // Auto-select first instrument
            if(count > 0)
                dev->selectInstrument(programIndex >= 0 && programIndex < count
                                      ? programIndex + 1 : 1);

            m_sysexFilePath  = filePath;
            m_patchName      = patchName.empty()
                ? (count > 0 ? dev->getCurrentPatchName() : std::string{})
                : patchName;
            m_sysexData.clear();
            m_bankMessages.clear();
            m_bankStride     = 1;
            m_currentProgram = (count > 0) ? std::max(0, programIndex) : 0;

            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails()
                .withNonParameterStateChanged(true));
            return true;
        }

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

    // ── Sound file loading (Akai S1000) ────────────────────────────────────────

    akaiLib::Device* HeadlessProcessor::getAkaiDevice() const
    {
        if(m_synthType != SynthType::AkaiS1000)
            return nullptr;
        return dynamic_cast<akaiLib::Device*>(m_device.get());
    }

    openWurliLib::Device* HeadlessProcessor::getOpenWurliDevice() const
    {
        if(m_synthType != SynthType::OpenWurli)
            return nullptr;
        return dynamic_cast<openWurliLib::Device*>(m_device.get());
    }

    opl3Lib::Device* HeadlessProcessor::getOpl3Device() const
    {
        if(m_synthType != SynthType::OPL3)
            return nullptr;
        return dynamic_cast<opl3Lib::Device*>(m_device.get());
    }

    sidLib::Device* HeadlessProcessor::getSidDevice() const
    {
        if(m_synthType != SynthType::SID)
            return nullptr;
        return dynamic_cast<sidLib::Device*>(m_device.get());
    }

    emu88Lib::HardwareDevice* HeadlessProcessor::getEmu88Device() const
    {
        if(m_synthType != SynthType::Emu88)
            return nullptr;
        return dynamic_cast<emu88Lib::HardwareDevice*>(m_device.get());
    }

    int HeadlessProcessor::getEmu88Model() const
    {
        const auto* dev = getEmu88Device();
        return dev ? static_cast<int>(dev->model()) : -1;
    }

    int HeadlessProcessor::getEmu88Part() const
    {
        return m_paramPool ? m_paramPool->getPartChannel() : 0;
    }

    void HeadlessProcessor::setEmu88Part(const int part)
    {
        if(!m_paramPool || part < 0 || part > 15)
            return;
        m_paramPool->setPartChannel(static_cast<uint8_t>(part));
        // The visible parameters belong to the new part now, so stale edits from the old
        // one must not be resent on top of it.
        m_paramPool->clearTouched();
        // Drum parts list kits, melodic parts list tones.
        loadEmu88ToneNames();
    }

    void HeadlessProcessor::resetEmu88Parts()
    {
        // A fresh board (or a GS/GM reset) puts every part back on program 0 and bank 0,
        // so the cache that feeds the tone display has to follow it.
        m_emu88PartPrograms.fill(0);
        m_emu88PartBankMsb.fill(0);
        m_emu88PartBankLsb.fill(0);
        // A GS reset also puts the rhythm assignment back: part 10 drums, the rest melodic.
        m_emu88PartRhythm.fill(0);
        m_emu88PartRhythm[9] = 1;
        if(m_synthType == SynthType::Emu88)
            loadEmu88ToneNames();
    }

    namespace
    {
        // GS DT1 "Use For Rhythm Part" at 40 1x 15. The part blocks are not the channel
        // order: block 0 is part 10, blocks 1-9 are parts 1-9, blocks 10-15 parts 11-16.
        std::vector<uint8_t> buildEmu88RhythmSysex(const int part, const int value)
        {
            const auto block = static_cast<uint8_t>(part == 9 ? 0 : part < 9 ? part + 1 : part);
            const uint8_t a1 = 0x40, a2 = static_cast<uint8_t>(0x10 | block), a3 = 0x15;
            const auto v   = static_cast<uint8_t>(value & 0x7f);
            const auto chk = static_cast<uint8_t>((128 - ((a1 + a2 + a3 + v) & 0x7f)) & 0x7f);
            return {0xf0, 0x41, 0x10, 0x42, 0x12, a1, a2, a3, v, chk, 0xf7};
        }
    }

    void HeadlessProcessor::sendEmu88PartPrograms()
    {
        if(m_synthType != SynthType::Emu88)
            return;

        // A part that was switched to drums (or away from the default part 10) goes back
        // first: it decides whether the program that follows is read as a kit or a tone.
        for(size_t ch = 0; ch < m_emu88PartRhythm.size(); ++ch)
        {
            const int r = m_emu88PartRhythm[ch];
            if(r == (ch == 9 ? 1 : 0))
                continue;
            sendSysex(buildEmu88RhythmSysex(static_cast<int>(ch), r));
        }

        // Every part gets its program, including program 0. A part the song put on 0 is
        // indistinguishable from an untouched one in the cache, and skipping it left the
        // board on whatever the boot happened to load.
        for(size_t ch = 0; ch < m_emu88PartPrograms.size(); ++ch)
        {
            const int prog = m_emu88PartPrograms[ch];
            // A drum part reads the program as a kit number and ignores the bank.
            if(!isEmu88PartRhythm(static_cast<int>(ch)))
            {
                // The part's own bank, not 0: a GS variation lives in the bank, so sending
                // 0 here would drop every part back to its capital tone.
                addMidiEvent({synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 0,
                    static_cast<uint8_t>(m_emu88PartBankMsb[ch] & 0x7f)});
                addMidiEvent({synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 32,
                    static_cast<uint8_t>(m_emu88PartBankLsb[ch] & 0x7f)});
            }
            addMidiEvent({synthLib::MidiEventSource::Editor,
                static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | ch),
                static_cast<uint8_t>(prog & 0x7f), 0});
        }
    }

    void HeadlessProcessor::setEmu88PartProgram(const int part, const int program)
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return;
        if(m_emu88PartPrograms[static_cast<size_t>(part)] == program)
            return;

        m_emu88PartPrograms[static_cast<size_t>(part)] = program;

        const auto ch = static_cast<uint8_t>(part);
        // A drum part reads the program as a kit number and ignores the bank. The others
        // keep the variation the part already has rather than being forced back to 0.
        if(!isEmu88PartRhythm(part))
        {
            addMidiEvent({synthLib::MidiEventSource::Editor,
                static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 0,
                static_cast<uint8_t>(m_emu88PartBankMsb[static_cast<size_t>(part)] & 0x7f)});
            addMidiEvent({synthLib::MidiEventSource::Editor,
                static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 32,
                static_cast<uint8_t>(m_emu88PartBankLsb[static_cast<size_t>(part)] & 0x7f)});
        }
        addMidiEvent({synthLib::MidiEventSource::Editor,
            static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | ch),
            static_cast<uint8_t>(program & 0x7f), 0});

        // Editing the visible part must move the tone list and patch name with it.
        if(part == getEmu88Part())
        {
            m_currentProgram = program;
            if(program >= 0 && program < static_cast<int>(m_programNames.size()))
                m_patchName = m_programNames[static_cast<size_t>(program)];
            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        }
    }

    void HeadlessProcessor::onEmu88ProgramChange(const int part, const int program)
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return;
        if(m_emu88PartPrograms[static_cast<size_t>(part)] == program)
            return;

        // The board already made this change, so only the cached view follows it: sending
        // it back would fight a MIDI file that is driving the parts.
        m_emu88PartPrograms[static_cast<size_t>(part)] = program;
        m_emu88PartProgramDirty.store(true, std::memory_order_release);
    }

    bool HeadlessProcessor::isEmu88PartRhythm(const int part) const
    {
        if(part < 0 || part > 15)
            return false;
        return m_emu88PartRhythm[static_cast<size_t>(part)] != 0;
    }

    void HeadlessProcessor::setEmu88PartRhythm(const int part, const int value)
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return;
        if(m_emu88PartRhythm[static_cast<size_t>(part)] == value)
            return;

        m_emu88PartRhythm[static_cast<size_t>(part)] = value;
        sendSysex(buildEmu88RhythmSysex(part, value));

        // The program means a kit now rather than a tone (or the other way round), so the
        // board is told again what to make of the one this part holds.
        addMidiEvent({synthLib::MidiEventSource::Editor,
            static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | static_cast<uint8_t>(part)),
            static_cast<uint8_t>(m_emu88PartPrograms[static_cast<size_t>(part)] & 0x7f), 0});

        if(part == getEmu88Part())
            loadEmu88ToneNames();
    }

    void HeadlessProcessor::onEmu88Sysex(const std::vector<uint8_t>& data)
    {
        if(m_synthType != SynthType::Emu88)
            return;

        // Roland DT1: F0 41 <dev> 42 12 <a1 a2 a3> <data...> <sum> F7. Only the one-byte
        // form is read here, which is what carries Use For Rhythm Part.
        if(data.size() < 11 || data[0] != 0xf0 || data[1] != 0x41 || data[3] != 0x42 || data[4] != 0x12)
            return;

        const uint8_t a1 = data[5], a2 = data[6], a3 = data[7];

        // Part parameters live at 40 1x 00.., where x is the part block. The blocks are
        // not the channel order: block 0 is part 10, blocks 1-9 are parts 1-9, and
        // blocks 10-15 are parts 11-16.
        if(a1 != 0x40 || (a2 & 0xf0) != 0x10 || a3 != 0x15)
            return;

        const int block = a2 & 0x0f;
        const int part  = block == 0 ? 9 : block <= 9 ? block - 1 : block;
        const int value = data[8] & 0x7f;
        if(m_emu88PartRhythm[static_cast<size_t>(part)] == value)
            return;
        m_emu88PartRhythm[static_cast<size_t>(part)] = value;

        // The tone list names kits or tones depending on this, so it is rebuilt on the
        // message thread, which is the only one allowed to touch it.
        if(part == getEmu88Part())
            m_emu88ToneListDirty.store(true, std::memory_order_release);
    }

    void HeadlessProcessor::onEmu88BankSelect(const int part, const int cc, const int value)
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return;
        auto& bank = (cc == 0) ? m_emu88PartBankMsb[static_cast<size_t>(part)]
                               : m_emu88PartBankLsb[static_cast<size_t>(part)];
        if(bank == value)
            return;
        bank = value;

        // The tone the shown part plays has a different name on another variation, so the
        // list follows it. This runs on the audio thread, so the rebuild is deferred.
        if(part == getEmu88Part())
            m_emu88ToneListDirty.store(true, std::memory_order_release);
    }

    int HeadlessProcessor::getEmu88PartBank(const int part, const int cc) const
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return 0;
        return (cc == 0) ? m_emu88PartBankMsb[static_cast<size_t>(part)]
                         : m_emu88PartBankLsb[static_cast<size_t>(part)];
    }

    void HeadlessProcessor::setEmu88PartBank(const int part, const int cc, const int value)
    {
        if(m_synthType != SynthType::Emu88 || part < 0 || part > 15)
            return;
        // A drum part reads the program as a kit number and ignores the bank.
        if(isEmu88PartRhythm(part))
            return;
        // The editor's timer mirrors the bank back into the host parameters, and the host
        // echoes that into setValue, which lands here again. Acting on the echo would
        // re-send a program change every block, so only a real change is sent.
        if(getEmu88PartBank(part, cc) == value)
            return;

        onEmu88BankSelect(part, cc, value);

        const auto ch = static_cast<uint8_t>(part);
        addMidiEvent({synthLib::MidiEventSource::Editor,
            static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), static_cast<uint8_t>(cc == 0 ? 0 : 32),
            static_cast<uint8_t>(value & 0x7f)});
        // A bank select only takes effect on the next program change, so the part's
        // program is re-sent to make the variation audible straight away.
        addMidiEvent({synthLib::MidiEventSource::Editor,
            static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | ch),
            static_cast<uint8_t>(m_emu88PartPrograms[static_cast<size_t>(part)] & 0x7f), 0});

        // The tone list names the tone each program plays, and this part just changed
        // which one that is, so the row it sits on is named again.
        if(part == getEmu88Part())
            loadEmu88ToneNames();
    }

    void HeadlessProcessor::restoreEmu88Parts(const int part)
    {
        if(m_synthType != SynthType::Emu88)
            return;

        // A different board has different tone and kit tables, so the name cache that
        // feeds the tone list and the parameter text is rebuilt before anything reads it.
        cacheEmu88Names();

        // The board has just booted and drops MIDI until it is up, so the programs go
        // out on the boot-settled tick instead of right now.
        m_pendingEmu88PartResend.store(true);
        if(!m_pendingResend.load())
        {
            m_resendBlocksRemaining = 100;
            m_pendingResend.store(true);
        }

        // Point the editor at the saved part; this reloads its tone list and picks up
        // that part's program from the cache.
        if(m_paramPool)
            m_paramPool->setPartChannel(static_cast<uint8_t>(part));
        loadEmu88ToneNames();
    }

    void HeadlessProcessor::sendSysex(const std::vector<uint8_t>& data)
    {
        if(data.empty())
            return;
        synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
        ev.sysex.assign(data.begin(), data.end());
        addMidiEvent(ev);
    }

    // ── Demo sequence ─────────────────────────────────────────────────────────
    //
    // Auditions the loaded board without external MIDI: four melodic parts plus drums,
    // each with its own program, so it shows off the multitimbral voice rather than one
    // instrument. Sequenced on the audio thread with sample offsets.
    //
    // ch 0 piano, ch 1 strings, ch 2 bass, ch 3 brass, ch 9 drums.

    namespace
    {
        // Channels the demo sets a program on, and so has to hand back afterwards.
        constexpr uint8_t g_demoChannels[] = {0, 1, 2, 3, 9};

        // Time-sorted so one index walks the whole sequence. Kept constant rather than
        // built on first use: this is read from the audio thread.
        constexpr struct { double seconds; uint8_t a, b, c; } g_demoSteps[] =
        {
            // Setup: bank select MSB/LSB then program, per part. Drums take no bank.
            {0.00, 0xB0,  0,  0}, {0.00, 0xB0, 32,  0}, {0.00, 0xC0,  0, 0},  // piano
            {0.00, 0xB1,  0,  0}, {0.00, 0xB1, 32,  0}, {0.00, 0xC1, 48, 0},  // strings
            {0.00, 0xB2,  0,  0}, {0.00, 0xB2, 32,  0}, {0.00, 0xC2, 33, 0},  // bass
            {0.00, 0xB3,  0,  0}, {0.00, 0xB3, 32,  0}, {0.00, 0xC3, 61, 0},  // brass
            {0.00, 0xC9,  0,  0},                                             // drum kit

            // Bar 1: chord on piano, root on bass, kick + hat.
            {0.50, 0x90, 60, 100}, {0.50, 0x90, 64, 100}, {0.50, 0x90, 67, 100},
            {0.50, 0x92, 36,  110},
            {0.50, 0x99, 36, 120}, {0.52, 0x89, 36, 0},
            {0.50, 0x99, 42,  80}, {0.52, 0x89, 42, 0},
            {0.75, 0x99, 42,  70}, {0.77, 0x89, 42, 0},

            // Strings enter under the chord.
            {1.00, 0x91, 55,  70}, {1.00, 0x91, 60,  70},
            {1.00, 0x99, 38, 110}, {1.02, 0x89, 38, 0},   // snare
            {1.00, 0x99, 42,  80}, {1.02, 0x89, 42, 0},
            {1.25, 0x99, 42,  70}, {1.27, 0x89, 42, 0},

            {1.50, 0x80, 60, 0},   {1.50, 0x80, 64, 0},   {1.50, 0x80, 67, 0},
            {1.50, 0x82, 36, 0},

            // Bar 2: F major, brass stab on top.
            {1.50, 0x90, 65, 100}, {1.50, 0x90, 69, 100}, {1.50, 0x90, 72, 100},
            {1.50, 0x92, 41, 110},
            {1.50, 0x93, 77,  95},
            {1.50, 0x99, 36, 120}, {1.52, 0x89, 36, 0},
            {1.50, 0x99, 42,  80}, {1.52, 0x89, 42, 0},
            {1.75, 0x99, 42,  70}, {1.77, 0x89, 42, 0},

            {1.90, 0x83, 77, 0},
            {2.00, 0x91, 55, 0},   {2.00, 0x91, 60, 0},
            {2.00, 0x91, 57,  70}, {2.00, 0x91, 65,  70},
            {2.00, 0x99, 38, 110}, {2.02, 0x89, 38, 0},
            {2.00, 0x99, 42,  80}, {2.02, 0x89, 42, 0},
            {2.25, 0x99, 42,  70}, {2.27, 0x89, 42, 0},

            // Bar 3: back to C, everything together, then release.
            {2.50, 0x80, 65, 0},   {2.50, 0x80, 69, 0},   {2.50, 0x80, 72, 0},
            {2.50, 0x82, 41, 0},
            {2.50, 0x90, 60,  100},{2.50, 0x90, 64, 100}, {2.50, 0x90, 67, 100},
            {2.50, 0x90, 72, 100},
            {2.50, 0x92, 36, 110},
            {2.50, 0x93, 79,  95},
            {2.50, 0x99, 36, 120}, {2.52, 0x89, 36, 0},
            {2.50, 0x99, 49, 100}, {2.52, 0x89, 49, 0},   // crash

            {3.50, 0x83, 79, 0},
            {3.50, 0x91, 57, 0},   {3.50, 0x91, 65, 0},
            {4.00, 0x80, 60, 0},   {4.00, 0x80, 64, 0},   {4.00, 0x80, 67, 0},
            {4.00, 0x80, 72, 0},   {4.00, 0x82, 36, 0},
        };
    }

    void HeadlessProcessor::triggerDemoSequence()
    {
        // Snapshot here, on the message thread that owns the cache: the audio thread
        // replays it when the demo ends, and this is the state the user expects back.
        for(size_t i = 0; i < m_demoSeqRestore.size(); ++i)
            m_demoSeqRestore[i] = m_emu88PartPrograms[g_demoChannels[i]];
        m_demoSeqArmed.store(true);
    }

    void HeadlessProcessor::processDemoSequence(const uint32_t numSamples, const double sampleRate)
    {
        if(m_demoSeqArmed.exchange(false))
        {
            m_demoSeqRunning = true;
            m_demoSeqSamples = 0;
            m_demoSeqIndex   = 0;
        }

        if(!m_demoSeqRunning || sampleRate <= 0.0)
            return;

        constexpr size_t stepCount = std::size(g_demoSteps);

        const auto blockEnd = m_demoSeqSamples + numSamples;
        while(m_demoSeqIndex < stepCount)
        {
            const auto& s = g_demoSteps[m_demoSeqIndex];
            const auto at = static_cast<uint64_t>(s.seconds * sampleRate + 0.5);
            if(at >= blockEnd)
                break;
            ++m_demoSeqIndex;

            synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
            ev.a = s.a;
            ev.b = s.b;
            ev.c = s.c;
            ev.offset = static_cast<uint32_t>(at > m_demoSeqSamples ? at - m_demoSeqSamples : 0);
            addMidiEvent(ev);
        }

        m_demoSeqSamples = blockEnd;

        if(m_demoSeqIndex >= stepCount)
        {
            m_demoSeqRunning = false;
            restoreDemoSequenceParts(numSamples ? numSamples - 1 : 0);
        }
    }

    void HeadlessProcessor::restoreDemoSequenceParts(const uint32_t offset)
    {
        // The demo puts its own tones on the parts it plays, so each one goes back to
        // the program the user had. Unlike sendEmu88PartPrograms this also restores a
        // part cached at program 0, which the demo would otherwise leave on its tone.
        for(size_t i = 0; i < m_demoSeqRestore.size(); ++i)
        {
            const uint8_t ch   = g_demoChannels[i];
            const int     prog = m_demoSeqRestore[i];
            // A drum part reads the program as a kit number and ignores the bank. The demo
            // left its own bank behind, so the user's variation goes back with the tone.
            if(!isEmu88PartRhythm(static_cast<int>(ch)))
            {
                addMidiEvent({synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 0,
                    static_cast<uint8_t>(m_emu88PartBankMsb[ch] & 0x7f), offset});
                addMidiEvent({synthLib::MidiEventSource::Editor,
                    static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch), 32,
                    static_cast<uint8_t>(m_emu88PartBankLsb[ch] & 0x7f), offset});
            }
            addMidiEvent({synthLib::MidiEventSource::Editor,
                static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | ch),
                static_cast<uint8_t>(prog & 0x7f), 0, offset});
        }
    }

    // ── MIDI file playback ────────────────────────────────────────────────────

    namespace
    {
        // Meter levels are fixed point so the whole row stays lock-free.
        constexpr int    kMidiLevelOne         = 1000;
        constexpr double kMidiLevelFallSeconds = 0.4;

        // The board answers a GS reset for roughly a third of a second. Real files open
        // with a GM System On or GS Reset in their first ticks, so playback starts ahead
        // of tick 0 and lets that settle, or every early event is swallowed.
        constexpr double kMidiPlayLeadInSeconds = 0.5;
        // A song's last event is often a note-on, so the tail is kept rather than
        // cutting the final chord off mid-release.
        constexpr double kMidiRenderTailSeconds = 4.0;
        // Time the board is run before a render starts, once after the reset and again
        // after the song's own setup messages. Long enough for a GS reset to land.
        constexpr double kMidiWarmUpSeconds = 0.5;
        constexpr int    kMp3BitRateKbps        = 320;
    }

    // Recent songs are remembered by file name, and the files themselves live in the
    // 88emu data folder, so the list still works after an iOS pick has gone out of scope.
    namespace
    {
        constexpr int kMaxRecentMidiFiles = 10;

        juce::File midiFolder()
        {
            return juce::File(juce::String(HeadlessProcessor::getSynthDataFolder(SynthType::Emu88))
                              + "MIDI/");
        }
    }

    std::vector<std::string> HeadlessProcessor::getRecentMidiFiles()
    {
        std::vector<std::string> result;
        const auto file = juce::File(juce::String(getDataFolder()) + "settings.xml");
        if(!file.existsAsFile())
            return result;

        const auto xml = juce::XmlDocument::parse(file);
        if(!xml)
            return result;

        const auto list = xml->getStringAttribute("recentMidiFiles");
        const auto folder = midiFolder();
        for(const auto& name : juce::StringArray::fromTokens(list, "\n", ""))
        {
            // A name whose copy has been deleted is dropped rather than offered.
            if(name.isNotEmpty() && folder.getChildFile(name).existsAsFile())
                result.push_back(name.toStdString());
            if(static_cast<int>(result.size()) >= kMaxRecentMidiFiles)
                break;
        }
        return result;
    }

    void HeadlessProcessor::addRecentMidiFile(const std::string& fileName)
    {
        if(fileName.empty())
            return;

        juce::StringArray names;
        names.add(juce::String(fileName));
        for(const auto& existing : getRecentMidiFiles())
            names.addIfNotAlreadyThere(juce::String(existing));
        while(names.size() > kMaxRecentMidiFiles)
            names.remove(names.size() - 1);

        const auto file = juce::File(juce::String(getDataFolder()) + "settings.xml");
        std::unique_ptr<juce::XmlElement> xml;
        if(file.existsAsFile())
            xml = juce::XmlDocument::parse(file);
        if(!xml)
            xml = std::make_unique<juce::XmlElement>("RetromulatorSettings");

        xml->setAttribute("recentMidiFiles", names.joinIntoString("\n"));
        xml->writeTo(file);
    }

    bool HeadlessProcessor::loadRecentMidiFile(const std::string& fileName)
    {
        const auto file = midiFolder().getChildFile(juce::String(fileName));
        juce::MemoryBlock block;
        if(!file.existsAsFile() || !file.loadFileAsData(block) || block.getSize() == 0)
            return false;

        const auto* raw = static_cast<const uint8_t*>(block.getData());
        std::vector<uint8_t> data(raw, raw + block.getSize());
        if(!loadMidiFile(std::move(data), fileName))
            return false;

        addRecentMidiFile(fileName);
        return true;
    }

    float HeadlessProcessor::getMidiChannelLevel(const int channel) const
    {
        if(channel < 0 || channel >= static_cast<int>(m_midiChannelLevels.size()))
            return 0.0f;
        return static_cast<float>(m_midiChannelLevels[static_cast<size_t>(channel)]
                                      .load(std::memory_order_relaxed)) / kMidiLevelOne;
    }

    bool HeadlessProcessor::loadMidiFile(std::vector<uint8_t>&& data, const std::string& fileName)
    {
        std::vector<sc88smf::Event> events;
        std::string error;
        if(!sc88smf::parse(data, fileName, events, error))
            return false;

        std::vector<MidiSongEvent> song;
        song.reserve(events.size());
        for(auto& e : events)
            song.push_back({e.seconds, std::move(e.bytes), e.port});

        {
            // The audio thread walks the song list, so the swap waits for the block it
            // may be in the middle of. Everything costly already happened above.
            const juce::ScopedLock sl(getCallbackLock());
            m_midiSongEvents.swap(song);
            m_midiPlayState.store(MidiPlayState::Stopped);
            m_midiPlayRequest.store(false);
            m_midiStopRequest.store(false);
            m_midiEventIndex = 0;
            m_midiPlayPos    = 0.0;
        }

        m_midiFileData = std::move(data);
        m_midiFileName = fileName;
        return true;
    }

    void HeadlessProcessor::playMidiFile()
    {
        if(m_midiSongEvents.empty())
            return;
        m_midiPlayRequest.store(true);
    }

    void HeadlessProcessor::stopMidiFile()
    {
        m_midiStopRequest.store(true);
    }

    void HeadlessProcessor::processMidiFile(const uint32_t numSamples, const double sampleRate)
    {
        const auto state = m_midiPlayState.load();

        if(m_midiStopRequest.exchange(false))
        {
            if(state != MidiPlayState::Stopped)
            {
                resetMidiModule(0);
                m_midiPlayState.store(MidiPlayState::Stopped);
            }
            m_midiPlayRequest.store(false);
            return;
        }

        if(m_midiPlayRequest.exchange(false))
        {
            // Also covers restarting mid-song: the board is handed back clean either way.
            resetMidiModule(0);
            m_midiPlayPos    = -kMidiPlayLeadInSeconds;
            m_midiEventIndex = 0;
            m_midiPlayState.store(MidiPlayState::LeadIn);
        }

        if(m_midiPlayState.load() == MidiPlayState::Stopped || sampleRate <= 0.0 || !numSamples)
            return;

        const double blockSeconds = static_cast<double>(numSamples) / sampleRate;
        const double blockEnd     = m_midiPlayPos + blockSeconds;

        while(m_midiEventIndex < m_midiSongEvents.size())
        {
            const auto& e = m_midiSongEvents[m_midiEventIndex];
            if(e.seconds >= blockEnd)
                break;
            ++m_midiEventIndex;
            if(e.bytes.empty())
                continue;

            // Events before the cursor belong to the lead-in window, not this block.
            const double delta  = e.seconds - m_midiPlayPos;
            const auto   offset = delta <= 0.0 ? 0u
                                : static_cast<uint32_t>(std::min<double>(delta * sampleRate,
                                                                         numSamples - 1));

            synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
            ev.offset = offset;
            ev.port   = e.port;
            if(e.bytes.front() == 0xf0)
            {
                ev.sysex.assign(e.bytes.begin(), e.bytes.end());
                if(ev.sysex.back() != 0xf7)
                    ev.sysex.push_back(0xf7);
                // A song may turn a part into a drum part, which changes what its program
                // means, so the cached view follows the message the board is about to get.
                onEmu88Sysex(ev.sysex);
            }
            else if(e.bytes.size() <= 3)
            {
                ev.a = e.bytes[0];
                ev.b = e.bytes.size() > 1 ? e.bytes[1] : 0;
                ev.c = e.bytes.size() > 2 ? e.bytes[2] : 0;
                // A song owns every part's tone while it plays, so the cached programs
                // follow it rather than drifting out of sync with the board.
                if((ev.a & 0xf0) == synthLib::M_PROGRAMCHANGE)
                    onEmu88ProgramChange(ev.a & 0x0f, ev.b);
                else if((ev.a & 0xf0) == synthLib::M_CONTROLCHANGE && (ev.b == 0 || ev.b == 32))
                    onEmu88BankSelect(ev.a & 0x0f, ev.b, ev.c);
                // A note-on drives that channel's meter bar to its velocity.
                else if((ev.a & 0xf0) == synthLib::M_NOTEON && ev.c)
                {
                    auto& level = m_midiChannelLevels[ev.a & 0x0f];
                    const auto v = static_cast<uint16_t>((ev.c * kMidiLevelOne) / 127);
                    if(v > level.load(std::memory_order_relaxed))
                        level.store(v, std::memory_order_relaxed);
                }
            }
            else
                continue;
            addMidiEvent(ev);
        }

        m_midiPlayPos = blockEnd;
        if(m_midiPlayPos >= 0.0)
            m_midiPlayState.store(MidiPlayState::Playing);

        // Decay the meter here rather than on read: the fall rate then follows the
        // block clock instead of however often the editor happens to repaint.
        const auto fall = static_cast<uint16_t>(
            std::max(1.0, kMidiLevelOne * blockSeconds / kMidiLevelFallSeconds));
        for(auto& level : m_midiChannelLevels)
        {
            const auto v = level.load(std::memory_order_relaxed);
            if(v)
                level.store(v > fall ? static_cast<uint16_t>(v - fall) : uint16_t{0},
                            std::memory_order_relaxed);
        }

        const bool finished = m_midiEventIndex >= m_midiSongEvents.size()
            && (m_midiSongEvents.empty()
                || m_midiPlayPos >= m_midiSongEvents.back().seconds + kMidiRenderTailSeconds);
        if(finished)
        {
            // The song's own tail is what the board is left holding, and it may well be
            // a fade to zero volume, so the end of a song resets like a stop does.
            resetMidiModule(numSamples - 1);
            m_midiPlayState.store(MidiPlayState::Stopped);
        }
    }

    // ── Offline WAV render ────────────────────────────────────────────────────

    bool HeadlessProcessor::startMidiRender(const juce::URL& destUrl, const RenderFormat format)
    {
        if(m_synthType != SynthType::Emu88 || m_midiSongEvents.empty() || destUrl.isEmpty())
            return false;
        if(m_renderActive.exchange(true))
            return false;   // one at a time

        stopMidiFile();
        m_renderCancel.store(false);
        m_renderProgress.store(0.0f);

        if(m_renderThread && m_renderThread->joinable())
            m_renderThread->join();
        m_renderThread = std::make_unique<std::thread>([this, destUrl, format]
        {
            renderMidiToWav(destUrl, format);
            m_renderActive.store(false);
        });
        return true;
    }

    void HeadlessProcessor::cancelMidiRender()
    {
        m_renderCancel.store(true);
    }

    void HeadlessProcessor::renderMidiToWav(const juce::URL& destUrl, const RenderFormat format)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int    bitDepth   = 24;
        constexpr int    blockSize  = 512;

        // Rendered here first. The chosen location may be a security-scoped iCloud or
        // Files URL, which cannot be written to as a plain path.
        const juce::File dest = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("retromulator_render.wav");
        dest.deleteFile();

        std::unique_ptr<juce::FileOutputStream> out(dest.createOutputStream());
        if(!out)
            return;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sampleRate, 2, bitDepth, {}, 0));
        if(!writer)
            return;
        out.release();   // the writer owns the stream from here

        // Live audio must not touch the board while this runs: there is one device and
        // the render drives it at its own pace.
        suspendProcessing(true);
        // Preferred device rate 0, as the live path passes: the board picks its native
        // rate and the resampler bridges to 48 kHz.
        getPlugin().setHostSamplerate(static_cast<float>(sampleRate), 0.0f);
        getPlugin().setBlockSize(blockSize);
        // The board renders on its own thread, and the pump below outruns it. Offline
        // mode makes processAudio wait for each frame instead of repeating the last one.
        if(auto* dev = getEmu88Device())
            dev->setOfflineRender(true);

        // The song's own tail plus the lead-in it needs before tick 0.
        const double songEnd = m_midiSongEvents.back().seconds;
        const double total   = songEnd + kMidiRenderTailSeconds;

        // Local cursors: the live playback members belong to the audio thread.
        double pos        = -kMidiPlayLeadInSeconds;
        size_t eventIndex = 0;
        resetMidiModule(0);

        juce::AudioBuffer<float> buffer(2, blockSize);
        std::vector<synthLib::SMidiEvent> midiOut;

        // Run the board on for a moment before the song starts. A GS reset takes the
        // module a while to work through, and these files put their program changes on
        // tick 0 together with the first notes, so without this the reset is still
        // settling when they arrive and the notes sound on whatever patch was loaded
        // before. Rendering into a scratch buffer keeps it out of the file.
        const auto spin = [&](const double seconds)
        {
            const auto blocks = static_cast<int>(seconds * sampleRate / blockSize);
            for(int i = 0; i < blocks && !m_renderCancel.load(); ++i)
            {
                buffer.clear();
                float* outs[2] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
                const synthLib::TAudioInputs  ins{};
                const synthLib::TAudioOutputs outputs{outs[0], outs[1]};
                getPlugin().process(ins, outputs, blockSize, 120.0f, 0.0f, false);
                getPlugin().getMidiOut(midiOut);
                midiOut.clear();
            }
        };
        spin(kMidiWarmUpSeconds);

        // Then the song's own setup: everything it puts on tick 0 that is not a note,
        // so the programs, volumes and pans are in place before the first note-on. They
        // are consumed here, so the pump below starts at the first event it left.
        for(; eventIndex < m_midiSongEvents.size(); ++eventIndex)
        {
            const auto& e = m_midiSongEvents[eventIndex];
            if(e.seconds > 0.0)
                break;
            if(e.bytes.empty())
                continue;
            const auto status = static_cast<uint8_t>(e.bytes.front() & 0xf0);
            if(status == synthLib::M_NOTEON || status == synthLib::M_NOTEOFF)
                break;

            synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
            ev.port = e.port;
            if(e.bytes.front() == 0xf0)
            {
                ev.sysex.assign(e.bytes.begin(), e.bytes.end());
                if(ev.sysex.back() != 0xf7)
                    ev.sysex.push_back(0xf7);
            }
            else if(e.bytes.size() <= 3)
            {
                ev.a = e.bytes[0];
                ev.b = e.bytes.size() > 1 ? e.bytes[1] : 0;
                ev.c = e.bytes.size() > 2 ? e.bytes[2] : 0;
            }
            else
                continue;
            getPlugin().addMidiEvent(ev);
        }
        spin(kMidiWarmUpSeconds);

        while(pos < total && !m_renderCancel.load())
        {
            buffer.clear();

            // Same event pump as live playback, so the file matches what was heard.
            const double blockSeconds = blockSize / sampleRate;
            const double blockEnd     = pos + blockSeconds;
            while(eventIndex < m_midiSongEvents.size())
            {
                const auto& e = m_midiSongEvents[eventIndex];
                if(e.seconds >= blockEnd)
                    break;
                ++eventIndex;
                if(e.bytes.empty())
                    continue;

                const double delta  = e.seconds - pos;
                const auto   offset = delta <= 0.0 ? 0u
                    : static_cast<uint32_t>(std::min<double>(delta * sampleRate, blockSize - 1));

                synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
                ev.offset = offset;
                ev.port   = e.port;
                if(e.bytes.front() == 0xf0)
                {
                    ev.sysex.assign(e.bytes.begin(), e.bytes.end());
                    if(ev.sysex.back() != 0xf7)
                        ev.sysex.push_back(0xf7);
                }
                else if(e.bytes.size() <= 3)
                {
                    ev.a = e.bytes[0];
                    ev.b = e.bytes.size() > 1 ? e.bytes[1] : 0;
                    ev.c = e.bytes.size() > 2 ? e.bytes[2] : 0;
                }
                else
                    continue;
                getPlugin().addMidiEvent(ev);
            }
            pos = blockEnd;

            float* outs[2] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
            const synthLib::TAudioInputs  ins{};
            const synthLib::TAudioOutputs outputs{outs[0], outs[1]};
            getPlugin().process(ins, outputs, blockSize, 120.0f, 0.0f, false);
            getPlugin().getMidiOut(midiOut);
            midiOut.clear();

            writer->writeFromAudioSampleBuffer(buffer, 0, blockSize);

            const double done = (pos + kMidiPlayLeadInSeconds)
                              / (total + kMidiPlayLeadInSeconds);
            m_renderProgress.store(static_cast<float>(juce::jlimit(0.0, 1.0, done)));
        }

        writer.reset();   // flushes the header

        // MP3 is encoded from the finished WAV, so the emulation path is the same one
        // for both formats and only the container differs.
        juce::File source = dest;
        if(!m_renderCancel.load() && format == RenderFormat::Mp3)
        {
            const auto mp3 = dest.getSiblingFile("retromulator_render.mp3");
            mp3.deleteFile();
            if(encodeWavToMp3(dest.getFullPathName().toStdString(),
                              mp3.getFullPathName().toStdString(), kMp3BitRateKbps))
            {
                source = mp3;
                dest.deleteFile();
            }
        }

        if(m_renderCancel.load())
        {
            dest.deleteFile();
        }
        else
        {
            // Copy out to the picked location. The URL stream goes first: a chosen iOS
            // location is security-scoped, and getLocalFile still hands back a path there
            // that a plain copy cannot write to.
            bool copied = false;
            if(auto outStream = destUrl.createOutputStream())
            {
                juce::FileInputStream in(source);
                if(in.openedOk())
                {
                    copied = outStream->writeFromInputStream(in, -1) > 0;
                    outStream->flush();
                }
            }
            if(!copied)
            {
                const auto destFile = destUrl.getLocalFile();
                if(destFile.getFullPathName().isNotEmpty())
                {
                    destFile.deleteFile();
                    copied = source.copyFileTo(destFile);
                }
            }
            if(copied)
                source.deleteFile();
        }

        resetMidiModule(0);
        m_renderProgress.store(1.0f);

        // Hand the board back to live audio at the rate it was using.
        if(auto* dev = getEmu88Device())
            dev->setOfflineRender(false);
        getPlugin().setHostSamplerate(static_cast<float>(getSampleRate()), 0.0f);
        getPlugin().setBlockSize(static_cast<uint32_t>(getBlockSize()));
        suspendProcessing(false);
    }

    void HeadlessProcessor::resetMidiModule(const uint32_t offset)
    {
        // Nothing is sounding after this, so the meter must not freeze mid-height.
        for(auto& level : m_midiChannelLevels)
            level.store(0, std::memory_order_relaxed);

        // A render must reach the board and nothing else: the routing matrix would also
        // hand these to the controller and out the physical MIDI port.
        const bool direct = m_renderActive.load();
        const auto send = [this, direct](const synthLib::SMidiEvent& ev)
        {
            if(direct) getPlugin().addMidiEvent(ev);
            else       addMidiEvent(ev);
        };

        for(uint8_t ch = 0; ch < 16; ++ch)
        {
            const auto cc = static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | ch);
            send({synthLib::MidiEventSource::Editor, cc, synthLib::M_ALLNOTESOFF, 0, offset});
            send({synthLib::MidiEventSource::Editor, cc, 120, 0, offset});  // all sound off
            send({synthLib::MidiEventSource::Editor, cc, 121, 0, offset});  // reset controllers
            send({synthLib::MidiEventSource::Editor, cc, 7, 100, offset});  // channel volume
            send({synthLib::MidiEventSource::Editor, cc, 11, 127, offset}); // expression
        }

        static constexpr uint8_t gsReset[] =
            {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
        synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
        ev.sysex.assign(std::begin(gsReset), std::end(gsReset));
        ev.offset = offset;
        send(ev);
    }

    void HeadlessProcessor::pollEmu88Params()
    {
        if(!m_paramPool || m_synthType != SynthType::Emu88)
            return;

        // An async boot names the board before the device exists, so the list is either
        // empty or still the previous board's. Either way the model it was read from no
        // longer matches the one now running, and the names are read again.
        const int bootedModel = getEmu88Model();
        if(bootedModel >= 0 && bootedModel != m_emu88NamesModel)
            loadEmu88ToneNames();

        // The part menu and the tone list are driven from the editor, so mirror them
        // into their host parameters to keep automation lanes showing the real state.
        m_paramPool->setNativeSlotValue(ParameterPool::kEmu88PartNative, getEmu88Part(), true, false);
        m_paramPool->setNativeSlotValue(ParameterPool::kEmu88ProgramNative,
                                        juce::jlimit(0, 127, m_currentProgram), true, false);
        // The variation rows in the bank combo and an incoming CC0/CC32 both move these,
        // so the lanes follow the part's real bank.
        const int shownPart = getEmu88Part();
        m_paramPool->setNativeSlotValue(ParameterPool::kEmu88BankMsbNative,
            juce::jlimit(0, 127, m_emu88PartBankMsb[static_cast<size_t>(shownPart)]), true, false);
        m_paramPool->setNativeSlotValue(ParameterPool::kEmu88BankLsbNative,
            juce::jlimit(0, 127, m_emu88PartBankLsb[static_cast<size_t>(shownPart)]), true, false);
        // The transport also moves from the C1/D1 keys and the bank combo, and it stops on
        // its own at the end of a song, so the lane follows it rather than the other way
        // round. Transient: it is not a sound edit and must not be saved or resent.
        m_paramPool->setNativeSlotValue(ParameterPool::kEmu88TransportNative,
                                        isMidiFilePlaying() ? 1 : 0, true, true);

        // A song moved the shown part's variation, or made it a drum part, so what its
        // programs are called changed and the editor reads the list again.
        if(m_emu88ToneListDirty.exchange(false, std::memory_order_acquire))
        {
            loadEmu88ToneNames();
            updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        }

        // A program change from a MIDI file or external gear moved the cache on the audio
        // thread; mirror every part so the lanes and the tone list follow the board.
        if(m_emu88PartProgramDirty.exchange(false, std::memory_order_acquire))
        {
            const int part = getEmu88Part();
            const int prog = m_emu88PartPrograms[static_cast<size_t>(part)];
            if(prog != m_currentProgram)
            {
                m_currentProgram = prog;
                if(prog >= 0 && prog < static_cast<int>(m_programNames.size()))
                    m_patchName = m_programNames[static_cast<size_t>(prog)];
                updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
            }
        }

        for(size_t p = 0; p < m_emu88PartPrograms.size(); ++p)
        {
            m_paramPool->setNativeSlotValue(ParameterPool::kEmu88PartProgFirst + static_cast<int>(p),
                                            juce::jlimit(0, 127, m_emu88PartPrograms[p]), true, false);
        }
    }

    void HeadlessProcessor::cacheEmu88Names()
    {
        const int model = getEmu88Model();
        // An async boot runs this while the DummyDevice is still installed, so the board
        // it would name does not exist yet. The cache is marked stale and left for the
        // retry in pollEmu88Params, which runs once the device is up.
        if(model < 0)
        {
            m_emu88NamesModel = -1;
            return;
        }
        m_emu88ToneNames.clear();
        m_emu88KitNames.clear();
        m_emu88NamesModel = model;
        const auto m = static_cast<emu88Lib::DeviceModel>(model);
        m_emu88ToneNames = emu88LoadCapitalTones(m);
        m_emu88KitNames  = emu88LoadDrumKits(m);

        // The host caches parameter text when it builds its tree, so the descriptions
        // are rebuilt with this board's names rather than named on demand.
        if(m_paramPool)
            m_paramPool->refreshFor(SynthType::Emu88);
    }

    std::string HeadlessProcessor::getEmu88ProgramName(const int part, const int program) const
    {
        if(program < 0 || part < 0 || part > 15)
            return {};
        if(!isEmu88PartRhythm(part))
        {
            // On a variation the ROM's capital-tone name is the wrong one, so the tone
            // table names the variation the part actually plays.
            const int cc0 = m_emu88PartBankMsb[static_cast<size_t>(part)];
            if(cc0 > 0)
            {
                const auto model = static_cast<emu88Lib::DeviceModel>(getEmu88Model());
                const auto maps  = emu88Maps(model);
                const int  cc32  = m_emu88PartBankLsb[static_cast<size_t>(part)];
                const int  map   = cc32 > 0 ? cc32 : maps.empty() ? 0 : maps.back();
                for(const auto& v : emu88Variations(model, map, program))
                    if(v.cc0 == cc0)
                        return v.name;
            }
        }

        const auto& names = isEmu88PartRhythm(part) ? m_emu88KitNames : m_emu88ToneNames;
        if(program >= static_cast<int>(names.size()))
            return {};
        return names[static_cast<size_t>(program)];
    }

    void HeadlessProcessor::loadEmu88ToneNames()
    {
        const int  model = getEmu88Model();
        const bool drums = isEmu88PartRhythm(getEmu88Part());
        if(model != m_emu88NamesModel || (m_emu88ToneNames.empty() && m_emu88KitNames.empty()))
            cacheEmu88Names();
        m_programNames = drums ? m_emu88KitNames : m_emu88ToneNames;
        // A drum part whose kit table was not recognized still has 128 selectable
        // programs; list them by number rather than hiding the part.
        if(drums && m_programNames.empty())
            m_programNames.assign(128, std::string{});
        m_bankMessages.clear();
        // Each part keeps its own tone, so show the one this part already has rather
        // than resetting the view to 0 while the board still plays the old program.
        const int part = getEmu88Part();
        const int progCount = static_cast<int>(m_programNames.size());
        m_currentProgram = juce::jlimit(0, std::max(0, progCount - 1), m_emu88PartPrograms[static_cast<size_t>(part)]);
        m_patchName = (m_currentProgram < progCount)
            ? m_programNames[static_cast<size_t>(m_currentProgram)] : std::string{};

        // The list is one row per program change, so it names capital tones. The row the
        // part is on names the tone it actually plays, which on a variation is not the
        // capital one.
        if(!drums && m_currentProgram < progCount)
        {
            const auto played = getEmu88ProgramName(part, m_currentProgram);
            if(!played.empty())
            {
                m_programNames[static_cast<size_t>(m_currentProgram)] = played;
                m_patchName = played;
            }
        }
        if(m_programNames.empty() || (drums && m_programNames.front().empty()))
            fprintf(stderr, "[88emu] %s directory not found in ROM for model %d\n", drums ? "drum kit" : "tone", model);
    }

    ayumiLib::Device* HeadlessProcessor::getAyumiDevice() const
    {
        if(m_synthType != SynthType::Ayumi)
            return nullptr;
        return dynamic_cast<ayumiLib::Device*>(m_device.get());
    }

    int HeadlessProcessor::getAyumiEngine() const
    {
        auto* dev = getAyumiDevice();
        return dev ? dev->getEngine() : 0;
    }

    void HeadlessProcessor::setAyumiEngine(int isAY)
    {
        if(auto* dev = getAyumiDevice())
            dev->setEngine(isAY);
    }

    void HeadlessProcessor::updateAyumiProgramList(const std::string& bankFolder)
    {
        auto* dev = getAyumiDevice();
        if(!dev) return;

        std::vector<std::string> paths;
        juce::File folder(bankFolder);
        if(folder.isDirectory())
        {
            juce::Array<juce::File> files;
            folder.findChildFiles(files, juce::File::findFiles, false, "*.ay");
            files.sort();
            for(const auto& f : files)
                paths.push_back(f.getFullPathName().toStdString());
        }
        dev->setProgramList(std::move(paths));
    }

    void HeadlessProcessor::pollAyumiProgramChange()
    {
        auto* dev = getAyumiDevice();
        if(!dev) return;

        const int idx = dev->takePendingProgramChange();
        if(idx < 0) return;

        const auto& list = dev->getProgramList();
        if(idx >= static_cast<int>(list.size())) return;

        const juce::File f(list[static_cast<size_t>(idx)]);
        loadPresetFromFile(f.getFullPathName().toStdString(),
                           f.getFileNameWithoutExtension().toStdString());
    }

    bool HeadlessProcessor::loadSoundFile(const std::string& filePath)
    {
        auto* dev = getAkaiDevice();
        if(!dev)
            return false;

        if(!dev->loadSoundFile(filePath))
            return false;

        m_sysexFilePath = filePath;
        m_patchName     = juce::File(filePath).getFileNameWithoutExtension().toStdString();
        m_sysexData.clear();
        m_bankMessages.clear();
        m_bankStride = 1;
        m_akaiIsoMode = false;
        m_akaiIsoPath.clear();
        m_akaiSliceCount = 0;

        // Populate program names from SF2/ZBP presets
        const int presetCount = dev->getPresetCount();
        if(presetCount > 0)
        {
            m_programNames.resize(static_cast<size_t>(presetCount));
            for(int i = 0; i < presetCount; ++i)
                m_programNames[static_cast<size_t>(i)] = dev->getPresetName(i);
            m_bankMessages.resize(static_cast<size_t>(presetCount));
        }
        else
        {
            // Single-file sound (SFZ/WAV): one program entry with filename as name
            m_programNames = { m_patchName };
            m_bankMessages.resize(1);
        }
        m_currentProgram = 0;

        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }

    bool HeadlessProcessor::loadAkaiIso(const std::string& filePath)
    {
        auto* dev = getAkaiDevice();
        if(!dev)
            return false;

        if(!dev->loadIsoFile(filePath))
            return false;

        m_akaiIsoMode = true;
        m_akaiIsoPath = filePath;
        m_sysexFilePath = filePath;
        m_patchName = dev->getIsoPresetName(0);
        m_sysexData.clear();
        m_bankMessages.clear();
        m_bankStride = 1;

        // Populate program names from ISO
        const int isoCount = dev->getIsoPresetCount();
        m_programNames.resize(static_cast<size_t>(isoCount));
        for(int i = 0; i < isoCount; ++i)
            m_programNames[static_cast<size_t>(i)] = dev->getIsoPresetName(i);
        m_bankMessages.resize(static_cast<size_t>(isoCount));
        m_currentProgram = 0;

        // Clear browse mode when loading ISO
        m_akaiBrowseFolder.clear();
        m_akaiSliceCount = 0;

        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }

    bool HeadlessProcessor::selectAkaiIsoPreset(int index)
    {
        auto* dev = getAkaiDevice();
        if(!dev || !dev->isIsoLoaded())
            return false;

        if(!dev->selectIsoPreset(index))
            return false;

        m_currentProgram = index;
        if(index >= 0 && index < static_cast<int>(m_programNames.size()))
            m_patchName = m_programNames[static_cast<size_t>(index)];

        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
    }

    bool HeadlessProcessor::selectSoundPreset(int index)
    {
        auto* dev = getAkaiDevice();
        if(!dev)
            return false;

        if(!dev->selectPreset(index))
            return false;

        m_currentProgram = index;
        if(index >= 0 && index < static_cast<int>(m_programNames.size()))
            m_patchName = m_programNames[static_cast<size_t>(index)];

        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true));
        return true;
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

    // ── Virus bank conversion export ────────────────────────────────────────────

    int HeadlessProcessor::exportConvertedVirusBank(const std::string& destPath, char targetVersion) const
    {
#ifdef CUSTOM
        if(m_bankMessages.empty())
            return -1;

        if(m_synthType != SynthType::VirusABC && m_synthType != SynthType::VirusTI)
            return -1;

        virusLib::PresetVersion target;
        switch(targetVersion)
        {
        case 'A': case 'a': target = virusLib::A; break;
        case 'B': case 'b': target = virusLib::B; break;
        case 'C': case 'c': target = virusLib::C; break;
        default: return -1;
        }

        // Make a mutable copy of the bank messages
        auto messages = m_bankMessages;
        const int converted = virusLib::PresetConverter::convertSysexBank(messages, target);

        // Write the converted bank to disk
        std::ofstream f(destPath, std::ios::binary | std::ios::trunc);
        if(!f.is_open())
            return -1;

        for(const auto& msg : messages)
            f.write(reinterpret_cast<const char*>(msg.data()),
                    static_cast<std::streamsize>(msg.size()));

        return f.good() ? converted : -1;
#else
        (void)destPath; (void)targetVersion;
        return -1;
#endif
    }

    // ── Track program changes so cached per-part programs follow the board ──

    void HeadlessProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        if(m_synthType == SynthType::Emu88)
        {
            // A MIDI file or external gear can set every part's tone, so the cached
            // programs follow the board rather than drifting out of sync with it.
            for(const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if(m.isProgramChange())
                    onEmu88ProgramChange(m.getChannel() - 1, m.getProgramChangeNumber());
                else if(m.isController() && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32))
                    onEmu88BankSelect(m.getChannel() - 1, m.getControllerNumber(), m.getControllerValue());
                else if(m.isSysEx())
                {
                    const auto* d = m.getSysExData();
                    // getSysExData omits the F0 and F7 the parser expects.
                    std::vector<uint8_t> sx;
                    sx.reserve(static_cast<size_t>(m.getSysExDataSize()) + 2);
                    sx.push_back(0xf0);
                    sx.insert(sx.end(), d, d + m.getSysExDataSize());
                    sx.push_back(0xf7);
                    onEmu88Sysex(sx);
                }
            }

            // C1 and D1 start and stop the loaded song. They are consumed here so they
            // never reach the board, which would sound a note under the transport.
            if(hasMidiFile())
            {
                juce::MidiBuffer kept;
                for(const auto meta : midi)
                {
                    const auto m = meta.getMessage();
                    if(m.isNoteOnOrOff() && (m.getNoteNumber() == kMidiPlayNote ||
                                             m.getNoteNumber() == kMidiStopNote))
                    {
                        if(m.isNoteOn())
                        {
                            if(m.getNoteNumber() == kMidiPlayNote) playMidiFile();
                            else                                   stopMidiFile();
                        }
                        continue;
                    }
                    kept.addEvent(m, meta.samplePosition);
                }
                midi.swapWith(kept);
            }

            processDemoSequence(static_cast<uint32_t>(buffer.getNumSamples()), getSampleRate());
            processMidiFile(static_cast<uint32_t>(buffer.getNumSamples()), getSampleRate());
        }
        // Runs for every core, and from the audio path rather than a host callback that
        // only fires when a playhead offers a BPM.
        processDeferredResend();

        Processor::processBlock(buffer, midi);
    }

    // ── Deferred resend (pre-audio guard + boot delay) ─────────────────────────

    void HeadlessProcessor::processDeferredResend()
    {
        // Resend the current bank message after the DSP is ready.
        // Three cases arm m_pendingResend:
        //   1. AU XPC: prepareToPlay not yet called when UI fires sendBankMessage.
        //   2. JE-8086: firmware silently discards MIDI in the first ~0.14s of emulation.
        //   3. 88emu: a board switch reboots the core, and the parts have to be replayed
        //      once it is up.
        if(m_pendingResend.load())
        {
            if(--m_resendBlocksRemaining <= 0)
            {
                m_pendingResend.store(false);
                m_deviceBooted = true;  // boot delay done; no more auto-resends
                if(m_currentProgram >= 0 && m_currentProgram < getProgramCount())
                    sendBankMessage(m_currentProgram);
                // 88emu keeps a program per part, and the board only accepts them once
                // it is up, so the replay waits for this tick rather than boot time.
                if(m_pendingEmu88PartResend.exchange(false))
                    sendEmu88PartPrograms();
                // Re-apply host parameter edits the bank message just overwrote.
                if(m_paramPool)
                    m_paramPool->resendTouched();
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

        // The restored board applies to this boot only: once it is consumed a later
        // switch from the editor goes through SynthFactory's shared value as before.
        const int model = m_restoredEmu88Model;
        m_restoredEmu88Model = -1;

        // Let DeviceException propagate — caller (rebootDevice or getPlugin) handles it.
        return SynthFactory::create(m_synthType, m_romPath, model);
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

    // "E88P": 88emu per-part program block appended after the params block.
    static constexpr int32_t kEmu88PartsMagic = 0x50383845;
    // "E88B": 88emu per-part variation banks, appended after the per-part program block.
    static constexpr int32_t kEmu88BanksMagic = 0x42383845;
    // "E88R": 88emu per-part rhythm assignment, appended after the bank block.
    static constexpr int32_t kEmu88RhythmMagic = 0x52383845;
    // "E88M": the loaded MIDI song, appended after the per-part block.
    static constexpr int32_t kEmu88MidiMagic = 0x4D383845;
    // "E88D": the board. Written last, and read back from the tail before the device is
    // created, so the boot picks the right one without parsing the blocks in between.
    static constexpr int32_t kEmu88BoardMagic = 0x44383845;

    void HeadlessProcessor::getStateInformation(juce::MemoryBlock& destData)
    {
        destData.setSize(0);
        appendInt32 (destData, static_cast<int32_t>(m_synthType));
        appendString(destData, m_romPath);
        appendString(destData, m_sysexFilePath);
        appendString(destData, m_patchName);

        // Akai S1000: no embedded sysex data — always loads from file path
        if(m_synthType == SynthType::AkaiS1000)
        {
            std::vector<uint8_t> empty;
            appendBytes(destData, empty);
        }
        else
        {
            appendBytes(destData, m_sysexData);
        }

        appendInt32 (destData, static_cast<int32_t>(m_currentProgram));
        appendInt32 (destData, static_cast<int32_t>(m_savedEditorWidth));
        appendInt32 (destData, static_cast<int32_t>(m_savedEditorHeight));

        // OpenWurli: save full device state blob (volume, tremolo, speaker, mlp, velCurve)
        if(m_synthType == SynthType::OpenWurli)
        {
            std::vector<uint8_t> owState;
            if(auto* ow = const_cast<openWurliLib::Device*>(getOpenWurliDevice()))
                ow->getState(owState, synthLib::StateTypeGlobal);
            appendBytes(destData, owState);
        }

        // Ayumi: save full device state blob (engine + CC1..CC11 + patch name) so
        // live CC edits and the chip variant are recalled on reload.
        if(m_synthType == SynthType::Ayumi)
        {
            std::vector<uint8_t> ayState;
            if(auto* dev = getAyumiDevice())
                dev->getState(ayState, synthLib::StateTypeGlobal);
            appendBytes(destData, ayState);
        }

        // Akai browse folder path (optional, backwards compatible)
        appendString(destData, m_akaiBrowseFolder);

        // Akai extended state: fixed block of 16 int32s for future expansion.
        // [0] = auto-slice count (0=root, 4/8/16/32)
        // [1] = global tuning in cents (CC20)
        // [2] = ISO mode flag (1=ISO loaded, 0=normal)
        // [3..15] = reserved (zero)
        static constexpr int kAkaiReservedSlots = 16;
        auto* akaiDev = getAkaiDevice();
        appendInt32(destData, static_cast<int32_t>(kAkaiReservedSlots));                    // slot count
        appendInt32(destData, static_cast<int32_t>(m_akaiSliceCount));                      // [0]
        appendInt32(destData, static_cast<int32_t>(akaiDev ? akaiDev->getTuneCents() : 0)); // [1]
        appendInt32(destData, static_cast<int32_t>(m_akaiIsoMode ? 1 : 0));                 // [2]
        for(int i = 3; i < kAkaiReservedSlots; ++i)
            appendInt32(destData, 0);                                                        // [3..15]

        // Host parameter slots edited since the last program change:
        // ['PRMS':int32][count:int32][(slot:u8, value:u8) * count]
        static constexpr int32_t kParamsMagic = 0x534d5250; // "PRMS"
        std::vector<uint8_t> slotBytes;
        if(m_paramPool)
        {
            for(const auto& [slot, value] : m_paramPool->getTouchedValues())
            {
                slotBytes.push_back(slot);
                slotBytes.push_back(value);
            }
        }
        appendInt32(destData, kParamsMagic);
        appendInt32(destData, static_cast<int32_t>(slotBytes.size() / 2));
        if(!slotBytes.empty())
            destData.append(slotBytes.data(), slotBytes.size());

        // 88emu per-part programs and the edited part:
        // ['E88P':int32][count:int32][program:int32 * count][part:int32]
        if(m_synthType == SynthType::Emu88)
        {
            appendInt32(destData, kEmu88PartsMagic);
            appendInt32(destData, static_cast<int32_t>(m_emu88PartPrograms.size()));
            for(const int prog : m_emu88PartPrograms)
                appendInt32(destData, static_cast<int32_t>(prog));
            appendInt32(destData, static_cast<int32_t>(getEmu88Part()));
        }

        // 88emu per-part variation banks, in their own block so a session written before
        // they were saved still loads:
        // ['E88B':int32][count:int32][msb:int32 * count][lsb:int32 * count]
        if(m_synthType == SynthType::Emu88)
        {
            appendInt32(destData, kEmu88BanksMagic);
            appendInt32(destData, static_cast<int32_t>(m_emu88PartBankMsb.size()));
            for(const int msb : m_emu88PartBankMsb)
                appendInt32(destData, static_cast<int32_t>(msb));
            for(const int lsb : m_emu88PartBankLsb)
                appendInt32(destData, static_cast<int32_t>(lsb));
        }

        // 88emu per-part rhythm assignment, so a part a song turned into a drum part
        // comes back as one:
        // ['E88R':int32][count:int32][rhythm:int32 * count]
        if(m_synthType == SynthType::Emu88)
        {
            appendInt32(destData, kEmu88RhythmMagic);
            appendInt32(destData, static_cast<int32_t>(m_emu88PartRhythm.size()));
            for(const int r : m_emu88PartRhythm)
                appendInt32(destData, static_cast<int32_t>(r));
        }

        // Loaded MIDI song, so it travels with the session:
        // ['E88M':int32][nameLen:int32][name][dataLen:int32][data]
        if(m_synthType == SynthType::Emu88 && !m_midiFileData.empty())
        {
            appendInt32(destData, kEmu88MidiMagic);
            appendInt32(destData, static_cast<int32_t>(m_midiFileName.size()));
            if(!m_midiFileName.empty())
                destData.append(m_midiFileName.data(), m_midiFileName.size());
            appendInt32(destData, static_cast<int32_t>(m_midiFileData.size()));
            destData.append(m_midiFileData.data(), m_midiFileData.size());
        }

        // Board the session was using, taken from the live device rather than the shared
        // value, so what is saved is what this instance actually booted.
        // ['E88D':int32][model:int32]
        if(m_synthType == SynthType::Emu88)
        {
            const int model = getEmu88Model();
            if(model >= 0)
            {
                appendInt32(destData, kEmu88BoardMagic);
                appendInt32(destData, static_cast<int32_t>(model));
            }
        }
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

        // The board sits in the last eight bytes, and it has to be known before the device
        // is created. Only the tail is examined: the blob also carries raw MIDI and sysex,
        // and scanning those for the tag could match one of their bytes. It goes to this
        // instance's own field, never SynthFactory's shared board, because an AU host
        // restores several instances at once and they would overwrite each other's.
        m_restoredEmu88Model = -1;
        if(newType == SynthType::Emu88 && sizeInBytes >= 8)
        {
            int32_t magic = 0, model = -1;
            std::memcpy(&magic, bytes + sizeInBytes - 8, 4);
            std::memcpy(&model, bytes + sizeInBytes - 4, 4);
            if(magic == kEmu88BoardMagic
               && model >= 0 && model < static_cast<int32_t>(emu88Lib::deviceModelCount()))
                m_restoredEmu88Model = model;
        }

        setSynthType(newType, romPath);

        if(newType == SynthType::AkaiS1000)
        {
            // Akai: reload from the original file path.
            // ISO mode is determined below after reading extended state slots.
            // For now, just store the path — we reload after reading slots.
        }
        else if(newType == SynthType::SID)
        {
            // SID: bank lives in the .sng/.ins/.sid file (no embedded sysex).
            // Reload it and select the saved instrument.
            if(!sysexFilePath.empty() && juce::File(sysexFilePath).existsAsFile())
                loadPresetFromFile(sysexFilePath, patchName, static_cast<int>(savedProgram));
            else
                m_sysexFilePath = sysexFilePath; // path lost; keep for display
        }
        else if(newType == SynthType::Ayumi)
        {
            // Ayumi: reload the .ay preset by path (populates the folder/bank UI),
            // then the device state blob below restores live CC edits + engine.
            if(!sysexFilePath.empty() && juce::File(sysexFilePath).existsAsFile())
                loadPresetFromFile(sysexFilePath, patchName);
            else
                m_sysexFilePath = sysexFilePath; // path lost; keep for display
        }
        else if(newType == SynthType::OPL3)
        {
            // OPL3: patch lives in the .sbi file (no embedded sysex). Reload it so
            // the instrument is recalled on project reopen.
            if(!sysexFilePath.empty() && juce::File(sysexFilePath).existsAsFile())
                loadPresetFromFile(sysexFilePath, patchName);
            else
                m_sysexFilePath = sysexFilePath; // path lost; keep for display
        }
        else
        {
            // loadPreset splits the bank, stores messages, and sends message[savedProgram].
            if(!sysexData.empty())
                loadPreset(sysexData, {}, patchName, static_cast<int>(savedProgram));

            // Restore the saved file path so navigatePatch can find the file in
            // the bank list for cross-bank navigation after a project reload.
            m_sysexFilePath = sysexFilePath;
        }

        // Restore OpenWurli device state blob (optional, backwards compatible)
        if(newType == SynthType::OpenWurli)
        {
            std::vector<uint8_t> owState;
            readBytes(bytes, sizeInBytes, offset, owState);
            if(!owState.empty())
                if(auto* ow = getOpenWurliDevice())
                    ow->setState(owState, synthLib::StateTypeGlobal);
        }

        // Restore Ayumi device state blob (engine + CC1..CC11 + name). Applied after
        // the .ay reload above so live CC edits win over the preset's stored values.
        if(newType == SynthType::Ayumi)
        {
            std::vector<uint8_t> ayState;
            readBytes(bytes, sizeInBytes, offset, ayState);
            if(!ayState.empty())
                if(auto* dev = getAyumiDevice())
                    dev->setState(ayState, synthLib::StateTypeGlobal);
        }

        // Restore Akai browse folder path (optional, backwards compatible)
        std::string browseFolder;
        if(readString(bytes, sizeInBytes, offset, browseFolder))
            m_akaiBrowseFolder = browseFolder;

        // Restore Akai extended state block (optional, backwards compatible)
        int32_t slotCount = 0;
        if(readInt32(bytes, sizeInBytes, offset, slotCount) && slotCount > 0)
        {
            int32_t slots[16] = {};
            const int toRead = std::min(static_cast<int>(slotCount), 16);
            for(int i = 0; i < toRead; ++i)
                readInt32(bytes, sizeInBytes, offset, slots[i]);
            // Skip any extra slots from a newer version
            for(int i = toRead; i < static_cast<int>(slotCount); ++i)
            {
                int32_t dummy = 0;
                readInt32(bytes, sizeInBytes, offset, dummy);
            }
            m_akaiSliceCount = static_cast<int>(slots[0]);
            m_akaiTuneCents  = static_cast<int>(slots[1]);
            m_akaiIsoMode    = (slots[2] != 0);
            // slots[3..15] reserved for future use
        }

        // Reload Akai content now that all state (including ISO flag) has been read
        if(newType == SynthType::AkaiS1000 && !sysexFilePath.empty())
        {
            if(m_akaiIsoMode)
            {
                // Reload from ISO
                loadAkaiIso(sysexFilePath);
                if(savedProgram > 0)
                    selectAkaiIsoPreset(static_cast<int>(savedProgram));
            }
            else
            {
                loadSoundFile(sysexFilePath);
                if(savedProgram > 0)
                    selectSoundPreset(static_cast<int>(savedProgram));
            }

            auto* dev = getAkaiDevice();
            if(dev)
            {
                if(m_akaiSliceCount > 0 && !m_akaiIsoMode)
                    dev->autoSlice(m_akaiSliceCount);
                if(m_akaiTuneCents != 0)
                    dev->setTuneCents(m_akaiTuneCents);
            }
        }

        // Restore host parameter slot edits (optional, appended after the Akai block).
        // Read the header unconditionally so offset stays usable for the blocks after it.
        int32_t magic = 0, count = 0;
        const int paramsStart = offset;
        if(readInt32(bytes, sizeInBytes, offset, magic) && magic == 0x534d5250
           && readInt32(bytes, sizeInBytes, offset, count) && count >= 0
           && offset + count * 2 <= sizeInBytes)
        {
            if(m_paramPool && count > 0)
            {
                std::vector<std::pair<uint8_t, uint8_t>> values;
                values.reserve(static_cast<size_t>(count));
                for(int32_t i = 0; i < count; ++i)
                    values.emplace_back(bytes[offset + i * 2], bytes[offset + i * 2 + 1]);
                m_paramPool->restoreValues(values);
            }
            offset += count * 2;
        }
        else
        {
            // Not a params block (older state): rewind so the next block still parses.
            offset = paramsStart;
        }

        // Restore the 88emu per-part programs and the edited part (optional).
        int restorePart = -1;
        const int partsStart = offset;
        int32_t partsMagic = 0, partsCount = 0;
        if(newType == SynthType::Emu88
           && readInt32(bytes, sizeInBytes, offset, partsMagic) && partsMagic == kEmu88PartsMagic
           && readInt32(bytes, sizeInBytes, offset, partsCount) && partsCount > 0)
        {
            const int toRead = std::min(static_cast<int>(partsCount),
                                        static_cast<int>(m_emu88PartPrograms.size()));
            for(int i = 0; i < toRead; ++i)
            {
                int32_t prog = 0;
                readInt32(bytes, sizeInBytes, offset, prog);
                m_emu88PartPrograms[static_cast<size_t>(i)] = static_cast<int>(prog);
            }
            for(int i = toRead; i < static_cast<int>(partsCount); ++i)
            {
                int32_t dummy = 0;
                readInt32(bytes, sizeInBytes, offset, dummy);
            }

            int32_t savedPart = 0;
            if(readInt32(bytes, sizeInBytes, offset, savedPart) && savedPart >= 0 && savedPart < 16)
                restorePart = static_cast<int>(savedPart);
        }
        else
        {
            offset = partsStart;
        }

        // Restore the per-part variation banks (optional: absent in older sessions).
        // Read before the parts are restored, as that is what resends them to the board.
        const int banksStart = offset;
        int32_t banksMagic = 0, banksCount = 0;
        if(newType == SynthType::Emu88
           && readInt32(bytes, sizeInBytes, offset, banksMagic) && banksMagic == kEmu88BanksMagic
           && readInt32(bytes, sizeInBytes, offset, banksCount) && banksCount > 0)
        {
            const auto readBanks = [&](std::array<int, 16>& dst)
            {
                const int toRead = std::min(static_cast<int>(banksCount), static_cast<int>(dst.size()));
                for(int i = 0; i < toRead; ++i)
                {
                    int32_t v = 0;
                    readInt32(bytes, sizeInBytes, offset, v);
                    dst[static_cast<size_t>(i)] = juce::jlimit(0, 127, static_cast<int>(v));
                }
                for(int i = toRead; i < static_cast<int>(banksCount); ++i)
                {
                    int32_t dummy = 0;
                    readInt32(bytes, sizeInBytes, offset, dummy);
                }
            };
            readBanks(m_emu88PartBankMsb);
            readBanks(m_emu88PartBankLsb);
        }
        else
        {
            offset = banksStart;
        }

        // Restore the per-part rhythm assignment (optional). Also read before the parts
        // are restored: it decides whether a part's program is a kit or a tone.
        const int rhythmStart = offset;
        int32_t rhythmMagic = 0, rhythmCount = 0;
        if(newType == SynthType::Emu88
           && readInt32(bytes, sizeInBytes, offset, rhythmMagic) && rhythmMagic == kEmu88RhythmMagic
           && readInt32(bytes, sizeInBytes, offset, rhythmCount) && rhythmCount > 0)
        {
            const int toRead = std::min(static_cast<int>(rhythmCount),
                                        static_cast<int>(m_emu88PartRhythm.size()));
            for(int i = 0; i < toRead; ++i)
            {
                int32_t v = 0;
                readInt32(bytes, sizeInBytes, offset, v);
                m_emu88PartRhythm[static_cast<size_t>(i)] = juce::jlimit(0, 127, static_cast<int>(v));
            }
            for(int i = toRead; i < static_cast<int>(rhythmCount); ++i)
            {
                int32_t dummy = 0;
                readInt32(bytes, sizeInBytes, offset, dummy);
            }
        }
        else
        {
            offset = rhythmStart;
        }

        if(restorePart >= 0)
            restoreEmu88Parts(restorePart);

        // Restore the loaded MIDI song (optional).
        const int midiStart = offset;
        int32_t midiMagic = 0;
        std::string midiName;
        std::vector<uint8_t> midiData;
        if(newType == SynthType::Emu88
           && readInt32(bytes, sizeInBytes, offset, midiMagic) && midiMagic == kEmu88MidiMagic
           && readString(bytes, sizeInBytes, offset, midiName)
           && readBytes(bytes, sizeInBytes, offset, midiData)
           && !midiData.empty())
        {
            loadMidiFile(std::move(midiData), midiName);
        }
        else
        {
            offset = midiStart;
        }
    }
}
