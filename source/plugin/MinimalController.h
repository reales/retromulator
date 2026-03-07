#pragma once

#include "jucePluginLib/controller.h"

namespace pluginLib { class Processor; }

namespace retromulator
{
    // Minimal controller: no parameter registration, no JSON descriptions.
    // Passes MIDI/SysEx through to the device. All CC handling is native
    // inside each synth's Device implementation.
    class MinimalController final : public pluginLib::Controller
    {
    public:
        explicit MinimalController(pluginLib::Processor& processor);

        // No parameter changes to send — device handles CC natively
        void sendParameterChange(const pluginLib::Parameter& /*parameter*/,
                                 pluginLib::ParamValue /*value*/,
                                 pluginLib::Parameter::Origin /*origin*/) override {}

        // Pass SysEx straight through to the device via the processor
        bool parseSysexMessage(const pluginLib::SysEx& sysex,
                               synthLib::MidiEventSource source) override;

        // Nothing to do after state load in headless mode
        void onStateLoaded() override {}

        uint8_t getPartCount() const override { return 16; }
    };
}
