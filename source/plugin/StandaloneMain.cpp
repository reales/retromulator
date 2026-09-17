// Standalone entry point. Everything here exists so that the same executable can
// render a MIDI file offline instead of opening its window; without a --render
// argument it hands straight over to the JUCE app JUCE would have started itself.

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone && !JUCE_IOS && !JUCE_ANDROID

#include "RenderCli.h"

#include <juce_events/juce_events.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <cstdio>
 #include <io.h>
 #include <fcntl.h>
#endif

extern juce::JUCEApplicationBase* juce_CreateApplication();

namespace
{
    int runStandalone(const int argc, char* argv[])
    {
        if(retromulator::RenderCli::wanted(argc, argv))
        {
           #if JUCE_WINDOWS
            // A GUI subsystem binary has no stdout of its own, so it borrows the
            // console it was launched from. Nothing to attach to when it was launched
            // from Explorer, and then the render still runs, just silently.
            if(AttachConsole(ATTACH_PARENT_PROCESS))
            {
                FILE* f = nullptr;
                freopen_s(&f, "CONOUT$", "w", stdout);
                freopen_s(&f, "CONOUT$", "w", stderr);
            }
           #endif
            return retromulator::RenderCli::run(argc, argv);
        }

        juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
        return juce::JUCEApplicationBase::main(argc, const_cast<const char**>(argv));
    }
}

#if JUCE_WINDOWS
int __stdcall WinMain(struct HINSTANCE__*, struct HINSTANCE__*, char*, int)
{
    return runStandalone(__argc, __argv);
}
#else
int main(int argc, char* argv[])
{
    return runStandalone(argc, argv);
}
#endif

#endif
