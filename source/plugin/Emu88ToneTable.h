#pragma once

#include <cstdint>

// The Roland GS tone tables: which variation numbers (CC0) exist for a program in a
// given tone map (CC32), and what each one is called. The board's ROM keeps the tones
// themselves, but their variation numbers are sparse and hand-assigned, so they are
// carried here rather than derived from the ROM layout. See the .cpp for provenance.
namespace retromulator::emu88ToneTable
{
    struct Entry
    {
        uint8_t  model;      // emu88Lib::DeviceModel
        uint8_t  map;        // CC32, 1-4; the board's own map has the highest number
        uint8_t  program;    // 0-127
        uint8_t  variation;  // CC0
        uint16_t name;       // index into g_names
    };

    extern const char* const g_names[];
    extern const int         g_nameCount;
    extern const Entry       g_entries[];
    extern const int         g_entryCount;
}
