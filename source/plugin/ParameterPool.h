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
        void markTouched() { m_touched.store(true, std::memory_order_relaxed); }

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

        // Multitimbral cores (88emu) edit one part at a time: the parameters shown are the
        // selected part's, so outgoing CCs carry its channel and incoming CCs are only adopted
        // from it. 0-15; mono-timbral cores leave this at 0 and behave as before.
        void setPartChannel(uint8_t channel) { m_partChannel.store(channel & 0x0f, std::memory_order_release); }
        uint8_t getPartChannel() const { return m_partChannel.load(std::memory_order_acquire); }

        // Native indices ("nrpn" in the JSON) for 88emu parameters that are not plain CCs.
        // 1-15 drive the plugin itself (part view, program change); 0x40.. are the
        // Roland GS global address map, sent as a one-byte DT1 SysEx to that address.
        // Transport for the loaded song, mirroring the C1/D1 keys and the bank combo.
        static constexpr int kEmu88TransportNative  = 1;
        static constexpr int kEmu88PartNative       = 2;
        static constexpr int kEmu88ProgramNative    = 3;
        static constexpr int kEmu88BankMsbNative    = 4;
        static constexpr int kEmu88BankLsbNative    = 5;
        // Native values at or above this are a GS address: 0x400130 packs as 0x40 0x01 0x30.
        // Per-part program: one parameter per part, so automating a part's tone does not
        // depend on the Part selector's position. Low nibble is the part, 0-15.
        static constexpr int kEmu88PartProgFirst    = 0x500000;
        static constexpr int kEmu88PartProgLast     = 0x50000f;

        // Part block 40 1x yy (Tone Modify: cutoff, resonance, envelope, vibrato). The JSON
        // stores x = 0; the edited part fills it in. Only the SC-8850 answers CC 71-78.
        static constexpr int kEmu88PartGsFirst      = 0x401000;
        static constexpr int kEmu88PartGsLast       = 0x4010ff;

        static constexpr int kEmu88GsAddressFirst   = 0x400000;

        // Message thread. Sets the slot bound to a native index without sending it to the device.
        // transient: clear the touched flag afterwards, for state that is not a sound edit
        // and so must not be saved or resent after a reboot.
        void setNativeSlotValue(int native, int value, bool notifyHost, bool transient = true);

        // True while a slot is being updated to mirror state the device already has.
        // sendSlot must not act on those: a mirror that writes back to the device makes
        // the host's echo of the new value a fresh edit, and the pair feed each other.
        bool isMirroring() const { return m_mirroring.load(std::memory_order_acquire); }

        // Rebuild the map so the program value lists pick up a newly loaded board's
        // names. The host caches parameter text when it builds its tree, so the names
        // must be in the descriptions before that, not produced on demand.
        void refreshFor(SynthType type);

        void clearTouched();
        void resendTouched();

        // Touched slots as (slot, value) pairs for state persistence.
        std::vector<std::pair<uint8_t, uint8_t>> getTouchedValues() const;
        void restoreValues(const std::vector<std::pair<uint8_t, uint8_t>>& values);

        // Point the slots at the patch just sent, so the host shows its values instead of
        // defaults. Touched slots keep their edits. Nothing goes back to the device.
        void syncFromPatch(const synthLib::SysexBufferList& messages);

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
            // Parameters address one part of a multitimbral device rather than the whole synth.
            bool perPart = false;

            // Slot positions in the core's single dump ("dumpmap" in the JSON)
            struct DumpTerm { uint8_t slot; uint16_t byte; uint8_t mask, shiftR, shiftL; };
            struct DumpLayout
            {
                uint32_t size = 0;
                std::vector<std::pair<uint32_t, uint8_t>> header;
                std::vector<DumpTerm> terms;
            };
            std::vector<DumpLayout> dumps;
        };

        const CoreMap& mapFor(SynthType type);
        std::unique_ptr<CoreMap> buildMap(SynthType type) const;
        std::string loadJson(const std::string& coreFile) const;
        // Substitute the loaded board's tone and kit names into the program value lists.
        void patchEmu88ToneLists(std::string& json) const;

        // 88emu parameters that are not plain CCs: GS SysEx, or a plugin-side action.
        void sendEmu88Native(int native, uint8_t value);
        // Bank MSB/LSB natives: hand the variation to the processor, which caches it per
        // part and re-sends the program so the change is audible at once.
        void sendEmu88ProgramChange(int native, uint8_t value);
        void markNativeTouched(int native);

        pluginLib::Processor& m_processor;
        std::array<SlotParameter*, NumSlots> m_slots{};
        std::array<std::unique_ptr<CoreMap>, static_cast<size_t>(SynthType::Count) + 1> m_maps;
        std::atomic<const CoreMap*> m_current{nullptr};
        std::atomic<uint8_t> m_partChannel{0};
        std::atomic<bool> m_mirroring{false};
        SynthType m_core = SynthType::None;
    };
}
