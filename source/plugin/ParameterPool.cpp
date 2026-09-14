#include "ParameterPool.h"

#include "HeadlessProcessor.h"
#include "jucePluginLib/processor.h"
#include "nord/n2x/n2xLib/n2xmiditypes.h"
#include "synthLib/midiTypes.h"

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

    std::unique_ptr<ParameterPool::CoreMap> ParameterPool::buildMap(SynthType type) const
    {
        auto map = std::make_unique<CoreMap>();
        map->ccToSlot.fill(-1);
        map->ppToSlot.fill(-1);
        map->type = type;
        map->virusSysex = (type == SynthType::VirusABC || type == SynthType::VirusTI);

        auto json = loadJson(coreFileName(type));
        if(json.empty() && type != SynthType::None)
            json = loadJson(coreFileName(SynthType::None));
        if(json.empty())
            return map;

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
                b.cc = ccs.front();
                map->ccToSlot[static_cast<size_t>(b.cc)] = static_cast<int8_t>(d.index);
            }

            const auto pps = cm.getControlTypes(synthLib::M_POLYPRESSURE, static_cast<uint32_t>(i));
            if(!pps.empty() && pps.front() < 128)
            {
                b.pp = pps.front();
                map->ppToSlot[static_cast<size_t>(b.pp)] = static_cast<int8_t>(d.index);
            }

            const auto natives = cm.getControlTypes(pluginLib::ControllerMap::NrpnType, static_cast<uint32_t>(i));
            if(!natives.empty())
                b.native = natives.front();
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

        const int slot = status == synthLib::M_CONTROLCHANGE ? map->ccToSlot[ev.b] : map->ppToSlot[ev.b];
        if(slot < 0)
            return false;

        const bool fromOutside = ev.source == synthLib::MidiEventSource::Host
                              || ev.source == synthLib::MidiEventSource::Physical;
        m_slots[static_cast<size_t>(slot)]->setFromMidi(ev.c, fromOutside);
        return true;
    }

    void ParameterPool::sendSlot(const SlotParameter&, const SlotParameter::Binding& b, int midiValue)
    {
        if(b.cc < 0 && b.pp < 0 && b.native < 0)
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
        else
        {
            ev.a = synthLib::M_CONTROLCHANGE;
            ev.b = static_cast<uint8_t>(b.cc);
            ev.c = value;
        }

        m_processor.addMidiEvent(ev);
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
