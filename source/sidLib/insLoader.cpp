/**
 * GoatTracker .sng / .ins parser implementation.
 *
 * Layout reference (.sng, GTS3/4/5):
 *   [0..3]   magic "GTS3"|"GTS4"|"GTS5"
 *   [4..]    songname[32], authorname[32], copyrightname[32]
 *   then     u8 numsongs
 *   then     for each song, for each of 3 channels:
 *              u8 length, then (length+1) bytes of orderlist
 *   then     u8 numinstr
 *   then     numinstr × 25-byte instrument record:
 *              ad,sr,wptr,pptr,fptr,sptr,vibdelay,gatetimer,firstwave (9 bytes)
 *              name[16]
 *   then     for each of 4 tables: u8 len, len bytes ltable, len bytes rtable
 *   then     u8 numpatterns
 *   then     for each pattern: u8 rows, rows*4 bytes pattern data
 *
 * Layout reference (.ins, GTI3/4/5):
 *   [0..3]   magic "GTI3"|"GTI4"|"GTI5"
 *   [4..12]  ad,sr,wptr,pptr,fptr,sptr,vibdelay,gatetimer,firstwave
 *   [13..28] name[16]
 *   [29..]   for each of 4 tables: u8 len, len bytes ltable, len bytes rtable
 *
 * The .ins format references tables by absolute index from the source song;
 * we rebase to start at 1 (each .ins becomes a self-contained bank with one
 * instrument and just-enough table bytes).
 */
#include "insLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace sidLib
{

// ── Tiny binary cursor ───────────────────────────────────────────────────
namespace
{
class Cursor
{
public:
    explicit Cursor(const std::vector<uint8_t>& data) : m_data(data) {}

    bool eof(size_t need = 1) const { return m_pos + need > m_data.size(); }
    size_t pos() const { return m_pos; }
    void skip(size_t n) { m_pos = std::min(m_pos + n, m_data.size()); }

    uint8_t u8()
    {
        if (eof()) return 0;
        return m_data[m_pos++];
    }

    bool readBytes(uint8_t* dst, size_t n)
    {
        if (eof(n)) return false;
        std::memcpy(dst, m_data.data() + m_pos, n);
        m_pos += n;
        return true;
    }

    bool match(const char (&magic)[5])
    {
        if (eof(4)) return false;
        return std::memcmp(m_data.data() + m_pos, magic, 4) == 0;
    }

private:
    const std::vector<uint8_t>& m_data;
    size_t                      m_pos = 0;
};

bool readWholeFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    out.resize(sz);
    if (sz == 0) return true;
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
    return f.good() || f.eof();
}

std::string trimName(const uint8_t* raw, size_t len)
{
    // Names are null-padded, may contain trailing spaces. Trim both.
    size_t end = 0;
    for (size_t i = 0; i < len; ++i)
    {
        if (raw[i] == 0) break;
        end = i + 1;
    }
    while (end > 0 && (raw[end - 1] == ' ' || raw[end - 1] == 0))
        --end;
    return std::string(reinterpret_cast<const char*>(raw), end);
}

std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    return name;
}

bool extensionEqualsCI(const std::string& path, const char* ext)
{
    const size_t n = std::strlen(ext);
    if (path.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
    {
        const char a = static_cast<char>(std::tolower(
            static_cast<unsigned char>(path[path.size() - n + i])));
        const char b = static_cast<char>(std::tolower(
            static_cast<unsigned char>(ext[i])));
        if (a != b) return false;
    }
    return true;
}
} // anon

