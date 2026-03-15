#pragma once

#include "HeadlessProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace retromulator
{
    class BasicEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
    {
    public:
        explicit BasicEditor(HeadlessProcessor& p);
        ~BasicEditor() override = default;

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        HeadlessProcessor& m_proc;

        juce::ComboBox m_synthCombo;
        juce::TextButton m_prevBtn  { "<" };
        juce::TextButton m_nextBtn  { ">" };
        juce::ComboBox m_progCombo;
        juce::ComboBox m_bankCombo;
        juce::Label    m_statusLabel;

        static constexpr int kImportId         = 9999;
        static constexpr int kExportPresetId   = 9998;
        static constexpr int kExportBankId     = 9997;
        static constexpr int kConvertToVirusB  = 9994;
        static constexpr int kConvertToVirusA  = 9993;

        juce::String m_lastSysexPath;
        juce::String m_lastBankFolder;
        SynthType    m_lastSynthType = SynthType::None;
        juce::String m_currentBankFolder;

        std::shared_ptr<juce::FileChooser> m_fileChooser;

        static bool isFolderMode(SynthType type, const juce::File& synthFolder);

        void updateStatus();
        void refreshBankCombo();
        void onLoadSysex();
        void onExportPreset();
        void onExportBank();
        void onConvertVirusBank(char targetVersion);
        void navigatePatch(int delta);
        void onSynthTypeChanged();
        void onFirmwareMissing(SynthType type);
        void applyLoadedRom(SynthType type, const juce::String& romFolder);

        void timerCallback() override { updateStatus(); }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BasicEditor)
    };
}
