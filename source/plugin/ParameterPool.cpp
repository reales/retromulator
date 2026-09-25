#include "ParameterPool.h"

#include "HeadlessProcessor.h"
#include "jucePluginLib/processor.h"
#include "nord/n2x/n2xLib/n2xmiditypes.h"
#include "ronaldo/je8086/jeLib/state.h"
#include "matrixLib/patch.h"
#include "synthLib/midiTypes.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace retromulator
{
    // ── SlotParameter ─────────────────────────────────────────────────────────

    const SlotParameter::Binding SlotParameter::s_unbound{};

    namespace
    {
        juce::String slotId(int slot)
        {
            return juce::String::formatted("slot_%03d", slot);
        }

        // Generic MIDI performance controllers. On the Virus these must stay
        // plain CC; everything else goes through the page A SysEx param change.
        bool isPerformanceCC(int cc)
        {
            switch(cc)
            {
            case 1: case 2: case 4: case 7: case 10: case 11: case 64: case 65:
                return true;
            default:
                return false;
            }
        }

        // Pedal switches (CC 64-69): on is 64 and up, so a 0/1 slot must not send 1.
        bool isSwitchSlot(const SlotParameter::Binding& b)
        {
            return b.desc && b.desc->isBool && b.cc >= 64 && b.cc <= 69;
        }
    }

    SlotParameter::SlotParameter(ParameterPool& pool, int slot)
        : juce::RangedAudioParameter(juce::ParameterID(slotId(slot), 1), slotId(slot))
        , m_pool(pool)
        , m_slot(slot)
    {
    }

    void SlotParameter::rebind(const Binding* b)
    {
        m_binding.store(b ? b : &s_unbound, std::memory_order_release);
        m_lastSent.store(-1, std::memory_order_relaxed);
        m_touched.store(false, std::memory_order_relaxed);
        m_value.store(getDefaultValue(), std::memory_order_relaxed);
    }

    void SlotParameter::setValue(float newValue)
    {
        newValue = juce::jlimit(0.0f, 1.0f, newValue);
        m_value.store(newValue, std::memory_order_relaxed);

        const auto& b = binding();
        if(!b.desc)
            return;

        const int midi = juce::roundToInt(b.range.convertFrom0to1(newValue));
        if(m_lastSent.exchange(midi, std::memory_order_relaxed) == midi)
            return;

        m_touched.store(true, std::memory_order_relaxed);
        m_pool.sendSlot(*this, b, midi);
    }

    float SlotParameter::getDefaultValue() const
    {
        const auto& b = binding();
        if(!b.desc)
            return 0.0f;
        return b.range.convertTo0to1(static_cast<float>(b.desc->defaultValue));
    }

    juce::String SlotParameter::getName(int maximumStringLength) const
    {
        const auto& b = binding();
        if(!b.desc)
            return {};
        return juce::String(b.desc->displayName).substring(0, maximumStringLength);
    }

    juce::String SlotParameter::getText(float normalisedValue, int) const
    {
        const auto& b = binding();
        const int v = juce::roundToInt(b.range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, normalisedValue)));
        if(!b.desc)
            return juce::String(v);
        return b.desc->valueList.valueToText(static_cast<uint32_t>(v - std::min(0, b.desc->range.getStart())));
    }

    float SlotParameter::getValueForText(const juce::String& text) const
    {
        const auto& b = binding();
        if(!b.desc)
            return b.range.convertTo0to1(static_cast<float>(text.getIntValue()));

        auto res = static_cast<int>(b.desc->valueList.textToValue(text.toStdString()));
        if(b.desc->range.getStart() < 0)
            res += b.desc->range.getStart();
        return b.range.convertTo0to1(static_cast<float>(res));
    }

    bool SlotParameter::isDiscrete() const
    {
        const auto* d = binding().desc;
        return d && (d->isDiscrete || d->isBool);
    }

    bool SlotParameter::isBoolean() const
    {
        const auto* d = binding().desc;
        return d && d->isBool;
    }

    int SlotParameter::getMidiValue() const
    {
        return juce::roundToInt(binding().range.convertFrom0to1(getValue()));
    }

    void SlotParameter::setFromMidi(int midiValue, bool notifyHost)
    {
        const auto& b = binding();
        if(!b.desc)
            return;

        const int v = juce::jlimit(b.desc->range.getStart(), b.desc->range.getEnd(), midiValue);
        m_lastSent.store(v, std::memory_order_relaxed);
        m_touched.store(true, std::memory_order_relaxed);

        const float n = b.range.convertTo0to1(static_cast<float>(v));

        if(notifyHost)
        {
            beginChangeGesture();
            setValueNotifyingHost(n);
            endChangeGesture();
        }
        else
        {
            m_value.store(n, std::memory_order_relaxed);
            sendValueChangedMessageToListeners(n);
        }
    }

    // ── ParameterPool ─────────────────────────────────────────────────────────

    ParameterPool::ParameterPool(pluginLib::Processor& processor)
        : m_processor(processor)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>("slots", "Parameters", "|");

        for(int i = 0; i < NumSlots; ++i)
        {
            auto p = std::make_unique<SlotParameter>(*this, i);
            m_slots[static_cast<size_t>(i)] = p.get();
            group->addChild(std::move(p));
        }

        m_processor.addParameterGroup(std::move(group));
    }

    const char* ParameterPool::coreFileName(SynthType type)
    {
        switch(type)
        {
        case SynthType::VirusABC:  return "virus_a";
        case SynthType::VirusTI:   return "virus_ti";
        case SynthType::MicroQ:    return "mq";
        case SynthType::XT:        return "xt";
        case SynthType::NordN2X:   return "n2x";
        case SynthType::JE8086:    return "je8086";
        case SynthType::DX7:       return "dx7";
        case SynthType::AkaiS1000: return "akai";
        case SynthType::OpenWurli: return "wurli";
        case SynthType::OPL3:      return "opl3";
        case SynthType::SID:       return "sid";
        case SynthType::Ayumi:     return "ayumi";
        case SynthType::Emu88:     return "emu88";
        case SynthType::Trackermeister: return "tracker";
        case SynthType::Matrix:    return "matrix";
        default:                   return "generic";
        }
    }

    std::string ParameterPool::loadJson(const std::string& coreFile) const
    {
        const std::string name = "parameterDescriptions_" + coreFile + ".json";

        // The descriptions are linked into the binary, so they are available in
        // every format on every platform with no install-time file copying.
        if(const auto res = m_processor.findResource(name))
            return {res->first, res->second};

        // Fall back to a file in the user data folder so a core's parameter set
        // can be overridden without a rebuild.
        const auto file = juce::File(HeadlessProcessor::getDataFolder())
                              .getChildFile("Params").getChildFile(juce::String(name));
        if(file.existsAsFile())
            return file.loadFileAsString().toStdString();

        return {};
    }

    void ParameterPool::patchEmu88ToneLists(std::string& json) const
    {
        const auto* hp = dynamic_cast<const HeadlessProcessor*>(&m_processor);
        if(!hp)
            return;

        // "001 Piano 1" style, matching the tone menu. A program the ROM leaves empty
        // keeps its bare number so the list stays 128 entries long either way.
        const auto buildList = [hp](const bool drums)
        {
            std::string out;
            for(int i = 0; i < 128; ++i)
            {
                const auto name = hp->getEmu88ProgramName(drums ? 9 : 0, i);
                char num[8];
                snprintf(num, sizeof(num), "%03d", i + 1);
                out += i ? ",\n      \"" : "\"";
                out += num;
                if(!name.empty())
                {
                    out += ' ';
                    // The ROM names are plain ASCII, but a stray quote or backslash would
                    // break the document, so both are dropped rather than escaped.
                    for(const char c : name)
                        if(c != '"' && c != '\\')
                            out += c;
                }
                out += '"';
            }
            return out;
        };

        const auto replaceList = [&json](const std::string& key, const std::string& body)
        {
            const auto keyPos = json.find("\"" + key + "\"");
            if(keyPos == std::string::npos)
                return;
            const auto open = json.find('[', keyPos);
            if(open == std::string::npos)
                return;
            const auto close = json.find(']', open);
            if(close == std::string::npos)
                return;
            json.replace(open + 1, close - open - 1, "\n      " + body + "\n    ");
        };

        replaceList("gs_prog", buildList(false));
        replaceList("gs_kit",  buildList(true));
    }

    std::unique_ptr<ParameterPool::CoreMap> ParameterPool::buildMap(SynthType type) const
    {
        auto map = std::make_unique<CoreMap>();
        map->ccToSlot.fill(-1);
        map->ppToSlot.fill(-1);
        map->type = type;
        map->virusSysex = (type == SynthType::VirusABC || type == SynthType::VirusTI);
        map->perPart    = (type == SynthType::Emu88);

        auto json = loadJson(coreFileName(type));
        if(json.empty() && type != SynthType::None)
            json = loadJson(coreFileName(SynthType::None));
        if(json.empty())
            return map;

        if(type == SynthType::Emu88)
            patchEmu88ToneLists(json);

        map->descriptions = std::make_unique<pluginLib::ParameterDescriptions>(json);

        if(!map->descriptions->isValid())
        {
            fprintf(stderr, "[Params] %s: %s\n", coreFileName(type), map->descriptions->getErrors().c_str());
            map->descriptions.reset();
            return map;
        }

        const auto& descs = map->descriptions->getDescriptions();
        const auto& cm    = map->descriptions->getControllerMap();

        for(size_t i = 0; i < descs.size(); ++i)
        {
            const auto& d = descs[i];
            if(!d.isPublic || d.index >= NumSlots)
                continue;

            auto& b = map->bindings[d.index];
            b.desc  = &d;
            b.range = juce::NormalisableRange<float>(static_cast<float>(d.range.getStart()),
                                                     static_cast<float>(d.range.getEnd()),
                                                     (d.isDiscrete || d.isBool) ? 1.0f : 0.0f);

            const auto ccs = cm.getControlTypes(synthLib::M_CONTROLCHANGE, static_cast<uint32_t>(i));
            if(!ccs.empty() && ccs.front() < 128)
            {
                b.cc = static_cast<int>(ccs.front());
                map->ccToSlot[static_cast<size_t>(b.cc)] = static_cast<int8_t>(d.index);
            }

            const auto pps = cm.getControlTypes(synthLib::M_POLYPRESSURE, static_cast<uint32_t>(i));
            if(!pps.empty() && pps.front() < 128)
            {
                b.pp = static_cast<int>(pps.front());
                map->ppToSlot[static_cast<size_t>(b.pp)] = static_cast<int8_t>(d.index);
            }

            const auto natives = cm.getControlTypes(pluginLib::ControllerMap::NrpnType, static_cast<uint32_t>(i));
            if(!natives.empty())
                b.native = static_cast<int>(natives.front());
        }

        if(const auto* dumps = juce::JSON::parse(juce::String(json))["dumpmap"].getDynamicObject())
        {
            for(const auto& packet : dumps->getProperties())
            {
                CoreMap::DumpLayout layout;
                layout.size = static_cast<uint32_t>(static_cast<int>(packet.value["size"]));
                if(const auto* header = packet.value["header"].getArray())
                    for(const auto& h : *header)
                        layout.header.emplace_back(static_cast<uint32_t>(static_cast<int>(h[0])), static_cast<uint8_t>(static_cast<int>(h[1])));
                if(const auto* terms = packet.value["params"].getArray())
                    for(const auto& t : *terms)
                        layout.terms.push_back({static_cast<uint8_t>(static_cast<int>(t[0])), static_cast<uint16_t>(static_cast<int>(t[1])),
                                                static_cast<uint8_t>(static_cast<int>(t[2])), static_cast<uint8_t>(static_cast<int>(t[3])),
                                                static_cast<uint8_t>(static_cast<int>(t[4]))});
                map->dumps.push_back(std::move(layout));
            }
        }
        return map;
    }

    const ParameterPool::CoreMap& ParameterPool::mapFor(SynthType type)
    {
        const auto idx = static_cast<size_t>(static_cast<int>(type) + 1);   // None = -1 -> 0
        auto& slot = m_maps[idx];
        if(!slot)
            slot = buildMap(type);
        return *slot;
    }

    void ParameterPool::refreshFor(const SynthType type)
    {
        const auto idx = static_cast<size_t>(static_cast<int>(type) + 1);
        m_maps[idx].reset();
        if(m_core == type)
            setCore(type);
    }

    void ParameterPool::setCore(SynthType type)
    {
        const auto& map = mapFor(type);
        m_core = type;
        m_current.store(&map, std::memory_order_release);

        for(int i = 0; i < NumSlots; ++i)
            m_slots[static_cast<size_t>(i)]->rebind(&map.bindings[static_cast<size_t>(i)]);

        m_processor.updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true));
    }

    bool ParameterPool::handleIncomingCC(const synthLib::SMidiEvent& ev)
    {
        const auto status = ev.a & 0xf0;
        if((status != synthLib::M_CONTROLCHANGE && status != synthLib::M_POLYPRESSURE) || ev.b >= 128)
            return false;

        const auto* map = m_current.load(std::memory_order_acquire);
        if(!map)
            return false;

        // On a multitimbral core the same CC means something different per part, so only the
        // part being edited feeds the visible parameters. Mono-timbral cores keep channel 0
        // on both sides and are unaffected.
        if(map->perPart && (ev.a & 0x0f) != getPartChannel())
            return false;

        const int slot = status == synthLib::M_CONTROLCHANGE ? map->ccToSlot[ev.b] : map->ppToSlot[ev.b];
        if(slot < 0)
            return false;

        const bool fromOutside = ev.source == synthLib::MidiEventSource::Host
                              || ev.source == synthLib::MidiEventSource::Physical;
        auto* s = m_slots[static_cast<size_t>(slot)];

        // The Matrix firmware ignores these CCs: spread 0-127 over the parameter's range
        // and send it as a remote parameter edit
        const auto& b = s->binding();
        if(map->type == SynthType::Matrix && b.native >= 0 && b.desc)
        {
            const int lo = b.desc->range.getStart();
            const int v = lo + ev.c * (b.desc->range.getEnd() - lo + 1) / 128;
            s->setFromMidi(v, fromOutside);
            sendSlot(*s, b, v);
            return true;
        }

        const bool isSwitch = status == synthLib::M_CONTROLCHANGE && isSwitchSlot(s->binding());
        s->setFromMidi(isSwitch ? (ev.c >= 64 ? 1 : 0) : ev.c, fromOutside);
        return true;
    }

    void ParameterPool::sendSlot(const SlotParameter&, const SlotParameter::Binding& b, int midiValue)
    {
        if(b.cc < 0 && b.pp < 0 && b.native < 0)
            return;

        // Mirroring only reports what the device already did; sending it back loops.
        if(isMirroring())
            return;

        const auto* map = m_current.load(std::memory_order_acquire);
        const bool virus = map && map->virusSysex;
        const auto value = static_cast<uint8_t>(juce::jlimit(0, 127, midiValue));

        synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);

        if(b.native >= 0)
        {
            // core-specific parameter index
            const auto hi = static_cast<uint8_t>((b.native >> 7) & 0x7f);
            const auto lo = static_cast<uint8_t>(b.native & 0x7f);
            if(map && map->type == SynthType::NordN2X)
            {
                ev.sysex = {0xf0, n2x::IdClavia, n2x::DefaultDeviceId, n2x::IdN2X,
                            n2x::EmuSetMultiParam, hi, lo, 0 /* slot A */, value, 0xf7};
            }
            else if(map && map->type == SynthType::DX7)
            {
                // Yamaha voice parameter change; the device patches in its RX channel
                ev.sysex = {0xf0, 0x43, 0x10, hi, lo, value, 0xf7};
            }
            else if(map && map->type == SynthType::Emu88)
            {
                sendEmu88Native(b.native, value);
                return;
            }
            else if(map && map->type == SynthType::Matrix)
            {
                // remote parameter edit; signed parameters sit around the middle of the slot
                int v = value;
                if(b.desc && b.desc->isBipolar)
                    v -= static_cast<int>(b.range.end) / 2;
                const auto msg = matrixLib::patch::createParamChange(static_cast<uint8_t>(b.native), v);
                ev.sysex.assign(msg.begin(), msg.end());
            }
            else if(map && map->type == SynthType::JE8086)
            {
                // DT1 into the temp performance's Upper patch: plain CCs need the firmware's
                // Tx/Rx Edit switch and a matching part channel. Upper only, MIDI in is 31.25k.
                ev.sysex = jeLib::State::createParameterChange(jeLib::PerformanceData::PatchUpper,
                                                               static_cast<jeLib::Patch>(b.native), value);
            }
            else
                return;
            ev.a = 0xf0;
        }
        else if(b.cc < 0)
        {
            // page B: poly pressure on the Virus, otherwise the same via SysEx
            const auto idx = static_cast<uint8_t>(b.pp);
            if(virus)
            {
                ev.sysex = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x71, 0x40, idx, value, 0xf7};
                ev.a = 0xf0;
            }
            else
            {
                ev.a = synthLib::M_POLYPRESSURE;
                ev.b = idx;
                ev.c = value;
            }
        }
        else if(virus && !isPerformanceCC(b.cc))
        {
            // Page A param change addressed to the single edit buffer (part 0x40),
            // omni device id. Covers CCs the emulator drops as plain CC (chorus/delay).
            const auto cc = static_cast<uint8_t>(b.cc);
            ev.sysex = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x40, cc, value, 0xf7};
            ev.a = 0xf0;
        }
        else if(map && map->type == SynthType::Emu88 && (b.cc == 0 || b.cc == 32))
        {
            // Bank select is per-part state the processor caches, so it takes the same
            // route as the Bank MSB/LSB natives rather than going out as a bare CC.
            if(auto* hp = dynamic_cast<HeadlessProcessor*>(&m_processor))
                hp->setEmu88PartBank(getPartChannel(), b.cc, value);
            return;
        }
        else
        {
            // Only 88emu addresses the part the editor is showing; the part channel outlives
            // a core switch, so the other cores must not read it.
            const auto channel = map && map->perPart ? getPartChannel() : 0;
            ev.a = static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel);
            ev.b = static_cast<uint8_t>(b.cc);
            ev.c = isSwitchSlot(b) && value ? uint8_t(127) : value;
        }

        m_processor.addMidiEvent(ev);
    }

    void ParameterPool::markNativeTouched(const int native)
    {
        for(auto* s : m_slots)
        {
            const auto& b = s->binding();
            if(b.desc && b.native == native)
            {
                s->markTouched();
                return;
            }
        }
    }

    void ParameterPool::sendEmu88Native(const int native, const uint8_t value)
    {
        auto* hp = dynamic_cast<HeadlessProcessor*>(&m_processor);

        // Checked before the GS address range, which starts lower and would match.
        if(native >= kEmu88PartProgFirst && native <= kEmu88PartProgLast)
        {
            if(hp)
                hp->setEmu88PartProgram(native - kEmu88PartProgFirst, value);
            return;
        }

        if(native >= kEmu88GsAddressFirst)
        {
            // Roland DT1: address is three 7-bit bytes, checksum makes the sum of
            // address+data a multiple of 128.
            auto address = native;
            auto data = value;
            if(native >= kEmu88PartGsFirst && native <= kEmu88PartGsLast)
            {
                // GS block order: part 10 is block 0, parts 1-9 are 1-9. Tone Modify takes -50..+50.
                const int part = getPartChannel();
                address |= (part == 9 ? 0 : part < 9 ? part + 1 : part) << 8;
                data = static_cast<uint8_t>(juce::jlimit(0x0e, 0x72, static_cast<int>(value)));
            }
            const auto a1 = static_cast<uint8_t>((address >> 16) & 0x7f);
            const auto a2 = static_cast<uint8_t>((address >>  8) & 0x7f);
            const auto a3 = static_cast<uint8_t>( address        & 0x7f);
            const auto sum = static_cast<uint8_t>((a1 + a2 + a3 + data) & 0x7f);
            const auto chk = static_cast<uint8_t>((128 - sum) & 0x7f);

            synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
            ev.sysex = {0xf0, 0x41, 0x10, 0x42, 0x12, a1, a2, a3, data, chk, 0xf7};
            ev.a = 0xf0;
            m_processor.addMidiEvent(ev);
            return;
        }

        switch(native)
        {
        // Both of these rebuild the tone-name list the editor reads, so they run on the
        // message thread: automation reaches sendSlot from the audio thread.
        case kEmu88TransportNative:
            // A two-value switch, so the host shows Stop/Play rather than 128 rows of
            // them. It only acts on a change, as the editor's timer mirrors the transport
            // back into this slot and re-applying the echo would restart the song every block.
            if(hp && hp->hasMidiFile())
            {
                const bool play = value != 0;
                if(play != hp->isMidiFilePlaying())
                {
                    juce::MessageManager::callAsync([hp, play]
                    {
                        if(!hp->hasMidiFile() || play == hp->isMidiFilePlaying())
                            return;
                        if(play) hp->playMidiFile();
                        else     hp->stopMidiFile();
                    });
                }
            }
            return;

        case kEmu88PartNative:
            // The part is a view selector: it repoints every CC parameter at that
            // channel and reloads the tone names, as the editor's part menu does.
            // setEmu88Part clears every touched flag (the old part's edits must not
            // land on the new one), so the selector re-marks itself afterwards.
            // The equality test also swallows the host's echo of the timer mirror:
            // re-applying the part there would clear the flags of edits already made.
            if(hp && value < 16 && value != getPartChannel())
            {
                juce::MessageManager::callAsync([this, hp, value]
                {
                    if(value == getPartChannel())
                        return;
                    hp->setEmu88Part(value);
                    markNativeTouched(kEmu88PartNative);
                });
            }
            return;

        case kEmu88ProgramNative:
            // Route through the processor so the patch name and the editor's tone
            // list follow the automation, exactly as picking a tone in the UI does.
            // Only when it actually differs: the editor mirrors this slot from
            // m_currentProgram on a timer, so acting on every echo would re-send
            // program changes the device already made.
            if(hp && hp->getCurrentProgram() != static_cast<int>(value))
                juce::MessageManager::callAsync([hp, value]
                {
                    if(hp->getCurrentProgram() != static_cast<int>(value))
                        hp->selectProgram(value);
                });
            return;

        case kEmu88BankMsbNative:
        case kEmu88BankLsbNative:
            sendEmu88ProgramChange(native, value);
            return;

        default:
            return;
        }
    }

    void ParameterPool::sendEmu88ProgramChange(const int native, const uint8_t value)
    {
        const auto cc = native == kEmu88BankMsbNative ? 0 : 32;

        // Routed through the processor so the per-part bank cache follows the edit:
        // sending the CC straight to the device would leave the cache on the old bank,
        // and a later resend would put that stale variation back.
        if(auto* hp = dynamic_cast<HeadlessProcessor*>(&m_processor))
            hp->setEmu88PartBank(getPartChannel(), cc, value);
    }

    void ParameterPool::setNativeSlotValue(const int native, const int value, const bool notifyHost,
                                           const bool transient)
    {
        for(auto* s : m_slots)
        {
            const auto& b = s->binding();
            if(!b.desc || b.native != native)
                continue;
            if(s->getMidiValue() != value)
            {
                // The device is already in this state, so the update must not be sent back
                // to it. Without the guard the host echoes the new value into setValue,
                // that reaches sendSlot, and for the program slot each send changes the
                // program the next mirror then disagrees with, flooding the board.
                m_mirroring.store(true, std::memory_order_release);
                s->setFromMidi(value, notifyHost);
                m_mirroring.store(false, std::memory_order_release);
            }
            // Transient state is not a sound edit: never resent after boot nor saved.
            // The part and program are, so they keep their touched flag.
            if(transient)
                s->clearTouched();
            return;
        }
    }

    void ParameterPool::clearTouched()
    {
        for(auto* s : m_slots)
            s->clearTouched();
    }

    void ParameterPool::resendTouched()
    {
        for(auto* s : m_slots)
        {
            if(!s->isTouched())
                continue;
            const auto& b = s->binding();
            if(b.desc)
                sendSlot(*s, b, s->getMidiValue());
        }
    }

    std::vector<std::pair<uint8_t, uint8_t>> ParameterPool::getTouchedValues() const
    {
        std::vector<std::pair<uint8_t, uint8_t>> out;
        for(auto* s : m_slots)
        {
            if(s->isTouched() && s->binding().desc)
                out.emplace_back(static_cast<uint8_t>(s->getSlot()),
                                 static_cast<uint8_t>(juce::jlimit(0, 127, s->getMidiValue())));
        }
        return out;
    }

    namespace
    {
        using SlotValues = std::array<int, ParameterPool::NumSlots>;

        // DX7 packed voice (VMEM, 128 bytes) to VCED parameter numbers, which the dx7
        // slots use as their native index.
        void unpackDx7Voice(const uint8_t* v, std::array<int, 155>& vced)
        {
            for(int op = 0; op < 6; ++op)   // both store OP6 first
            {
                const uint8_t* s = v + op * 17;
                int* d = vced.data() + op * 21;
                for(int i = 0; i < 11; ++i)
                    d[i] = s[i];            // EG rates/levels, break point, depths
                d[11] = s[11] & 3;          d[12] = (s[11] >> 2) & 3;
                d[13] = s[12] & 7;          d[20] = (s[12] >> 3) & 15;
                d[14] = s[13] & 3;          d[15] = (s[13] >> 2) & 7;
                d[16] = s[14];
                d[17] = s[15] & 1;          d[18] = (s[15] >> 1) & 31;
                d[19] = s[16];
            }
            for(int i = 0; i < 8; ++i)
                vced[126 + i] = v[102 + i];
            vced[134] = v[110] & 31;
            vced[135] = v[111] & 7;         vced[136] = (v[111] >> 3) & 1;
            for(int i = 0; i < 4; ++i)
                vced[137 + i] = v[112 + i];
            vced[141] = v[116] & 1;         vced[142] = (v[116] >> 1) & 7;
            vced[143] = (v[116] >> 4) & 7;
            vced[144] = v[117];
        }
    }

    void ParameterPool::syncFromPatch(const synthLib::SysexBufferList& messages)
    {
        const auto* map = m_current.load(std::memory_order_acquire);
        if(!map || !map->descriptions)
            return;

        SlotValues values;
        values.fill(-1);

        const auto setNative = [&](const int native, const int value)
        {
            for(size_t i = 0; i < static_cast<size_t>(NumSlots); ++i)
                if(map->bindings[i].desc && map->bindings[i].native == native)
                    values[i] = value;
        };

        for(const auto& m : messages)
        {
            if(map->type == SynthType::DX7)
            {
                // a bank entry is one packed voice; a single voice dump carries VCED as is
                std::array<int, 155> vced{};
                if(m.size() == 128)
                    unpackDx7Voice(m.data(), vced);
                else if(m.size() == 163 && m[0] == 0xf0 && m[1] == 0x43 && m[3] == 0x00)
                    for(size_t i = 0; i < vced.size(); ++i)
                        vced[i] = m[6 + i];
                else
                    continue;
                for(size_t i = 0; i < vced.size(); ++i)
                    setNative(static_cast<int>(i), vced[i]);
                continue;
            }

            if(map->type == SynthType::Matrix)
            {
                const auto data = matrixLib::patch::decode(m.data(), m.size());
                if(!data)
                    continue;
                for(size_t i = 0; i < static_cast<size_t>(NumSlots); ++i)
                {
                    const auto& b = map->bindings[i];
                    if(!b.desc || b.native < 0)
                        continue;
                    if(const auto* p = matrixLib::patch::findParam(static_cast<uint8_t>(b.native)))
                    {
                        const int v = matrixLib::patch::getParamValue(*data, *p);
                        values[i] = b.desc->isBipolar ? v - p->min : v;
                    }
                }
                continue;
            }

            if(map->type == SynthType::JE8086)
            {
                // Roland DT1. Patch data starts at a user patch (02 bb pp oo) or at the
                // Upper patch of a performance (01/03 xx 40 oo); the offset is the native.
                if(m.size() < 12 || m[0] != 0xf0 || m[1] != 0x41 || m[5] != 0x12)
                    continue;
                int offset;
                if(m[6] == 0x02)
                    offset = ((m[8] & 1) << 7) | m[9];
                else if((m[6] == 0x01 || m[6] == 0x03) && m[8] == 0x40)
                    offset = m[9];
                else
                    continue;
                for(size_t i = 10; i + 2 < m.size(); ++i)
                    setNative(offset + static_cast<int>(i - 10), m[i]);
                continue;
            }

            // Cores with an upstream single dump layout. Firmware versions add or drop bytes
            // at the end (names, padding), so the header decides and only shared bytes are read.
            for(const auto& layout : map->dumps)
            {
                if(m.size() + 64 < layout.size || layout.size + 64 < m.size())
                    continue;
                if(!std::all_of(layout.header.begin(), layout.header.end(),
                                [&](const auto& h) { return h.first < m.size() && m[h.first] == h.second; }))
                    continue;
                const auto limit = std::min<size_t>(layout.size, m.size()) - 1;
                for(const auto& t : layout.terms)
                {
                    if(t.byte >= limit || t.slot >= NumSlots)
                        continue;
                    auto& v = values[t.slot];
                    v = (v < 0 ? 0 : v) | (((m[t.byte] << t.shiftL) >> t.shiftR) & t.mask);
                }
                break;
            }
        }

        m_mirroring.store(true, std::memory_order_release);
        for(size_t i = 0; i < static_cast<size_t>(NumSlots); ++i)
        {
            auto* s = m_slots[i];
            if(values[i] < 0 || s->isTouched() || !s->binding().desc)
                continue;
            if(s->getMidiValue() != values[i])
                s->setFromMidi(values[i], true);
            s->clearTouched();
        }
        m_mirroring.store(false, std::memory_order_release);
    }

    void ParameterPool::restoreValues(const std::vector<std::pair<uint8_t, uint8_t>>& values)
    {
        for(const auto& [slot, value] : values)
        {
            if(slot >= NumSlots)
                continue;
            auto* s = m_slots[slot];
            s->setFromMidi(value, false);
            const auto& b = s->binding();
            if(b.desc)
                sendSlot(*s, b, value);
        }
    }
}
