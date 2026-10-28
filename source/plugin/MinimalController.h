#pragma once

#include "jucePluginLib/controller.h"

namespace pluginLib { class Processor; }

namespace retromulator
{
    // Minimal controller: no JSON-driven parameter registration. Host parameters
    // live in ParameterPool; this class only routes incoming CCs into it.
    class MinimalController final : public pluginLib::Controller
    {
    public:
        explicit MinimalController(pluginLib::Processor& processor);

        // Pool parameters send their own MIDI, nothing to do here
        void sendParameterChange(const pluginLib::Parameter& /*parameter*/,
                                 pluginLib::ParamValue /*value*/,
                                 pluginLib::Parameter::Origin /*origin*/) override {}

        // Incoming SysEx needs no editor handling
        bool parseSysexMessage(const pluginLib::SysEx& sysex,
                               synthLib::MidiEventSource source) override;

        // Incoming host/physical CCs update the matching pool slot
        bool parseControllerMessage(const synthLib::SMidiEvent& ev) override;

        // Nothing to do after state load in headless mode
        void onStateLoaded() override {}

        uint8_t getPartCount() const override { return 16; }
    };
}
