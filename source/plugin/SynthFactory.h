#pragma once

#include <string>
#include "SynthType.h"
#include "synthLib/device.h"

namespace retromulator
{
    class SynthFactory
    {
    public:
        // Create a device for the given synth type.
        // romPath may be a file path or directory; each synth's RomLoader
        // will search the path if needed.
        static synthLib::Device* create(SynthType type, const std::string& romPath = {});
    };
}
