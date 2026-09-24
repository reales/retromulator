// Standalone app class: JUCE's own, plus the files the OS hands over. Double click,
// "Open With", a drop on the dock icon. Modules and .m3u go to the tracker, .mid to
// 88emu, and a batch of modules becomes a playlist.

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP && !JUCE_IOS && !JUCE_ANDROID

#include "HeadlessProcessor.h"

#ifdef CUSTOM
#include "../custom/RetroEditor.h"
#endif

#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace retromulator
{
    namespace
    {
        juce::StringArray g_pending;	// message thread only

        // Switches and anything that is not on disk are dropped.
        std::vector<std::string> existingPaths(const juce::StringArray& args)
        {
            std::vector<std::string> paths;
            for(const auto& a : args)
            {
                const auto arg = a.unquoted();
                if(arg.isEmpty() || arg.startsWith("-"))
                    continue;
                const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(arg);
                if(file.isDirectory() || file.existsAsFile())
                    paths.push_back(file.getFullPathName().toStdString());
            }
            return paths;
        }

        // The editor owns the synth switch and the combo, so the open goes through it.
        // Null while the window is still coming up.
        void flushPending()
        {
            if(g_pending.isEmpty())
                return;

           #ifdef CUSTOM
            auto* holder = juce::StandalonePluginHolder::getInstance();
            if(!holder || !holder->processor)
                return;
            auto* editor = dynamic_cast<RetroEditor*>(holder->processor->getActiveEditor());
            if(!editor)
                return;

            const auto files = g_pending;
            g_pending.clear();
            editor->openExternalFiles(files);
           #endif
        }

        // Windows starts one process per selected file, so a batch trickles in: wait for
        // it to settle. The same timer retries while the window is still coming up.
        struct FlushTimer final : juce::Timer
        {
            int tries = 0;
            void timerCallback() override
            {
                flushPending();
                if(g_pending.isEmpty() || ++tries > 40)
                {
                    g_pending.clear();
                    stopTimer();
                }
            }
        };
    }

    class StandaloneApp final : public juce::JUCEApplication
    {
    public:
        StandaloneApp()
        {
            juce::PropertiesFile::Options options;
            options.applicationName     = juce::CharPointer_UTF8(JucePlugin_Name);
            options.filenameSuffix      = ".settings";
            options.osxLibrarySubFolder = "Application Support";
           #if JUCE_LINUX || JUCE_BSD
            options.folderName          = "~/.config";
           #else
            options.folderName          = "";
           #endif
            m_appProperties.setStorageParameters(options);
        }

        const juce::String getApplicationName() override    { return juce::CharPointer_UTF8(JucePlugin_Name); }
        const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        // macOS: open events and dock drops. Windows: a later launch that had files.
        void anotherInstanceStarted(const juce::String& commandLine) override
        {
            openFiles(existingPaths(juce::StringArray::fromTokens(commandLine, true)));
            if(m_mainWindow)
                m_mainWindow->toFront(true);
        }

        void initialise(const juce::String& commandLine) override
        {
           #if JUCE_WINDOWS
            // Explorer starts one process per selected file, so a launch with files hands
            // them to the instance already running. Without files a second window is fine.
            bool otherRunning = false;
            {
                juce::InterProcessLock probe("juceAppLock_" + getApplicationName());
                otherRunning = !probe.enter(0);
            }
            const bool hasFiles = !existingPaths(getCommandLineParameterArray()).empty();
            // The first instance takes the lock here and becomes the listener.
            if((!otherRunning || hasFiles) && sendCommandLineToPreexistingInstance())
            {
                quit();
                return;
            }
           #endif

            if(juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            {
                jassertfalse;
                m_pluginHolder = createPluginHolder();
            }
            else
            {
                m_mainWindow = std::make_unique<juce::StandaloneFilterWindow>(
                    getApplicationName(),
                    juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
                    createPluginHolder());
                m_mainWindow->setVisible(true);
            }

            juce::ignoreUnused(commandLine);
            openFiles(existingPaths(getCommandLineParameterArray()));
        }

        void shutdown() override
        {
            m_flushTimer.stopTimer();
            g_pending.clear();
            m_pluginHolder = nullptr;
            m_mainWindow = nullptr;
            m_appProperties.saveIfNeeded();
        }

        void systemRequestedQuit() override
        {
            if(m_pluginHolder)
                m_pluginHolder->savePluginState();
            if(m_mainWindow)
                m_mainWindow->pluginHolder->savePluginState();

            if(juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
            {
                juce::Timer::callAfterDelay(100, []
                {
                    if(auto* app = juce::JUCEApplicationBase::getInstance())
                        app->systemRequestedQuit();
                });
            }
            else
            {
                quit();
            }
        }

    private:
        void openFiles(const std::vector<std::string>& paths)
        {
            for(const auto& p : paths)
                g_pending.add(juce::String(p));
            if(g_pending.isEmpty())
                return;
            m_flushTimer.tries = 0;
            m_flushTimer.startTimer(250);
        }

        std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
        {
           #ifdef JucePlugin_PreferredChannelConfigurations
            constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig(channels, juce::numElementsInArray(channels));
           #else
            const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
           #endif

            return std::make_unique<juce::StandalonePluginHolder>(
                m_appProperties.getUserSettings(), false, juce::String{}, nullptr, channelConfig, false);
        }

        FlushTimer m_flushTimer;
        juce::ApplicationProperties m_appProperties;
        std::unique_ptr<juce::StandaloneFilterWindow> m_mainWindow;
        std::unique_ptr<juce::StandalonePluginHolder> m_pluginHolder;
    };
}

JUCE_CREATE_APPLICATION_DEFINE(retromulator::StandaloneApp)

#endif
