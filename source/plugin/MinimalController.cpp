#include "MinimalController.h"
#include "jucePluginLib/processor.h"

namespace retromulator
{
    MinimalController::MinimalController(pluginLib::Processor& processor)
        : pluginLib::Controller(processor, {})   // empty string = no JSON descriptions
    {
    }

    bool MinimalController::parseSysexMessage(const pluginLib::SysEx& sysex,
                                              synthLib::MidiEventSource source)
    {
        // Forward SysEx directly to the device via the plugin
        synthLib::SMidiEvent ev(source);
        ev.sysex = sysex;
        ev.a = 0xf0;
        getProcessor().addMidiEvent(ev);
        return true;
    }
}