// ── .sng loader ───────────────────────────────────────────────────────────
bool loadSng(const std::string& filePath, SidBank& out)
{
    std::vector<uint8_t> data;
    if (!readWholeFile(filePath, data)) return false;
    if (data.size() < 4 + 32 * 3 + 1) return false;

    Cursor c(data);
    char magic[4]; c.readBytes(reinterpret_cast<uint8_t*>(magic), 4);
    const bool gts3 = std::memcmp(magic, "GTS3", 4) == 0;
    const bool gts4 = std::memcmp(magic, "GTS4", 4) == 0;
    const bool gts5 = std::memcmp(magic, "GTS5", 4) == 0;
    if (!gts3 && !gts4 && !gts5) return false;

    // Skip 3 × 32-byte info strings
    c.skip(32 * 3);
    if (c.eof()) return false;

    // Skip songorder lists: numsongs × MAX_CHN(3) × (1 + length+1) bytes
    const uint8_t numSongs = c.u8();
    for (int s = 0; s < numSongs; ++s)
    {
        for (int ch = 0; ch < 3; ++ch)
        {
            if (c.eof()) return false;
            const uint8_t len = c.u8();
            c.skip(static_cast<size_t>(len) + 1);
        }
    }
    if (c.eof()) return false;

    // Instrument table
    const uint8_t numInstr = c.u8();
    out.instruments.clear();
    out.instruments.resize(static_cast<size_t>(numInstr) + 1); // index 0 = silent

    for (int i = 1; i <= numInstr; ++i)
    {
        if (c.eof(9 + kMaxInstrNameLen)) return false;
        SidInstrument& ins = out.instruments[i];
        ins.ad        = c.u8();
        ins.sr        = c.u8();
        ins.wptr      = c.u8();
        ins.pptr      = c.u8();
        ins.fptr      = c.u8();
        ins.sptr      = c.u8();
        ins.vibdelay  = c.u8();
        ins.gatetimer = c.u8();
        ins.firstwave = c.u8();
        uint8_t nameRaw[kMaxInstrNameLen];
        c.readBytes(nameRaw, kMaxInstrNameLen);
        ins.name = trimName(nameRaw, kMaxInstrNameLen);
        if (ins.name.empty()) ins.name = "Instrument " + std::to_string(i);
    }

    // 4 macro tables
    for (int t = 0; t < kNumTables; ++t)
    {
        if (c.eof()) return false;
        const uint8_t len = c.u8();
        out.ltable[t].assign(len, 0);
        out.rtable[t].assign(len, 0);
        if (len)
        {
            if (!c.readBytes(out.ltable[t].data(), len)) return false;
            if (!c.readBytes(out.rtable[t].data(), len)) return false;
        }
    }

    // We deliberately ignore patterns — they're song data, not patches.

    out.name = baseName(filePath);
    return true;
}

// ── .ins loader ───────────────────────────────────────────────────────────
bool loadIns(const std::string& filePath, SidBank& out)
{
    std::vector<uint8_t> data;
    if (!readWholeFile(filePath, data)) return false;
    if (data.size() < 4 + 9 + kMaxInstrNameLen) return false;

    Cursor c(data);
    char magic[4]; c.readBytes(reinterpret_cast<uint8_t*>(magic), 4);
    const bool gti3 = std::memcmp(magic, "GTI3", 4) == 0;
    const bool gti4 = std::memcmp(magic, "GTI4", 4) == 0;
    const bool gti5 = std::memcmp(magic, "GTI5", 4) == 0;
    if (!gti3 && !gti4 && !gti5) return false;

    out.instruments.clear();
    out.instruments.resize(2); // index 0 = silent, 1 = the instrument

    SidInstrument& ins = out.instruments[1];
    ins.ad        = c.u8();
    ins.sr        = c.u8();

    // The .ins stores the original song's table indices (optr[]). We'll
    // remap these to start-at-1 in our local tables after rebasing jumps.
    uint8_t optr[kNumTables];
    optr[kWTBL] = c.u8();
    optr[kPTBL] = c.u8();
    optr[kFTBL] = c.u8();
    optr[kSTBL] = c.u8();
    ins.vibdelay  = c.u8();
    ins.gatetimer = c.u8();
    ins.firstwave = c.u8();
    uint8_t nameRaw[kMaxInstrNameLen];
    c.readBytes(nameRaw, kMaxInstrNameLen);
    ins.name = trimName(nameRaw, kMaxInstrNameLen);
    if (ins.name.empty()) ins.name = baseName(filePath);

    uint8_t newPtr[kNumTables] = { 0, 0, 0, 0 };

    for (int t = 0; t < kNumTables; ++t)
    {
        if (c.eof()) return false;
        const uint8_t len = c.u8();
        out.ltable[t].assign(len, 0);
        out.rtable[t].assign(len, 0);
        if (len)
        {
            if (!c.readBytes(out.ltable[t].data(), len)) return false;
            if (!c.readBytes(out.rtable[t].data(), len)) return false;

            // Rebase 0xff jump targets relative to original song's optr.
            // Per ginstr.c: rtable = rtable - optr[c] + start + 1
            // Here `start` = 0 (we begin each table fresh), so rtable += 1 - optr[c].
            // This is skipped for STBL (speed table has no jump markers).
            if (t != kSTBL)
            {
                for (uint8_t d = 0; d < len; ++d)
                {
                    if (out.ltable[t][d] == 0xff && out.rtable[t][d] != 0)
                    {
                        const int v = static_cast<int>(out.rtable[t][d])
                                      - static_cast<int>(optr[t]) + 1;
                        out.rtable[t][d] = static_cast<uint8_t>(std::clamp(v, 0, 255));
                    }
                }
            }
            newPtr[t] = 1; // tables in a .ins always start at index 1
        }
    }

    ins.wptr = newPtr[kWTBL];
    ins.pptr = newPtr[kPTBL];
    ins.fptr = newPtr[kFTBL];
    ins.sptr = newPtr[kSTBL];

    out.name = baseName(filePath);
    return true;
}

