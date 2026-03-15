#include "BasicEditor.h"
#include "synthLib/midiToSysex.h"
#include <climits>
#include <fstream>

namespace retromulator
{
    static constexpr const char* kSoundFilePattern = "*.sfz;*.sf2;*.wav;*.aif;*.aiff;*.flac;*.ogg;*.zbp;*.zbb";

    static bool isAkaiSampler(SynthType type) { return type == SynthType::AkaiS1000; }

    static void findSysexFiles(const juce::File& folder, juce::Array<juce::File>& out, bool recursive = false)
    {
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.syx");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.mid");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.bin");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.pfm");
    }

    static void findSoundFiles(const juce::File& folder, juce::Array<juce::File>& out, bool recursive = false)
    {
        folder.findChildFiles(out, juce::File::findFiles, recursive, kSoundFilePattern);
    }

    static juce::String normalisePath(const std::string& p)
    {
        if(p.empty()) return {};
        return juce::File(p).getFullPathName();
    }

    static bool isFolderModeBasic(SynthType type, const juce::File& synthFolder)
    {
        if(type != SynthType::MicroQ && type != SynthType::XT)
            return false;
        juce::Array<juce::File> subs;
        synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
        return !subs.isEmpty();
    }

    bool BasicEditor::isFolderMode(SynthType type, const juce::File& synthFolder)
    {
        return isFolderModeBasic(type, synthFolder);
    }

    BasicEditor::BasicEditor(HeadlessProcessor& p)
        : AudioProcessorEditor(p), m_proc(p)
    {
        setSize(640, 80);
        setResizable(false, false);

        // ── Synth combo ──────────────────────────────────────────────────────
        m_synthCombo.addItem("None", 1);
        int displayCount = 0;
        const auto* displayOrder = synthTypeDisplayOrder(displayCount);
        for(int i = 0; i < displayCount; ++i)
            m_synthCombo.addItem(synthTypeName(displayOrder[i]),
                                 static_cast<int>(displayOrder[i]) + 2);
        m_synthCombo.setSelectedId(static_cast<int>(m_proc.getSynthType()) + 2,
                                   juce::dontSendNotification);
        m_synthCombo.onChange = [this] { onSynthTypeChanged(); };
        addAndMakeVisible(m_synthCombo);

        // ── Prev / Next buttons ──────────────────────────────────────────────
        // Shift+click jumps to the previous/next bank folder
        m_prevBtn.onClick = [this]
        {
            if(juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                navigateBankFolder(-1);
            else
                navigatePatch(-1);
        };
        m_nextBtn.onClick = [this]
        {
            if(juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                navigateBankFolder(+1);
            else
                navigatePatch(+1);
        };
        addAndMakeVisible(m_prevBtn);
        addAndMakeVisible(m_nextBtn);

        // ── Program combo ────────────────────────────────────────────────────
        m_progCombo.onChange = [this]
        {
            const int sel = m_progCombo.getSelectedId() - 1;
            if(sel < 0) return;

            const auto type = m_proc.getSynthType();
            const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

            if(isFolderMode(type, synthFolder) && !m_currentBankFolder.isEmpty())
            {
                juce::Array<juce::File> patches;
                juce::File(m_currentBankFolder).findChildFiles(
                    patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
                patches.sort();

                if(sel < patches.size())
                {
                    m_proc.loadPresetFromFile(
                        patches[sel].getFullPathName().toStdString(),
                        patches[sel].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                }
            }
            else if(sel != m_proc.getCurrentProgram())
            {
                if(isAkaiSampler(type))
                    m_proc.selectSoundPreset(sel);
                else
                    m_proc.selectProgram(sel);
                updateStatus();
            }
        };
        addAndMakeVisible(m_progCombo);

        // ── Bank combo ───────────────────────────────────────────────────────
        m_bankCombo.onChange = [this]
        {
            const int id = m_bankCombo.getSelectedId();
            if(id == kImportId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onLoadSysex();
                return;
            }
            if(id == kExportPresetId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onExportPreset();
                return;
            }
            if(id == kExportBankId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onExportBank();
                return;
            }
            if(id == kConvertToVirusB)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onConvertVirusBank('B');
                return;
            }
            if(id == kConvertToVirusA)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onConvertVirusBank('A');
                return;
            }
            if(id <= 0) return;

            const int idx = id - 1;
            const auto type = m_proc.getSynthType();
            const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

            if(isFolderMode(type, synthFolder))
            {
                juce::Array<juce::File> subs;
                synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
                subs.sort();

                if(idx < subs.size())
                {
                    m_currentBankFolder = subs[idx].getFullPathName();

                    juce::Array<juce::File> patches;
                    subs[idx].findChildFiles(patches, juce::File::findFiles, false,
                                             "*.syx;*.mid;*.bin;*.pfm");
                    patches.sort();

                    if(!patches.isEmpty())
                        m_proc.loadPresetFromFile(
                            patches[0].getFullPathName().toStdString(),
                            patches[0].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                }
            }
            else
            {
                juce::Array<juce::File> files;
                if(isAkaiSampler(type))
                    findSoundFiles(synthFolder, files);
                else
                    synthFolder.findChildFiles(files, juce::File::findFiles, false,
                                               "*.syx;*.mid;*.bin;*.pfm");
                files.sort();

                if(idx < files.size())
                {
                    if(isAkaiSampler(type))
                    {
                        m_proc.loadSoundFile(files[idx].getFullPathName().toStdString());
                    }
                    else
                    {
                        m_proc.loadPresetFromFile(files[idx].getFullPathName().toStdString(),
                                                  files[idx].getFileNameWithoutExtension().toStdString());
                    }
                    updateStatus();
                }
            }
        };
        addAndMakeVisible(m_bankCombo);

        // ── Status label ─────────────────────────────────────────────────────
        m_statusLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(m_statusLabel);

        updateStatus();
        startTimerHz(2);
    }

    void BasicEditor::paint(juce::Graphics& g)
    {
        g.fillAll(juce::Colour(0xff222222));
        g.setColour(juce::Colours::white);
        g.setFont(14.f);
        g.drawText("Retromulator", 29, 0, 120, getHeight(),
                   juce::Justification::centredLeft, false);
    }

    void BasicEditor::resized()
    {
        const int h   = getHeight();
        const int ctH = 28;
        const int y   = (h - ctH) / 2;
        const int pad = 4;

        const bool isOpenWurli = (m_proc.getSynthType() == SynthType::OpenWurli);

        int x = 130;
        m_synthCombo.setBounds(x, y, 110, ctH); x += 110 + pad;
        m_prevBtn.setEnabled(!isOpenWurli);
        m_prevBtn   .setBounds(x, y,  24, ctH); x +=  24 + 2;
        m_nextBtn.setEnabled(!isOpenWurli);
        m_nextBtn   .setBounds(x, y,  24, ctH); x +=  24 + pad;
        m_progCombo.setEnabled(!isOpenWurli);
        m_progCombo .setBounds(x, y, 160, ctH); x += 160 + pad;
        m_bankCombo.setEnabled(!isOpenWurli);
        m_bankCombo .setBounds(x, y, 160, ctH); x += 160 + pad;
        m_statusLabel.setBounds(x, y, getWidth() - x - 4, ctH);
    }

    void BasicEditor::refreshBankCombo()
    {
        const auto type = m_proc.getSynthType();

        if(type == SynthType::None)
        {
            m_bankCombo.clear(juce::dontSendNotification);
            return;
        }

        // OpenWurli is a single physical model — no banks/presets to import.
        if(type == SynthType::OpenWurli)
        {
            if(m_bankCombo.getNumItems() == 0 || m_bankCombo.getText() != "by Joshua Price")
            {
                m_bankCombo.clear(juce::dontSendNotification);
                m_bankCombo.setText("by Joshua Price", juce::dontSendNotification);
            }
            return;
        }

        const juce::File folder(HeadlessProcessor::getSynthDataFolder(type));

        if(isFolderMode(type, folder))
        {
            juce::Array<juce::File> subs;
            folder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();

            const int subCount = subs.size();
            const int folderExtra = (type == SynthType::VirusABC) ? 5 : 3;
            bool needsRebuild = (m_bankCombo.getNumItems() != subCount + folderExtra);
            if(!needsRebuild)
                for(int i = 0; i < subCount; ++i)
                    if(m_bankCombo.getItemText(i) != subs[i].getFileName())
                        { needsRebuild = true; break; }

            if(needsRebuild)
            {
                m_bankCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < subCount; ++i)
                    m_bankCombo.addItem(subs[i].getFileName(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem("[ + Import... ]", kImportId);
                m_bankCombo.addItem("[ Export Preset... ]", kExportPresetId);
                m_bankCombo.addItem("[ Export Bank... ]",   kExportBankId);
                if(type == SynthType::VirusABC)
                {
                    m_bankCombo.addItem("[ Convert to Virus B... ]", kConvertToVirusB);
                    m_bankCombo.addItem("[ Convert to Virus A... ]", kConvertToVirusA);
                }
            }

            int selId = 0;
            for(int i = 0; i < subs.size(); ++i)
                if(subs[i].getFullPathName() == m_currentBankFolder)
                    { selId = i + 1; break; }
            m_bankCombo.setSelectedId(selId, juce::dontSendNotification);
        }
        else
        {
            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));

            juce::Array<juce::File> files;
            if(isAkaiSampler(type))
                findSoundFiles(folder, files);
            else
                findSysexFiles(folder, files);
            files.sort();

            const bool akai = isAkaiSampler(type);
            const bool virusABC = (type == SynthType::VirusABC);
            // Import + ExportPreset + ExportBank = 3; +ConvertB + ConvertA = 5 for Virus ABC
            // (separators don't count in getNumItems)
            const int extraItems = akai ? 1 : (virusABC ? 5 : 3);
            const int fileCount = files.size();
            bool needsRebuild = (m_bankCombo.getNumItems() != fileCount + extraItems);
            if(!needsRebuild)
                for(int i = 0; i < fileCount; ++i)
                    if(m_bankCombo.getItemText(i) != files[i].getFileNameWithoutExtension())
                        { needsRebuild = true; break; }

            if(needsRebuild)
            {
                m_bankCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < fileCount; ++i)
                    m_bankCombo.addItem(files[i].getFileNameWithoutExtension(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem(akai ? "[ + Load... ]" : "[ + Import... ]", kImportId);
                if(!akai)
                {
                    m_bankCombo.addItem("[ Export Preset... ]", kExportPresetId);
                    m_bankCombo.addItem("[ Export Bank... ]",   kExportBankId);
                    if(virusABC)
                    {
                        m_bankCombo.addItem("[ Convert to Virus B... ]", kConvertToVirusB);
                        m_bankCombo.addItem("[ Convert to Virus A... ]", kConvertToVirusA);
                    }
                }
            }

            int selId = 0;
            for(int i = 0; i < files.size(); ++i)
                if(files[i].getFullPathName() == currentPath)
                    { selId = i + 1; break; }

            // For Akai, if the loaded file is outside the data folder, show its
            // name in the combo even though it isn't in the folder listing.
            if(selId == 0 && akai && !currentPath.isEmpty())
            {
                const juce::String extName = juce::File(currentPath).getFileNameWithoutExtension();
                constexpr int kExternalFileId = 9000;
                m_bankCombo.clear(juce::dontSendNotification);
                m_bankCombo.addItem(extName, kExternalFileId);
                m_bankCombo.addSeparator();
                for(int i = 0; i < fileCount; ++i)
                    m_bankCombo.addItem(files[i].getFileNameWithoutExtension(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem("[ + Load... ]", kImportId);
                selId = kExternalFileId;
            }

            m_bankCombo.setSelectedId(selId, juce::dontSendNotification);
        }
    }

    void BasicEditor::updateStatus()
    {
        if(m_proc.consumeEditorSizeDirty())
        {
            const int w = m_proc.getSavedEditorWidth();
            const int h = m_proc.getSavedEditorHeight();
            if(w > 0 && h > 0)
                setSize(w, h);
        }

        if(m_proc.isFirmwareMissing() || !m_proc.getPlugin().isValid())
        {
            m_statusLabel.setText("No firmware", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else if(m_proc.hasDeviceError())
        {
            m_statusLabel.setText("Device error", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        }
        else
        {
            m_statusLabel.setText("Ready", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        }

        const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));
        const bool bankChanged = (currentPath != m_lastSysexPath);
        m_lastSysexPath = currentPath;

        const auto type = m_proc.getSynthType();
        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

        if(isAkaiSampler(type))
        {
            m_bankCombo.setTextWhenNothingSelected("No Bank");
            m_progCombo.setTextWhenNothingSelected("No Preset");
        }
        else
        {
            m_bankCombo.setTextWhenNothingSelected({});
            m_progCombo.setTextWhenNothingSelected({});
        }

        if(isFolderMode(type, synthFolder) && bankChanged && !currentPath.isEmpty())
        {
            const juce::File parent = juce::File(currentPath).getParentDirectory();
            if(parent.getParentDirectory().getFullPathName() == synthFolder.getFullPathName())
                m_currentBankFolder = parent.getFullPathName();
        }

        const bool folderChanged = (m_currentBankFolder != m_lastBankFolder);
        m_lastBankFolder = m_currentBankFolder;

        if(isFolderMode(type, synthFolder) && !m_currentBankFolder.isEmpty())
        {
            juce::Array<juce::File> patches;
            juce::File(m_currentBankFolder).findChildFiles(
                patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
            patches.sort();

            const int patchCount = patches.size();
            if(bankChanged || folderChanged || m_progCombo.getNumItems() != patchCount)
            {
                m_progCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < patchCount; ++i)
                {
                    const juce::String num = juce::String(i + 1).paddedLeft('0', 3);
                    m_progCombo.addItem(num + "  " + patches[i].getFileNameWithoutExtension(), i + 1);
                }
            }

            int selId = 0;
            for(int i = 0; i < patches.size(); ++i)
                if(patches[i].getFullPathName() == currentPath)
                    { selId = i + 1; break; }
            m_progCombo.setSelectedId(0,     juce::dontSendNotification);
            m_progCombo.setSelectedId(selId, juce::dontSendNotification);
        }
        else
        {
            const int progCount = m_proc.getProgramCount();
            const int progIdx   = m_proc.getCurrentProgram();
            const auto& names   = m_proc.getProgramNames();

            if(bankChanged || m_progCombo.getNumItems() != progCount)
            {
                m_progCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < progCount; ++i)
                {
                    const juce::String num = juce::String(i + 1).paddedLeft('0', 3);
                    const juce::String name = (i < static_cast<int>(names.size()) && !names[static_cast<size_t>(i)].empty())
                        ? (num + "  " + juce::String(names[static_cast<size_t>(i)]))
                        : num;
                    m_progCombo.addItem(name, i + 1);
                }
            }

            // Update current item text (name may have been set after initial load via sendBankMessage).
            // Skip for Akai — program names are already set correctly by loadSoundFile.
            if(progCount > 0 && !isAkaiSampler(type))
            {
                const juce::String patch(m_proc.getPatchName());
                const juce::String bankStr = currentPath.isEmpty()
                    ? juce::String{}
                    : juce::File(currentPath).getFileNameWithoutExtension();
                const bool hasPatchName = !patch.isEmpty() && patch != bankStr;
                const juce::String num = juce::String(progIdx + 1).paddedLeft('0', 3);
                m_progCombo.changeItemText(progIdx + 1, hasPatchName ? (num + "  " + patch) : num);
            }

            m_progCombo.setSelectedId(0,           juce::dontSendNotification);
            if(progCount > 0)
                m_progCombo.setSelectedId(progIdx + 1, juce::dontSendNotification);
        }

        const bool synthChanged = (type != m_lastSynthType);
        m_lastSynthType = type;
        if(synthChanged)
            m_bankCombo.clear(juce::dontSendNotification);

        refreshBankCombo();
    }

    void BasicEditor::onLoadSysex()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));
        synthFolder.createDirectory();

        const auto lastFolder = HeadlessProcessor::getLastLoadFolder(type);
        const auto destFolder = lastFolder.empty() ? synthFolder : juce::File(lastFolder);

        const juce::String filter = isAkaiSampler(type)
            ? juce::String(kSoundFilePattern)
            : juce::String("*.syx;*.mid;*.bin;*.pfm");

        const juce::String title = isAkaiSampler(type)
            ? "Select SFZ, SF2, ZBP, ZBB, WAV, AIF, FLAC or OGG sound file"
            : "Select SysEx patch file";

        m_fileChooser = std::make_shared<juce::FileChooser>(title, destFolder, filter);

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, type](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(!chosen.existsAsFile()) return;
                HeadlessProcessor::setLastLoadFolder(type, chosen.getParentDirectory().getFullPathName().toStdString());

                if(isAkaiSampler(type))
                {
                    m_proc.loadSoundFile(chosen.getFullPathName().toStdString());
                    updateStatus();
                    return;
                }

                // ── Virus ABC / TI cross-detection ──────────────────────────
                if(type == SynthType::VirusABC || type == SynthType::VirusTI)
                {
                    std::ifstream f(chosen.getFullPathName().toStdString(), std::ios::binary);
                    if(f.is_open())
                    {
                        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                                  std::istreambuf_iterator<char>());
                        synthLib::SysexBufferList msgs;
                        const synthLib::SysexBuffer buf(raw.begin(), raw.end());
                        synthLib::MidiToSysex::extractSysexFromData(msgs, buf);

                        const auto detected = HeadlessProcessor::detectVirusType(msgs);

                        if(detected != SynthType::None && detected != type)
                        {
                            const juce::String detectedName(synthTypeName(detected));
                            const juce::String currentName(synthTypeName(type));

                            juce::NativeMessageBox::showMessageBoxAsync(
                                juce::MessageBoxIconType::WarningIcon,
                                "Incompatible Virus Patch",
                                "This file contains " + detectedName + " patches "
                                "and is not compatible with " + currentName + ".\n\n"
                                "The file has been placed in the " + detectedName + " folder.");

                            HeadlessProcessor::copySysexToFolder(
                                chosen.getFullPathName().toStdString(), detected);
                            return;
                        }
                    }
                }

                m_proc.loadPresetFromFile(chosen.getFullPathName().toStdString(),
                                          chosen.getFileNameWithoutExtension().toStdString());
                updateStatus();
            });
    }

    void BasicEditor::onExportPreset()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None || isAkaiSampler(type) || m_proc.getProgramCount() == 0) return;

        const juce::String patchName(m_proc.getPatchName());
        const juce::String defaultName = patchName.isEmpty()
            ? juce::String("preset_") + juce::String(m_proc.getCurrentProgram() + 1)
            : patchName.trim().replaceCharacters("/\\:*?\"<>|", "_________");

        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            "Export Preset as SysEx",
            destFolder.getChildFile(defaultName + ".syx"),
            "*.syx");

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                if(!m_proc.exportCurrentPresetToFile(path.toStdString()))
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Export Failed")
                            .withMessage("Could not write preset to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::onExportBank()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None || isAkaiSampler(type) || m_proc.getProgramCount() == 0) return;

        const juce::String currentPath(m_proc.getSysexFilePath());
        const juce::String defaultName = currentPath.isEmpty()
            ? "bank"
            : juce::File(currentPath).getFileNameWithoutExtension();

        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            "Export Bank as SysEx",
            destFolder.getChildFile(defaultName + "_named.syx"),
            "*.syx");

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                if(!m_proc.exportCurrentBankToFile(path.toStdString()))
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Export Failed")
                            .withMessage("Could not write bank to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::onConvertVirusBank(char targetVersion)
    {
        const auto type = m_proc.getSynthType();
        if(type != SynthType::VirusABC || m_proc.getProgramCount() == 0) return;

        const juce::String currentPath(m_proc.getSysexFilePath());
        const juce::String defaultName = currentPath.isEmpty()
            ? "bank"
            : juce::File(currentPath).getFileNameWithoutExtension();

        const juce::String suffix = juce::String("_Virus") + juce::String::charToString(targetVersion);
        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            juce::String("Convert Bank to Virus ") + juce::String::charToString(targetVersion),
            destFolder.getChildFile(defaultName + suffix + ".syx"),
            "*.syx");

        const char tv = targetVersion;
        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, tv](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                const int result = m_proc.exportConvertedVirusBank(path.toStdString(), tv);
                if(result < 0)
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Conversion Failed")
                            .withMessage("Could not convert bank to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
                else
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle("Conversion Complete")
                            .withMessage(juce::String(result) + " patch(es) converted to Virus "
                                         + juce::String::charToString(tv) + " format.\n\nSaved to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::navigatePatch(int delta)
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

        if(isFolderMode(type, synthFolder))
        {
            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));

            if(!m_currentBankFolder.isEmpty())
            {
                juce::Array<juce::File> patches;
                juce::File(m_currentBankFolder).findChildFiles(
                    patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
                patches.sort();

                int cur = -1;
                for(int i = 0; i < patches.size(); ++i)
                    if(patches[i].getFullPathName() == currentPath)
                        { cur = i; break; }

                const int next = cur + delta;
                if(next >= 0 && next < patches.size())
                {
                    m_proc.loadPresetFromFile(patches[next].getFullPathName().toStdString(),
                                              patches[next].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                    return;
                }
            }

            juce::Array<juce::File> subs;
            synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();
            if(subs.isEmpty()) return;

            int subCur = -1;
            for(int i = 0; i < subs.size(); ++i)
                if(subs[i].getFullPathName() == m_currentBankFolder)
                    { subCur = i; break; }

            const int nextSub = (subCur + delta + subs.size()) % subs.size();
            m_currentBankFolder = subs[nextSub].getFullPathName();

            juce::Array<juce::File> patches;
            findSysexFiles(subs[nextSub], patches);
            patches.sort();

            if(!patches.isEmpty())
            {
                const int patchIdx = (delta < 0) ? patches.size() - 1 : 0;
                m_proc.loadPresetFromFile(patches[patchIdx].getFullPathName().toStdString(),
                                          patches[patchIdx].getFileNameWithoutExtension().toStdString());
            }
            updateStatus();
        }
        else
        {
            const int count = m_proc.getProgramCount();
            const int cur   = m_proc.getCurrentProgram();

            if(count > 1)
            {
                const int next = cur + delta;
                if(next >= 0 && next < count)
                {
                    if(isAkaiSampler(type))
                        m_proc.selectSoundPreset(next);
                    else
                        m_proc.selectProgram(next);
                    updateStatus();
                    return;
                }
            }

            const juce::File synthFolder2(HeadlessProcessor::getSynthDataFolder(type));
            juce::Array<juce::File> files;
            if(isAkaiSampler(type))
                findSoundFiles(synthFolder2, files);
            else
                findSysexFiles(synthFolder2, files);
            files.sort();
            if(files.isEmpty()) return;

            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));
            int fileCur = -1;
            for(int i = 0; i < files.size(); ++i)
                if(files[i].getFullPathName() == currentPath)
                    { fileCur = i; break; }

            const int next = (fileCur + delta + files.size()) % files.size();
            if(isAkaiSampler(type))
            {
                m_proc.loadSoundFile(files[next].getFullPathName().toStdString());
            }
            else
            {
                m_proc.loadPresetFromFile(files[next].getFullPathName().toStdString(),
                                          files[next].getFileNameWithoutExtension().toStdString(),
                                          delta < 0 ? INT_MAX : 0);
            }
            updateStatus();
        }
    }

    // ── Shift+< / Shift+> : jump to previous/next bank folder ─────────────
    void BasicEditor::navigateBankFolder(int delta)
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));
        if(!isFolderMode(type, synthFolder)) return;

        juce::Array<juce::File> subs;
        synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
        subs.sort();
        if(subs.isEmpty()) return;

        int subCur = -1;
        for(int i = 0; i < subs.size(); ++i)
            if(subs[i].getFullPathName() == m_currentBankFolder)
                { subCur = i; break; }

        const int nextSub = (subCur + delta + subs.size()) % subs.size();
        m_currentBankFolder = subs[nextSub].getFullPathName();

        juce::Array<juce::File> patches;
        findSysexFiles(subs[nextSub], patches);
        patches.sort();

        if(!patches.isEmpty())
        {
            m_proc.loadPresetFromFile(patches[0].getFullPathName().toStdString(),
                                      patches[0].getFileNameWithoutExtension().toStdString());
        }
        updateStatus();
    }

    void BasicEditor::applyLoadedRom(SynthType type, const juce::String& romFolder)
    {
        m_proc.setSynthType(type, romFolder.toStdString());

        if(m_proc.isFirmwareMissing())
        {
            m_proc.setSynthType(SynthType::None);
            m_synthCombo.setSelectedId(1, juce::dontSendNotification);
            updateStatus();

            juce::NativeMessageBox::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Invalid Firmware")
                    .withMessage("The selected file is not a valid firmware for " +
                                 juce::String(synthTypeName(type)) + ".")
                    .withButton("OK"),
                nullptr);
            return;
        }

        m_synthCombo.setSelectedId(static_cast<int>(type) + 2, juce::dontSendNotification);
        m_currentBankFolder.clear();

        const juce::File folder(HeadlessProcessor::getSynthDataFolder(type));
        if(isFolderMode(type, folder))
        {
            juce::Array<juce::File> subs;
            folder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();
            if(!subs.isEmpty())
            {
                m_currentBankFolder = subs[0].getFullPathName();
                juce::Array<juce::File> patches;
                findSysexFiles(subs[0], patches);
                patches.sort();
                if(!patches.isEmpty())
                    m_proc.loadPresetFromFile(patches[0].getFullPathName().toStdString(),
                                              patches[0].getFileNameWithoutExtension().toStdString());
            }
        }
        else
        {
            juce::Array<juce::File> files;
            if(isAkaiSampler(type))
                findSoundFiles(folder, files);
            else
                findSysexFiles(folder, files);
            files.sort();
            if(!files.isEmpty())
            {
                if(isAkaiSampler(type))
                    m_proc.loadSoundFile(files[0].getFullPathName().toStdString());
                else
                    m_proc.loadPresetFromFile(files[0].getFullPathName().toStdString(),
                                              files[0].getFileNameWithoutExtension().toStdString(), 0);
            }
        }

        updateStatus();
    }

    void BasicEditor::onFirmwareMissing(SynthType type)
    {
        m_proc.setSynthType(SynthType::None);
        m_synthCombo.setSelectedId(1, juce::dontSendNotification);
        updateStatus();

        const juce::String romFolder(HeadlessProcessor::getDataFolder() + "ROM/");
        juce::File(romFolder).createDirectory();

        m_fileChooser = std::make_shared<juce::FileChooser>(
            "Select firmware file for " + juce::String(synthTypeName(type)),
            juce::File(romFolder),
            "*.bin;*.mid;*.rom");

        juce::Component::SafePointer<BasicEditor> safe(this);
        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safe, type, romFolder](const juce::FileChooser& fc)
            {
                if(!safe) return;
                const auto chosen = fc.getResult();
                if(!chosen.existsAsFile()) return;

                const juce::File dest = juce::File(romFolder).getChildFile(chosen.getFileName());
                chosen.copyFileTo(dest);

                safe->applyLoadedRom(type, romFolder);

                if(safe && safe->m_proc.isFirmwareMissing())
                    dest.deleteFile();
            });
    }

    void BasicEditor::onSynthTypeChanged()
    {
        const auto newType = static_cast<SynthType>(m_synthCombo.getSelectedId() - 2);

        m_currentBankFolder.clear();
        m_proc.setSynthType(newType, {});

        if(newType == SynthType::None)
        {
            updateStatus();
            return;
        }

        if(m_proc.isFirmwareMissing())
        {
            onFirmwareMissing(newType);
            return;
        }

        m_synthCombo.setSelectedId(static_cast<int>(m_proc.getSynthType()) + 2,
                                   juce::dontSendNotification);

        const juce::File folder(HeadlessProcessor::getSynthDataFolder(newType));
        if(isFolderMode(newType, folder))
        {
            juce::Array<juce::File> subs;
            folder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();
            if(!subs.isEmpty())
            {
                m_currentBankFolder = subs[0].getFullPathName();
                juce::Array<juce::File> patches;
                findSysexFiles(subs[0], patches);
                patches.sort();
                if(!patches.isEmpty())
                    m_proc.loadPresetFromFile(patches[0].getFullPathName().toStdString(),
                                              patches[0].getFileNameWithoutExtension().toStdString());
            }
        }
        else
        {
            juce::Array<juce::File> files;
            if(isAkaiSampler(newType))
                findSoundFiles(folder, files);
            else
                findSysexFiles(folder, files);
            files.sort();
            if(!files.isEmpty())
            {
                if(isAkaiSampler(newType))
                    m_proc.loadSoundFile(files[0].getFullPathName().toStdString());
                else
                    m_proc.loadPresetFromFile(files[0].getFullPathName().toStdString(),
                                              files[0].getFileNameWithoutExtension().toStdString(), 0);
            }
        }

        updateStatus();
    }
}
