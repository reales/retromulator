/**
 * GoatTracker .sng / .ins parser for Retromulator SID core.
 *
 * Supports v3/v4/v5 of both formats (GTS3/4/5 song, GTI3/4/5 instrument).
 * Older v1/v2 formats are ignored — they predate the speedtable and require
 * non-trivial conversion logic (the GoatTracker source does it but it's >200
 * lines and the modern format is what every recent .sng uses).
 *
 * The four tables (wavetable / pulsetable / filtertable / speedtable) are
 * stored shared at the bank level — each SidInstrument's wptr/pptr/fptr/sptr
 * is a 1-based index into the corresponding table (0 means "no table").
 *
 * Reference: GoatTracker 2 source (Lasse Öörni, GPL v2),
 *   gsong.c::loadsong() and ginstr.c::loadinstrument()
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sidLib
{

constexpr int kMaxTableLen     = 255;
constexpr int kMaxInstrNameLen = 16;
constexpr int kNumTables       = 4;
constexpr int kWTBL = 0;
constexpr int kPTBL = 1;
constexpr int kFTBL = 2;
constexpr int kSTBL = 3;

struct SidInstrument
{
    uint8_t  ad         = 0;   // Attack/Decay (SID $05+ch*7)
    uint8_t  sr         = 0;   // Sustain/Release (SID $06+ch*7)
    uint8_t  wptr       = 0;   // 1-based wavetable start (0 = none)
    uint8_t  pptr       = 0;   // 1-based pulsetable start
    uint8_t  fptr       = 0;   // 1-based filtertable start
    uint8_t  sptr       = 0;   // 1-based speedtable start
    uint8_t  vibdelay   = 0;   // vibrato delay frames
    uint8_t  gatetimer  = 0;   // bits 0-5: gate frame count, 0x40: no keyoff, 0x80: no hard restart
    uint8_t  firstwave  = 0;   // 0xfe=keyoff,0xff=keyon, else initial waveform bits
    std::string name;          // up to 16 chars, null-trimmed
};

struct SidBank
{
    std::string name;          // file basename, used as bank label
    std::vector<SidInstrument> instruments;       // index 0 = "no instrument", first real one at 1
    std::array<std::vector<uint8_t>, kNumTables> ltable{}; // lo bytes (entry layout per table — see device.cpp)
    std::array<std::vector<uint8_t>, kNumTables> rtable{}; // hi bytes

    bool isValid() const { return !instruments.empty(); }
};

// Parse a GoatTracker .sng (GTS3/4/5) file. Returns a bank with all instruments
// and tables. Songs/patterns are skipped — we only need the patches.
bool loadSng(const std::string& filePath, SidBank& out);

// Parse a GoatTracker .ins (GTI3/4/5) file. Yields a bank containing exactly
// one instrument plus its referenced tables (rebased to start at offset 1).
bool loadIns(const std::string& filePath, SidBank& out);

// Auto-dispatch based on file extension (.sng or .ins). Case-insensitive.
bool loadBank(const std::string& filePath, SidBank& out);

} // namespace sidLib
