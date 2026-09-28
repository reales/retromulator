#pragma once

#include "HeadlessProcessor.h"
#include "ParameterPool.h"
#include "SynthType.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace retromulator
{
    // A core's MIDI CC table as plain text, in its own window with a copy button.
    class CcTableWindow final : public juce::Component
    {
    public:
        explicit CcTableWindow(const juce::String& text) : m_text(text)
        {
            m_view.setMultiLine(true, false);
            m_view.setReadOnly(true);
            m_view.setScrollbarsShown(true);
            m_view.setCaretVisible(false);
            m_view.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
            m_view.setText(text, false);
            addAndMakeVisible(m_view);

            m_copy.onClick = [this]
            {
                juce::SystemClipboard::copyTextToClipboard(m_text);
                m_copy.setButtonText("Copied");
            };
            addAndMakeVisible(m_copy);

            m_close.onClick = [this]
            {
                if(auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState(0);
            };
            addAndMakeVisible(m_close);

            setSize(420, 520);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced(8);
            auto buttons = r.removeFromBottom(28);
            r.removeFromBottom(8);
            m_view.setBounds(r);
            m_close.setBounds(buttons.removeFromRight(90));
            buttons.removeFromRight(8);
            m_copy.setBounds(buttons.removeFromRight(90));
        }

        static void show(const juce::String& title, const juce::String& text, juce::Component* centreAround)
        {
            juce::DialogWindow::LaunchOptions o;
            o.content.setOwned(new CcTableWindow(text));
            o.dialogTitle = title;
            o.componentToCentreAround = centreAround;
            o.useNativeTitleBar = true;
            o.resizable = true;
            o.escapeKeyTriggersCloseButton = true;
            o.dialogBackgroundColour = juce::LookAndFeel::getDefaultLookAndFeel()
                                           .findColour(juce::ResizableWindow::backgroundColourId);
            // a plugin window floats above the host, so this one has to as well
            if(auto* dw = o.launchAsync())
                dw->setAlwaysOnTop(true);
        }

        // The loaded core's table, or every core's when none is loaded.
        static void showFor(HeadlessProcessor& proc, juce::Component* centreAround)
        {
            auto* pool = proc.getParameterPool();
            if(!pool)
                return;

            const auto type = proc.getSynthType();
            if(type != SynthType::None)
            {
                show(juce::String(synthTypeName(type)) + " MIDI CC Table", pool->getCcTableText(type), centreAround);
                return;
            }

            juce::String all;
            int count = 0;
            const auto* order = synthTypeDisplayOrder(count);
            for(int i = 0; i < count; ++i)
                all << (i ? "\n" : "") << "== " << synthTypeName(order[i]) << " ==\n" << pool->getCcTableText(order[i]);
            show("MIDI CC Tables", all, centreAround);
        }

    private:
        juce::String m_text;
        juce::TextEditor m_view;
        juce::TextButton m_copy{"Copy"};
        juce::TextButton m_close{"Close"};
    };
}