// ── .sid (PSID/RSID) loader ──────────────────────────────────────────────
//
// Reverse-engineers a GoatTracker-packed PSID back into the instrument bank.
// Algorithm follows 2bt/sid2sng (https://github.com/2bt/sid2sng):
//   1. Anchor on freqtblhi (12-byte signature unique to GT's player)
//   2. Read songtbl: numSongs × 3 channel pointers (lo bytes then hi bytes)
//   3. Walk song orderlists to find pattern count; walk patterns to find the
//      highest instrument index and highest macro-table indices referenced
//   4. Skip pattern-pointer table; instruments follow as column-major arrays
//      (mt_insad[N], mt_inssr[N], mt_inswaveptr[N], optionally ptable, ftable,
//      vibparam+vibdelay, gatetimer+firstwave)
//   5. Four macro tables follow, sized by the highest indices found in step 3
//      plus any further indices referenced from earlier table entries
//
// Per-export feature flags (nopulse, nofilter, noinstrvib, fixedparams,
// nowavedelay) aren't stored in the PSID, so we auto-detect by trying flag
// combinations until parsing succeeds end-to-end.
namespace
{

// 6510 opcodes / GT's packed-pattern format
constexpr uint8_t kFX        = 0x40;
constexpr uint8_t kFXOnly    = 0x50;
constexpr uint8_t kFirstNote = 0x60;
constexpr uint8_t kRest      = 0xbd;
constexpr uint8_t kKeyOn     = 0xbf;
constexpr uint8_t kRepeat    = 0xd0;
constexpr uint8_t kLoopSong  = 0xff;
constexpr uint8_t kEndPatt   = 0x00;

constexpr uint8_t kCmdPortaUp     = 1;
constexpr uint8_t kCmdVibrato     = 4;
constexpr uint8_t kCmdSetTempo    = 15;
constexpr uint8_t kCmdSetWavePtr  = 8;
constexpr uint8_t kCmdSetFiltPtr  = 10;

constexpr uint8_t kWaveCmd = 0xf0;

// GT's stock freqtblhi signature (12 bytes from mid-table — distinctive
// because nearly the only spot with this exact ascending hi-byte pattern).
const uint8_t kFreqHi[12] = {
    0x08,0x09,0x09,0x0a,0x0a,0x0b,0x0c,0x0d,0x0d,0x0e,0x0f,0x10
};
// Continuation of freqtblhi past the 12-byte anchor.
const uint8_t kFreqHiTail[] = {
    0x11,0x12,0x13,0x14,0x15,0x17,0x18,0x1a,0x1b,0x1d,0x1f,0x20,
    0x22,0x24,0x27,0x29,0x2b,0x2e,0x31,0x34,0x37,0x3a,0x3e,0x41,
    0x45,0x49,0x4e,0x52,0x57,0x5c,0x62,0x68,0x6e,0x75,0x7c,0x83,
    0x8b,0x93,0x9c,0xa5,0xaf,0xb9,0xc4,0xd0,0xdd,0xea,0xf8,0xff
};

struct SidStream
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos  = 0;
    bool   bad  = false;

