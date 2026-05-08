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

bool loadBank(const std::string& filePath, SidBank& out)
{
    if (extensionEqualsCI(filePath, ".sng")) return loadSng(filePath, out);
    if (extensionEqualsCI(filePath, ".ins")) return loadIns(filePath, out);
    return false;
}

} // namespace sidLib
