#include "Emu88Tones.h"

#include <algorithm>
#include <cstring>

#include "Emu88ToneTable.h"
#include "ronaldo/88emu/88lib/romloader.h"

namespace retromulator
{
    namespace
    {
        constexpr size_t kNameLen = 12;
        constexpr size_t kNone    = static_cast<size_t>(-1);

        struct BoardRom
        {
            std::vector<uint8_t> data;
            bool   pointers   = true;   // 24-bit absolute pointers (H8 boards) vs 16-bit record index
            size_t toneStride = 0;      // tone record size for index-based boards
            size_t kitStride  = 0x50C;  // drum map record size
        };

        bool printableName(const uint8_t* p)
        {
            bool any = false;
            for(size_t i = 0; i < kNameLen; ++i)
            {
                if(p[i] < 0x20 || p[i] > 0x7e)
                    return false;
                any |= p[i] != ' ';
            }
            return any;
        }

        std::string trimmedName(const uint8_t* p)
        {
            std::string s(reinterpret_cast<const char*>(p), kNameLen);
            while(!s.empty() && s.back() == ' ')
                s.pop_back();
            return s;
        }

        bool startsWith(const uint8_t* p, const char* prefix)
        {
            return std::memcmp(p, prefix, std::strlen(prefix)) == 0;
        }

        bool loadBoardRom(const emu88Lib::DeviceModel model, BoardRom& rom)
        {
            using namespace emu88Lib;

            RomDevice device;
            RomSlot   slot;

            switch(model)
            {
            case DeviceModel::Sc88:    device = RomDevice::Sc88;    slot = RomSlot::Control; break;
            case DeviceModel::Sc88VL:  device = RomDevice::Sc88VL;  slot = RomSlot::Control; break;
            case DeviceModel::Sc88Pro: device = RomDevice::Sc88Pro; slot = RomSlot::Control; break;
            case DeviceModel::Sc8850:  device = RomDevice::Sc8850;  slot = RomSlot::Data;    rom.pointers = false; rom.toneStride = 256; break;
            case DeviceModel::Sc55Mk2: device = RomDevice::Sc55Mk2; slot = RomSlot::Program; rom.pointers = false; rom.toneStride = 216; rom.kitStride = 0x48C; break;
            default: return false;
            }

            return RomLoader::scan().read(rom.data, device, slot) && rom.data.size() >= 0x1000;
        }
    }

    std::vector<std::string> emu88LoadCapitalTones(const emu88Lib::DeviceModel model)
    {
        BoardRom rom;
        if(!loadBoardRom(model, rom))
            return {};

        const auto*  d        = rom.data.data();
        const size_t size     = rom.data.size();
        const bool   pointers = rom.pointers;
        const size_t stride   = rom.toneStride;

        // GM programs 1 and 2 are "Piano 1" and "Piano 2" on every board; their records
        // anchor the directory and tell it apart from other 128-entry tone lists.
        static const uint8_t anchor[kNameLen]  = {'P','i','a','n','o',' ','1',' ',' ',' ',' ',' '};
        static const uint8_t anchor2[kNameLen] = {'P','i','a','n','o',' ','2',' ',' ',' ',' ',' '};
        size_t base = kNone;
        for(size_t o = 0; o + kNameLen <= size; ++o)
        {
            if(std::memcmp(d + o, anchor, kNameLen) == 0)
            {
                base = o;
                break;
            }
        }
        if(base == kNone)
            return {};

        const size_t width = pointers ? 3 : 2;

        // ROM images are powers of two; pointer bits above the image are tone-type flags.
        const size_t addressMask = (size & (size - 1)) == 0 ? size - 1 : static_cast<size_t>(-1);

        auto resolve = [&](const size_t o) -> size_t
        {
            size_t r;
            if(pointers)
                r = ((static_cast<size_t>(d[o]) << 16) | (static_cast<size_t>(d[o + 1]) << 8) | d[o + 2]) & addressMask;
            else
                r = base + ((static_cast<size_t>(d[o]) << 8) | d[o + 1]) * stride;
            return r + kNameLen <= size ? r : kNone;
        };

        std::vector<size_t> records;
        std::vector<std::string> names;
        std::vector<std::string> result;

        for(size_t o = 0; o + 128 * width <= size; ++o)
        {
            const size_t r0 = resolve(o);
            if(r0 == kNone || std::memcmp(d + r0, anchor, kNameLen) != 0)
                continue;
            const size_t r1 = resolve(o + width);
            if(r1 == kNone || std::memcmp(d + r1, anchor2, kNameLen) != 0)
                continue;

            records.clear();
            names.clear();
            size_t valid = 0, distinct = 0;

            for(size_t k = 0; k < 128; ++k)
            {
                const size_t r = resolve(o + k * width);
                if(r == kNone || !printableName(d + r))
                {
                    names.emplace_back();
                    continue;
                }
                ++valid;
                bool dup = false;
                for(const auto seen : records)
                    dup |= seen == r;
                distinct += dup ? 0 : 1;
                records.push_back(r);
                names.push_back(trimmedName(d + r));
            }

            // A real directory resolves nearly every program to its own record. A run of
            // zeros or a drum index list resolves several entries to the same record.
            if(valid < 120 || distinct != valid)
                continue;

            if(result.empty())
            {
                result = names;
                if(valid == 128)
                    break;
                // A few programs of a board's native map are indexed through a flag the
                // firmware resolves elsewhere; the older map that follows names the same
                // GM instrument, so take those names from it.
                o += 128 * width - 1;
                continue;
            }

            for(size_t k = 0; k < result.size(); ++k)
                if(result[k].empty())
                    result[k] = names[k];
            break;
        }

        for(size_t k = 0; k < result.size(); ++k)
            if(result[k].empty())
                result[k] = "Program " + std::to_string(k + 1);
        return result;
    }