    uint8_t peek()
    {
        if (pos >= size) { bad = true; return 0; }
        return data[pos];
    }
    uint8_t read()
    {
        if (pos >= size) { bad = true; return 0; }
        return data[pos++];
    }
};

struct SidFlags
{
    bool nopulse     = false;
    bool nofilter    = false;
    bool noinstrvib  = false;
    bool fixedparams = false;
    bool nowavedelay = false;
};

bool tryLoadSidWith(const std::vector<uint8_t>& fileData, SidFlags flags, SidBank& out)
{
    if (fileData.size() < 0x7c) return false;
    if (std::memcmp(fileData.data(), "PSID", 4) != 0
        && std::memcmp(fileData.data(), "RSID", 4) != 0)
        return false;

    const uint16_t dataOffset = (uint16_t(fileData[6]) << 8) | fileData[7];
    uint16_t       loadAddr   = (uint16_t(fileData[8]) << 8) | fileData[9];
    if (dataOffset >= fileData.size()) return false;
    if (loadAddr == 0)
    {
        if (dataOffset + 2 > fileData.size()) return false;
        loadAddr = uint16_t(fileData[dataOffset])
                 | (uint16_t(fileData[dataOffset + 1]) << 8);
    }
    const uint16_t songCount = (uint16_t(fileData[0x0e]) << 8) | fileData[0x0f];
    if (songCount == 0 || songCount > 32) return false;

    // sid2sng uses the entire file as `data` and translates pointers via
    // `addr_offset = headerDataOffset - loadAddr + 2`. We mirror that.
    SidStream s;
    s.data = fileData.data();
    s.size = fileData.size();
    const int addrOffset = int(dataOffset) - int(loadAddr) + 2;

    // ── 1. Anchor on freqtblhi ───────────────────────────────────────────
    size_t hiAt = 0;
    bool   foundHi = false;
    for (size_t i = 0; i + sizeof(kFreqHi) <= s.size; ++i)
    {
        if (std::memcmp(s.data + i, kFreqHi, sizeof(kFreqHi)) == 0)
        { hiAt = i; foundHi = true; break; }
    }
    if (!foundHi) return false;
    // Skip past the matched tail
    size_t tailMatch = 0;
    while (tailMatch < sizeof(kFreqHiTail)
           && hiAt + sizeof(kFreqHi) + tailMatch < s.size
           && s.data[hiAt + sizeof(kFreqHi) + tailMatch] == kFreqHiTail[tailMatch])
        ++tailMatch;
    s.pos = hiAt + sizeof(kFreqHi) + tailMatch;

    // ── 2. Song-pointer table ─────────────────────────────────────────────
    std::vector<int> songOrderPos(songCount);
    for (int& addr : songOrderPos) { addr = s.read(); s.read(); s.read(); }
    for (int& addr : songOrderPos)
    {
        addr |= s.read() << 8;
        addr += addrOffset;
        s.read(); s.read();
    }
    if (s.bad) return false;
    const int pattTablePos = (int)s.pos;

    // ── 3. Walk songs to discover patterns referenced ────────────────────
    int pattCount = 0;
    for (int songIdx = 0; songIdx < songCount; ++songIdx)
    {
        if (songOrderPos[songIdx] < 0 || (size_t)songOrderPos[songIdx] >= s.size)
            return false;
        s.pos = (size_t)songOrderPos[songIdx];
        for (int ch = 0; ch < 3; ++ch)
        {
            for (;;)
            {
                const uint8_t x = s.read();
                if (s.bad) return false;
                if (x == kLoopSong) break;
                if (x < kRepeat)
                    pattCount = std::max(pattCount, (int)x + 1);
                // REPEAT/TRANSDOWN/TRANSUP carry no pattern info we need
            }
            s.read();  // pattern-end restart byte
            if (s.bad) return false;
        }
    }
    if (pattCount == 0) return false;

    // ── 4. Walk patterns (right after song orderlists) to find max instr &
    //       max table refs from FX commands.  Pattern data starts where we
    //       just stopped reading.
    int instrCount = 0;
    int maxTable[kNumTables] = { 0, 0, 0, 0 };
    for (int pat = 0; pat < pattCount; ++pat)
    {
        if (s.bad) return false;
        int instr = 0;
        int cmd   = 0;
        int arg   = 0;
        for (;;)
        {
            if (s.peek() < kFX)
            {
                instr = s.read();
                instrCount = std::max(instrCount, instr);
            }

            int repeat = 1;
            uint8_t x = s.read();
            if (s.bad) return false;
            if (x > kKeyOn) { repeat = 256 - x; /* note=REST */ }
            else if (x >= kRest) { /* note */ }
            else
            {
                if (x >= kFirstNote) { /* plain note */ }
                else
                {
                    cmd = x & 0x0f;
                    arg = cmd ? s.read() : 0;
                    if (x < kFXOnly) s.read();  // skip note byte
                    if (s.bad) return false;

                    if (cmd == kCmdSetTempo && (arg & 0x7f) >= 2) ++arg;
                    if ((cmd >= kCmdPortaUp && cmd <= kCmdVibrato) || cmd == 0xe)
                        maxTable[kSTBL] = std::max(maxTable[kSTBL], arg);
                    if (cmd >= kCmdSetWavePtr && cmd <= kCmdSetFiltPtr)
                    {
                        const int t = cmd - kCmdSetWavePtr;
                        if (t >= 0 && t < kNumTables)
                            maxTable[t] = std::max(maxTable[t], arg);
                    }
                }
            }
            (void)repeat;

            if (s.peek() == kEndPatt) break;
        }
        s.read();  // consume the 0x00 end-of-pattern
    }

    // ── 5. Skip pattern-pointer table; instruments follow ────────────────
    s.pos = (size_t)pattTablePos + (size_t)pattCount * 2;

    if (instrCount <= 0 || instrCount > 63) return false;

    out.instruments.clear();
    out.instruments.resize(instrCount + 1);

    auto readArr = [&](auto setter) {
        for (int i = 1; i <= instrCount; ++i)
        {
            uint8_t v = s.read();
            if (s.bad) return false;
            setter(out.instruments[i], v);
        }
        return true;
    };

    if (!readArr([](SidInstrument& ins, uint8_t v){ ins.ad = v; })) return false;
    if (!readArr([](SidInstrument& ins, uint8_t v){ ins.sr = v; })) return false;
    if (!readArr([&](SidInstrument& ins, uint8_t v) {
        ins.wptr = v;
        maxTable[kWTBL] = std::max(maxTable[kWTBL], (int)v);
    })) return false;
    if (!flags.nopulse && !readArr([&](SidInstrument& ins, uint8_t v) {
        ins.pptr = v;
        maxTable[kPTBL] = std::max(maxTable[kPTBL], (int)v);
    })) return false;
    if (!flags.nofilter && !readArr([&](SidInstrument& ins, uint8_t v) {
        ins.fptr = v;
        maxTable[kFTBL] = std::max(maxTable[kFTBL], (int)v);
    })) return false;
    if (!flags.noinstrvib)
    {
        if (!readArr([&](SidInstrument& ins, uint8_t v) {
            ins.sptr = v;
            maxTable[kSTBL] = std::max(maxTable[kSTBL], (int)v);
        })) return false;
        if (!readArr([](SidInstrument& ins, uint8_t v){ ins.vibdelay = v; })) return false;
    }
    if (!flags.fixedparams)
    {
        if (!readArr([](SidInstrument& ins, uint8_t v){ ins.gatetimer = v; })) return false;
        if (!readArr([](SidInstrument& ins, uint8_t v){ ins.firstwave = v; })) return false;
    }
    for (int i = 1; i <= instrCount; ++i)
        out.instruments[i].name = "Instrument " + std::to_string(i);

    // ── 6. Macro tables ──────────────────────────────────────────────────
    for (int t = 0; t < kNumTables; ++t)
    {
        if (t == kPTBL && flags.nopulse)  continue;
        if (t == kFTBL && flags.nofilter) continue;

        if (t == kSTBL)
        {
            const uint8_t leadIn = s.read();
            if (s.bad || leadIn != 0) return false;
        }

        out.ltable[t].clear();
        out.rtable[t].clear();
        int lastX = 0;
        for (int i = 0; i < maxTable[t]; ++i)
        {
            const uint8_t v = s.read();
            if (s.bad) return false;
            out.ltable[t].push_back(v);
            lastX = v;
        }
        if (t < kSTBL)
        {
            // Trailing entries until we hit a 0xff terminator
            while (lastX != 0xff)
            {
                const uint8_t v = s.read();
                if (s.bad) return false;
                out.ltable[t].push_back(v);
                lastX = v;
            }
        }
        else
        {
            // Speedtable: read until we find a zero (the post-table sentinel)
            while (s.pos < s.size && s.peek() != 0)
                out.ltable[t].push_back(s.read());
            const uint8_t trailing = s.read();
            if (s.bad || trailing != 0) return false;
        }

        // Right-side bytes
        const size_t lcount = out.ltable[t].size();
        out.rtable[t].resize(lcount);
        for (size_t i = 0; i < lcount; ++i)
        {
            out.rtable[t][i] = s.read();
            if (s.bad) return false;

            // Per-table post-processing (mirrors sid2sng)
            if (t == kWTBL)
            {
                if (!flags.nowavedelay)
                {
                    int v = out.ltable[t][i];
                    if (v > 0x1f && v < 0xf0)      v -= 0x10;
                    else if (v > 0x0f && v < 0x20) v += 0xd0;
                    out.ltable[t][i] = (uint8_t)v;
                }
                if (out.ltable[t][i] < kWaveCmd)
                    out.rtable[t][i] ^= 0x80;
            }
            else if (t == kFTBL)
            {
                int v = out.ltable[t][i];
                if (v > 0x80 && v < 0xff)
                    out.ltable[t][i] = (uint8_t)((v << 1) | 0x80);
            }
            else if (t == kSTBL)
            {
                const uint8_t v = out.ltable[t][i];
                if ((v >= 0xf1 && v <= 0xf4) || v == 0xfe)
                    maxTable[kSTBL] = std::max(maxTable[kSTBL], (int)out.rtable[t][i]);
            }
        }
    }

    // Sanity: we should have stopped at or before the first song-orderlist
    if (!songOrderPos.empty() && (int)s.pos > songOrderPos[0]) return false;

    out.name = baseName({}); // filled by caller
    return true;
}

} // anon

