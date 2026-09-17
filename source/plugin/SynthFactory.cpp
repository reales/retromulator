#include "SynthFactory.h"

#include "virusLib/device.h"
#include "virusLib/romloader.h"
#include "virusLib/deviceModel.h"

#include "mqLib/device.h"
#include "mqLib/romloader.h"

#include "xtLib/xtDevice.h"
#include "xtLib/xtRomLoader.h"

#include "nord/n2x/n2xLib/n2xdevice.h"
#include "nord/n2x/n2xLib/n2xromloader.h"

#include "ronaldo/je8086/jeLib/device.h"
#include "ronaldo/je8086/jeLib/romloader.h"

#include "ronaldo/88emu/88lib/hardwareDevice.h"
#include "ronaldo/88emu/88lib/romloader.h"

#include "dx7Lib/device.h"
#include "dx7Lib/romloader.h"

#include "akaiLib/device.h"
#include "openWurliLib/device.h"
#include "opl3Lib/device.h"
#include "sidLib/device.h"
#include "ayumiLib/device.h"

#include "synthLib/romLoader.h"
#include "synthLib/deviceException.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace retromulator
{
    static std::string getRomSearchPath()
    {
#if defined(_WIN32)
        // Windows: Documents/discoDSP/Retromulator/
        const char* docs = std::getenv("USERPROFILE");
        if(!docs) docs = std::getenv("HOMEDRIVE");
        if(docs)
            return std::string(docs) + "\\Documents\\discoDSP\\Retromulator\\";
#elif defined(__APPLE__)
        // macOS: ~/Library/Application Support/discoDSP/Retromulator/
        const char* home = std::getenv("HOME");
        if(home)
            return std::string(home) + "/Library/Application Support/discoDSP/Retromulator/";
#else
        // Linux: ~/Documents/discoDSP/Retromulator/
        const char* home = std::getenv("HOME");
        if(home)
            return std::string(home) + "/Documents/discoDSP/Retromulator/";
#endif
        return {};
    }

    namespace
    {
        // -1 = auto: boot the first board whose ROM set is complete.
        int s_emu88Model = -1;

        const std::string& romSearchPath()
        {
            static const std::string s_path = getRomSearchPath();
            return s_path;
        }
    }

    void SynthFactory::setEmu88Model(const int model) { s_emu88Model = model; }
    int  SynthFactory::getEmu88Model()                { return s_emu88Model; }

    void SynthFactory::addDefaultRomSearchPaths(const std::string& extraPath)
    {
        // Register the platform ROM search paths once (base folder + ROM/ subfolder)
        const auto& s_romPath = romSearchPath();
        if(!s_romPath.empty())
        {
            synthLib::RomLoader::addSearchPath(s_romPath);
            synthLib::RomLoader::addSearchPath(s_romPath + "ROM/");
            // The SC-88 family needs a whole set of images per board, so they live together
            // rather than loose in ROM/. Recursive because the set is identified by content:
            // a user can drop a collection in unsorted and every recognized dump still lands.
            synthLib::RomLoader::addSearchPath(s_romPath + "88emu/", true);
        }

        if(!extraPath.empty())
            synthLib::RomLoader::addSearchPath(extraPath);
    }

    synthLib::Device* SynthFactory::create(SynthType type, const std::string& romPath,
                                           const int emu88Model)
    {
        addDefaultRomSearchPaths(romPath);

        switch(type)
        {
            case SynthType::VirusABC:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = virusLib::ROMLoader::findROM(virusLib::DeviceModel::ABC);

                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "Virus ABC firmware not found. Place a .bin or .mid file in the search path.");

                p.romData    = rom.getRomFileData();
                p.romName    = rom.getFilename();
                p.customData = static_cast<uint32_t>(rom.getModel());
                return new virusLib::Device(p);
            }

            case SynthType::VirusTI:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = virusLib::ROMLoader::findROM(virusLib::DeviceModel::TI);

                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "Virus TI firmware not found. Place firmware.bin in the search path.");

                p.romData    = rom.getRomFileData();
                p.romName    = rom.getFilename();
                p.customData = static_cast<uint32_t>(rom.getModel());
                return new virusLib::Device(p);
            }

            case SynthType::MicroQ:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = mqLib::RomLoader::findROM();
                if(rom.isValid())
                {
                    p.romData = rom.getData();
                    p.romName = rom.getFilename();
                }
                return new mqLib::Device(p);
            }

            case SynthType::XT:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = xt::RomLoader::findROM();
                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "XT firmware not found. Place the two 128 KB IC dump .bin files in the search path.");

                p.romData = rom.getData();
                p.romName = rom.getFilename();
                return new xt::Device(p);
            }

            case SynthType::NordN2X:
            {
                const auto rom = n2x::RomLoader::findROM();
                synthLib::DeviceCreateParams p;
                if(rom.isValid())
                {
                    p.romData = rom.data();
                    p.romName = rom.getFilename();
                }
                auto* d = new n2x::Device(p);
                if(!d->isValid())
                {
                    delete d;
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "Nord N2X firmware not found. Place a 512KB .bin file in the search path.");
                }
                return d;
            }

            case SynthType::JE8086:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = jeLib::RomLoader::findROM();
                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "JE-8086 firmware not found. Place the required .bin or .mid files in the search path.");

                p.romData   = rom.getData();
                p.romName   = rom.getName();
                p.homePath  = romSearchPath().empty() ? romPath : romSearchPath();
                return new jeLib::Device(p);
            }

            case SynthType::DX7:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = dx7Emu::RomLoader::findROM();
                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "DX7 firmware not found. Place a 16KB .bin firmware file in the search path.");

                p.romData  = rom.getData();
                p.romName  = rom.getFilename();
                return new dx7Emu::Device(p);
            }

            case SynthType::AkaiS1000:
            {
                synthLib::DeviceCreateParams p;
                return new akaiLib::Device(p);
            }

            case SynthType::OpenWurli:
            {
                synthLib::DeviceCreateParams p;
                return new openWurliLib::Device(p);
            }

            case SynthType::OPL3:
            {
                synthLib::DeviceCreateParams p;
                return new opl3Lib::Device(p);
            }

            case SynthType::SID:
            {
                synthLib::DeviceCreateParams p;
                return new sidLib::Device(p);
            }

            case SynthType::Ayumi:
            {
                synthLib::DeviceCreateParams p;
                return new ayumiLib::Device(p);
            }

            case SynthType::Emu88:
            {
                // ROMs are identified by content, so the loader finds its own sets - there is no
                // romData to hand over. Rescan first: the user may just have imported a dump.
                emu88Lib::RomLoader::rescan();

                // The caller's board wins where it gave one, so a restore does not depend
                // on a global another instance may be writing at the same moment.
                const int wanted = emu88Model >= 0 ? emu88Model : s_emu88Model;
                auto model = static_cast<emu88Lib::DeviceModel>(wanted);
                if(wanted < 0 || !emu88Lib::RomLoader::isDeviceAvailable(model))
                {
                    // Fall back to the first board whose set is complete rather than booting
                    // one into silence.
                    bool found = false;
                    for(uint32_t i = 0; i < emu88Lib::deviceModelCount(); ++i)
                    {
                        const auto candidate = static_cast<emu88Lib::DeviceModel>(i);
                        if(emu88Lib::RomLoader::isDeviceAvailable(candidate))
                        {
                            model = candidate;
                            found = true;
                            break;
                        }
                    }

                    if(!found)
                        throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                            "No complete SC-88 family ROM set found. Place the ROMs in the 88emu folder.");
                }

                synthLib::DeviceCreateParams p;
                p.customData = static_cast<uint32_t>(model);
                p.romName    = emu88Lib::getDeviceProfile(model).displayName;
                p.homePath   = romSearchPath().empty() ? romPath : romSearchPath();
                return new emu88Lib::HardwareDevice(p);
            }

            default:
                fprintf(stderr, "[SynthFactory] Unknown SynthType %d\n", static_cast<int>(type));
                return nullptr;
        }
    }
}