    std::vector<std::string> emu88LoadDrumKits(const emu88Lib::DeviceModel model)
    {
        BoardRom rom;
        if(!loadBoardRom(model, rom))
            return {};

        const auto*  d      = rom.data.data();
        const size_t size   = rom.data.size();
        const size_t stride = rom.kitStride;

        // Kit records sit in blocks at a fixed stride. Every GS board has a kit named
        // STANDARD (or STANDARD 1), so each occurrence of that word heading a printable
        // name seeds a block, walked back to its first record and forward to its last.
        struct Block
        {
            size_t start = 0;
            std::vector<size_t> records;
        };
        std::vector<Block> blocks;

        for(size_t o = 0; o + kNameLen <= size; ++o)
        {
            if(!startsWith(d + o, "STANDARD") || !printableName(d + o))
                continue;

            size_t start = o;
            while(start >= stride && printableName(d + start - stride))
                start -= stride;

            bool known = false;
            for(const auto& b : blocks)
                known |= b.start == start;
            if(known)
                continue;

            Block block;
            block.start = start;
            for(size_t r = start; r + kNameLen <= size && printableName(d + r) && block.records.size() < 128; r += stride)
                block.records.push_back(r);
            if(block.records.size() >= 4)
                blocks.push_back(std::move(block));
        }

        // The directory is 128 bytes of kit index, 0xFF for programs without a kit.
        // Programs 1, 9 and 25 are STANDARD, ROOM and ELECTRONIC kits on every GS board;
        // every other entry must be empty or a distinct valid index. Among the maps a
        // board carries, the one with the most kits is its native map.
        const uint8_t* best      = nullptr;
        const Block*   bestBlock = nullptr;
        size_t         bestValid = 0;

        for(const auto& block : blocks)
        {
            const size_t n = block.records.size();
            for(size_t o = 0; o + 128 <= size; ++o)
            {
                const uint8_t* t = d + o;
                if(t[0] >= n || !startsWith(d + block.records[t[0]], "STANDARD"))
                    continue;
                if(t[8] >= n || !startsWith(d + block.records[t[8]], "ROOM"))
                    continue;
                if(t[24] >= n || !startsWith(d + block.records[t[24]], "ELECTRO"))
                    continue;

                bool seen[256] = {};
                size_t valid = 0;
                bool ok = true;
                for(size_t k = 0; k < 128 && ok; ++k)
                {
                    const uint8_t v = t[k];
                    if(v == 0xff)
                        continue;
                    if(v >= n || seen[v])
                        ok = false;
                    seen[v] = true;
                    ++valid;
                }
                if(!ok || valid < 6 || valid <= bestValid)
                    continue;

                best      = t;
                bestBlock = &block;
                bestValid = valid;
            }
        }

        if(!best)
            return {};

        std::vector<std::string> result(128);
        for(size_t k = 0; k < 128; ++k)
            if(best[k] != 0xff)
                result[k] = trimmedName(d + bestBlock->records[best[k]]);
        return result;
    }

    std::vector<int> emu88Maps(const emu88Lib::DeviceModel model)
    {
        switch(model)
        {
        case emu88Lib::DeviceModel::Sc88:
        case emu88Lib::DeviceModel::Sc88VL:  return {1, 2};
        case emu88Lib::DeviceModel::Sc88Pro: return {1, 2, 3};
        case emu88Lib::DeviceModel::Sc8850:  return {1, 2, 3, 4};
        // The SC-55mkII has one tone map and does not read CC32 at all.
        default: return {};
        }
    }

    std::string emu88MapName(const int map)
    {
        switch(map)
        {
        case 1:  return "SC-55";
        case 2:  return "SC-88";
        case 3:  return "SC-88Pro";
        case 4:  return "SC-8850";
        default: return {};
        }
    }

    std::vector<Emu88Variation> emu88Variations(const emu88Lib::DeviceModel model,
                                                const int map, const int program)
    {
        if(program < 0 || program > 127)
            return {};

        using namespace emu88ToneTable;
        const auto wantModel = static_cast<uint8_t>(model);
        const auto wantMap   = static_cast<uint8_t>(map);

        // The table is sorted by model, map and program, so one program's variations are
        // a contiguous run found by binary search.
        const Entry key{wantModel, wantMap, static_cast<uint8_t>(program), 0, 0};
        const auto less = [](const Entry& a, const Entry& b)
        {
            if(a.model != b.model)     return a.model < b.model;
            if(a.map != b.map)         return a.map < b.map;
            if(a.program != b.program) return a.program < b.program;
            return a.variation < b.variation;
        };

        const auto* first = std::lower_bound(g_entries, g_entries + g_entryCount, key, less);

        std::vector<Emu88Variation> result;
        for(const auto* e = first; e != g_entries + g_entryCount; ++e)
        {
            if(e->model != wantModel || e->map != wantMap || e->program != program)
                break;
            if(e->name < g_nameCount)
                result.push_back({e->variation, g_names[e->name]});
        }
        return result;
    }
}