bool loadSid(const std::string& filePath, SidBank& out)
{
    std::vector<uint8_t> data;
    if (!readWholeFile(filePath, data)) return false;

    // Auto-detect feature flags by trying every combination of the four
    // per-export switches that change the binary layout. nowavedelay only
    // affects post-processing of wavetable values; we leave it on (sid2sng's
    // default for "modern" GT exports). If wave data ends up garbled the user
    // can manually massage, but auto-detection of this would require running
    // the .sid in an emulator.
    const SidFlags variants[] = {
        {false, false, false, false, false},  // all features
        {false, false, false, true,  false},  // fixedparams
        {true,  false, false, false, false},  // nopulse
        {false, true,  false, false, false},  // nofilter
        {false, false, true,  false, false},  // noinstrvib
        {true,  false, false, true,  false},
        {false, true,  false, true,  false},
        {false, false, true,  true,  false},
        {true,  true,  false, false, false},
        {true,  true,  false, true,  false},
        {true,  true,  true,  true,  false},
    };
    for (const auto& f : variants)
    {
        SidBank attempt;
        if (tryLoadSidWith(data, f, attempt))
        {
            out = std::move(attempt);
            out.name = baseName(filePath);
            return true;
        }
    }
    return false;
}

bool loadBank(const std::string& filePath, SidBank& out)
{
    if (extensionEqualsCI(filePath, ".sng")) return loadSng(filePath, out);
    if (extensionEqualsCI(filePath, ".ins")) return loadIns(filePath, out);
    if (extensionEqualsCI(filePath, ".sid")) return loadSid(filePath, out);
    return false;
}

} // namespace sidLib
