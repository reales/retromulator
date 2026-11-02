#pragma once

#include <string>
#include "SynthType.h"
#include "synthLib/device.h"

namespace retromulator
{
    class SynthFactory
    {
    public:
        // Create a device for the given synth type.
        // romPath may be a file path or directory; each synth's RomLoader
        // will search the path if needed.
        // emu88Model overrides the process-wide board for this call only: a restoring
        // instance passes the board its own session saved, so two instances booting at
        // once (an AU host restores them on separate threads) cannot read each other's.
        // -1 keeps the shared value the editor sets.
        static synthLib::Device* create(SynthType type, const std::string& romPath = {},
                                        int emu88Model = -1);

        // Which board the 88emu core boots. Switching it means recreating the device:
        // the models differ in CPU, chipset and ROM set, so it is not a live parameter.
        // Values are emu88Lib::DeviceModel; -1 picks the first board whose ROMs are complete.
        static void setEmu88Model(int model);
        static int  getEmu88Model();

        // Register the platform ROM folders with the loaders. create() does this on its
        // own; call it directly to inspect what is installed without booting a device.
        static void addDefaultRomSearchPaths(const std::string& extraPath = {});
    };
}
