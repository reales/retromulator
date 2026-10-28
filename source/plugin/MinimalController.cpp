#include "MinimalController.h"
#include "HeadlessProcessor.h"
#include "ParameterPool.h"
#include "jucePluginLib/processor.h"

namespace retromulator
{
    MinimalController::MinimalController(pluginLib::Processor& processor)
        : pluginLib::Controller(processor, {})   // empty string = no JSON descriptions
    {
    }

    bool MinimalController::parseSysexMessage(const pluginLib::SysEx& /*sysex*/,
                                              synthLib::MidiEventSource /*source*/)
    {
        // Host and physical sysex reach the device through the routing matrix already.
        // Sending it on from here handed it back to the editor, which looped it forever.
        return true;
    }

    bool MinimalController::parseControllerMessage(const synthLib::SMidiEvent& ev)
    {
        auto* pool = static_cast<HeadlessProcessor&>(getProcessor()).getParameterPool();
        return pool && pool->handleIncomingCC(ev);
    }
}
