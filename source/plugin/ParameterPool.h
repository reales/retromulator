#pragma once

#include "SynthType.h"
#include "jucePluginLib/parameterdescriptions.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace pluginLib { class Processor; }
namespace synthLib { struct SMidiEvent; }

namespace retromulator
{
    class ParameterPool;

    // One of 128 fixed host parameters. The slot ID never changes; only the
    // binding (name, range, value list, CC) is swapped when the core changes.
    class SlotParameter final : public juce::RangedAudioParameter
    {
    public:
        struct Binding
        {
            const pluginLib::Description* desc = nullptr;   // null = unused slot
            juce::NormalisableRange<float> range{0.0f, 127.0f, 0.0f};
            int cc = -1;      // control change number
            int pp = -1;      // poly pressure index (Virus page B)
            int native = -1;  // core-specific parameter index (JSON "nrpn"), N2X multi params
        };

        SlotParameter(ParameterPool& pool, int slot);

        float getValue() const override { return m_value.load(std::memory_order_relaxed); }
        void  setValue(float newValue) override;
        float getDefaultValue() const override;

        juce::String getName(int maximumStringLength) const override;
        juce::String getLabel() const override { return {}; }
        juce::String getText(float normalisedValue, int) const override;
        float getValueForText(const juce::String& text) const override;

        const juce::NormalisableRange<float>& getNormalisableRange() const override { return binding().range; }
        bool isDiscrete() const override;
        bool isBoolean() const override;
        bool isAutomatable() const override { return binding().desc != nullptr; }

        int  getSlot() const { return m_slot; }
        int  getMidiValue() const;
        bool isTouched() const { return m_touched.load(std::memory_order_relaxed); }

        // Bindings live as long as the pool, so a plain atomic pointer is enough.
        void rebind(const Binding* b);
        const Binding& binding() const { return *m_binding.load(std::memory_order_acquire); }

        // Called on the message thread for CCs coming from host/physical MIDI.
        void setFromMidi(int midiValue, bool notifyHost);
        void clearTouched() { m_touched.store(false, std::memory_order_relaxed); }

    private:
        ParameterPool& m_pool;
        const int m_slot;

        static const Binding s_unbound;
        std::atomic<const Binding*> m_binding{&s_unbound};
        std::atomic<float> m_value{0.0f};
        std::atomic<int>   m_lastSent{-1};
        std::atomic<bool>  m_touched{false};
    };

    // Owns the per-core description sets and the 128 slots registered with the host.
    class ParameterPool
    {
    public:
        static constexpr int NumSlots = 128;

        explicit ParameterPool(pluginLib::Processor& processor);

        // Message thread. Rebinds every slot and asks the host to re-read the tree.
        void setCore(SynthType type);
        SynthType getCore() const { return m_core; }

        // Message thread. Returns true if the CC was mapped to a slot.
        bool handleIncomingCC(const synthLib::SMidiEvent& ev);

        // Any thread. Builds the MIDI for one slot and pushes it to the device.
        void sendSlot(const SlotParameter& slot, const SlotParameter::Binding& b, int midiValue);

        void clearTouched();
        void resendTouched();

        // Touched slots as (slot, value) pairs for state persistence.
        std::vector<std::pair<uint8_t, uint8_t>> getTouchedValues() const;
        void restoreValues(const std::vector<std::pair<uint8_t, uint8_t>>& values);

        static const char* coreFileName(SynthType type);

    private:
        struct CoreMap
        {
            std::unique_ptr<pluginLib::ParameterDescriptions> descriptions;
            std::array<SlotParameter::Binding, NumSlots> bindings{};
            std::array<int8_t, 128> ccToSlot{};
            std::array<int8_t, 128> ppToSlot{};
            SynthType type = SynthType::None;
            bool virusSysex = false;
        };

        const CoreMap& mapFor(SynthType type);
        std::unique_ptr<CoreMap> buildMap(SynthType type) const;
        std::string loadJson(const std::string& coreFile) const;

        pluginLib::Processor& m_processor;
        std::array<SlotParameter*, NumSlots> m_slots{};
        std::array<std::unique_ptr<CoreMap>, static_cast<size_t>(SynthType::Count) + 1> m_maps;
        std::atomic<const CoreMap*> m_current{nullptr};
        SynthType m_core = SynthType::None;
    };
}
