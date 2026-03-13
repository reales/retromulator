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

#include "dx7Lib/device.h"
#include "dx7Lib/romloader.h"

#include "akaiLib/device.h"
#include "openWurliLib/device.h"

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

    synthLib::Device* SynthFactory::create(SynthType type, const std::string& romPath)
    {
        // Register the platform ROM search paths once (base folder + ROM/ subfolder)
        static const std::string s_romPath = getRomSearchPath();
        if(!s_romPath.empty())
        {
            synthLib::RomLoader::addSearchPath(s_romPath);
            synthLib::RomLoader::addSearchPath(s_romPath + "ROM/");
        }

        // If a custom romPath was provided, add it too
        if(!romPath.empty())
            synthLib::RomLoader::addSearchPath(romPath);

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
                        "Virus TI firmware not found. Place a .bin file in the search path.");

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
                // MicroQ can run without ROM (uses embedded fallback)
                return new mqLib::Device(p);
            }

            case SynthType::XT:
            {
                synthLib::DeviceCreateParams p;
                const auto rom = xt::RomLoader::findROM();
                if(!rom.isValid())
                    throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing,
                        "XT firmware not found. Place a .bin file in the search path.");

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
                p.homePath  = s_romPath.empty() ? romPath : s_romPath;
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

            default:
                fprintf(stderr, "[SynthFactory] Unknown SynthType %d\n", static_cast<int>(type));
                return nullptr;
        }
    }
}
