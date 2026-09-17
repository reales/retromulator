#pragma once

#include <string>
#include <vector>

#include "ronaldo/88emu/88lib/deviceModel.h"

namespace retromulator
{
    // The 128 capital tone names (bank 0, program 0..127) of a board, read from the
    // user's own ROM image: every SC firmware keeps a 128-entry program directory
    // pointing at tone records whose first 12 bytes are the name.
    // Empty if the ROM set is missing or the directory was not recognized.
    std::vector<std::string> emu88LoadCapitalTones(emu88Lib::DeviceModel model);

    // The drum kit for each of the 128 programs of the drum part, read the same way:
    // the firmware keeps a 128-byte table of kit indices, 0xFF where a program has no
    // kit, into a block of fixed-size drum map records headed by the kit name.
    // Programs without a kit are empty strings. Empty if not recognized.
    std::vector<std::string> emu88LoadDrumKits(emu88Lib::DeviceModel model);

    // One selectable variation of a program: its GS variation number and its name.
    struct Emu88Variation
    {
        int         cc0;
        std::string name;
    };

    // Every variation a program has in one tone map, in ascending CC0 order, the capital
    // tone (CC0 0) first. The numbers are sparse: Roland assigns sub-variations at +1, +2
    // inside each group of eight and leaves gaps, so they cannot be counted off.
    //
    // map is the CC32 value, 1 being the SC-55 map and the board's own map the highest it
    // supports. Empty if the board or map is unknown.
    std::vector<Emu88Variation> emu88Variations(emu88Lib::DeviceModel model, int map, int program);

    // The CC32 map values a board supports, lowest (SC-55) to its own, e.g. {1,2,3} for
    // the SC-88Pro. Empty on the SC-55mkII, which has one map and does not read CC32.
    std::vector<int> emu88Maps(emu88Lib::DeviceModel model);

    // Name of the map a CC32 value selects ("SC-55", "SC-88", ...), or empty.
    std::string emu88MapName(int map);
}
