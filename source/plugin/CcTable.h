#pragma once

#include "HeadlessProcessor.h"
#include "ParameterPool.h"
#include "SynthType.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace retromulator
{
    // The loaded core's MIDI CC table in a native message box. Copy puts the one per line
    // version on the clipboard.
    inline void showCcTable(HeadlessProcessor& proc, juce::Component* associated)
    {
        auto* pool = proc.getParameterPool();
        const auto type = proc.getSynthType();
        if(!pool || type == SynthType::None)
            return;

        const auto plain = pool->getCcTableText(type);
        const auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle(juce::String(synthTypeName(type)) + " MIDI CC Table")
            .withMessage(pool->getCcTableText(type, true))
            .withButton("Copy")
            .withButton("Close")
            .withAssociatedComponent(associated);

        juce::NativeMessageBox::showAsync(options, [plain](const int result)
        {
            if(result == 0)
                juce::SystemClipboard::copyTextToClipboard(plain);
        });
    }
}
