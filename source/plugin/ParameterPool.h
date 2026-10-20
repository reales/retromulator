#pragma once

#include "SynthType.h"
#include "jucePluginLib/parameterdescriptions.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
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
            int board = -1;   // board parameter sent through the board's own message (slot links)
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

    public:
        static const Binding& unboundBinding() { return s_unbound; }
    private:
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

        // Slot links: any slot can be rebound to a board parameter, replacing its default.
        // Message thread; setCore restores the defaults.
        struct LinkTarget
        {
            std::string name;
            int min = 0;
            int max = 127;
            bool bipolar = false;
            std::vector<std::string> values;   // display texts from min upwards
            int cc = -1;
            int native = -1;
            int board = -1;
        };
        void linkSlot(int slot, const LinkTarget& target);
        // Sets a slot to what the device already has, without sending it.
        void setSlotMirrored(int slot, int value);
        void clearSlot(int slot);
        juce::String getSlotName(int slot) const;
        // tells the host the slot names and ranges changed
        void announceLinks();
        // Called from sendSlot (any thread) for slots linked to a board parameter.
        void setBoardSender(std::function<void(int boardParam, int value)> sender) { m_boardSend = std::move(sender); }

        // Message thread. Mirrors a value the device was just sent by other means.
        void setSlotFromMidi(int slot, int value);
        // The slot's value in MIDI units, -1 when the slot is unused.
        int getSlotMidiValue(int slot) const;

        void clearTouched();
        void resendTouched();

        // Touched slots as (slot, value) pairs for state persistence.
        std::vector<std::pair<uint8_t, uint8_t>> getTouchedValues() const;
        void restoreValues(const std::vector<std::pair<uint8_t, uint8_t>>& values);

        // Point the slots at the patch just sent, so the host shows its values instead of
        // defaults. Touched slots keep their edits. Nothing goes back to the device.
        void syncFromPatch(const synthLib::SysexBufferList& messages);

        // Board support for cores whose edits and dumps are core specific (DX7, Matrix,
        // JE-8086): send one native parameter, decode dumps into native / value pairs.
        // captured, when given, receives the messages sent (for MIDI out)
        void sendNative(int native, int value, std::vector<synthLib::SMidiEvent>* captured = nullptr);
        void readNatives(const std::vector<std::vector<uint8_t>>& messages,
                         const std::function<void(int, int)>& out) const;

        // Message thread. Cores driven by plain CCs (OpenB3): point the slots at the
        // (cc, value) pairs of a preset the device just loaded. Clears the touched flags.
        void syncFromCcValues(const std::vector<std::pair<uint8_t, uint8_t>>& values);

        // Message thread. An edit made on a core's own panel (CA-72): the slot bound to the CC
        // takes the value and tells the host, nothing goes back to the device. Not touched.
        void setCcFromEditor(uint8_t cc, uint8_t value);

        static const char* coreFileName(SynthType type);

        // Message thread. The core's MIDI CC table as text. grid: several tab separated
        // columns sized to the list, for a native message box; otherwise one per line.
        juce::String getCcTableText(SynthType type, bool grid = false);

    private:
        static bool readNative(SynthType type, const std::vector<uint8_t>& m, const std::function<void(int, int)>& out);
        void emit(const synthLib::SMidiEvent& ev);
        void unmapCc(int slot);

        // links replace the map's bindings slot by slot; descriptions are never freed, as the
        // host may still be reading one from another thread
        std::array<SlotParameter::Binding, NumSlots> m_linkBindings{};
        std::deque<pluginLib::Description> m_linkDescs;
        std::array<int8_t, 128> m_ccToSlot{};
        std::array<bool, NumSlots> m_linked{};   // syncFromPatch's dump layouts address the default slots
        std::function<void(int, int)> m_boardSend;
        std::vector<synthLib::SMidiEvent>* m_capture = nullptr;
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
