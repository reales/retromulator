#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../synth.h"

// The panel after upstream's ca72-panel (art.rs, controls.rs): its drawing is 3108 units wide,
// the wood 130 high, the panel 795; our strip below holds the extra controls.
namespace ca72ui
{
// Cut at the OUTPUT section's right edge (upstream's QUALITY column moved to the strip).
inline constexpr float W = 2960.0f;
// Upstream has 130 units of wood and the name plate above the panel; a thinner board here sets
// the instrument apart from the rack above it.
inline constexpr float TOP = 40.0f;
// The panel 50 units taller than upstream's 795, so the section titles clear the controls.
inline constexpr float PH = 845.0f;
inline constexpr float PANEL_H = TOP + PH;
// A wood rail between the panel and the strip (the standalone's preset bar).
inline constexpr float BAR_H = 24.0f;
inline constexpr float STRIP_Y = PANEL_H + BAR_H;
inline constexpr float STRIP_H = 520.0f;
inline constexpr float H = STRIP_Y + STRIP_H;

enum class Dial { Ten, Tune, Cutoff, Freq, Time, ModMix, Detune, Level, Percent, Drive };
enum class Legends { Ranges, Waves12, Waves3, Quality };
enum class RockerColour { Blue, Red, Ivory };

struct Look;

class Knob : public juce::Slider
{
public:
    Knob (bool bigKnob, bool selector);
    // With Shift held, values snap through this (fixed steps: semitones, cents).
    double snapValue (double attempted, juce::Slider::DragMode) override;
    std::function<double (double)> snap;
    bool big, isSelector;
};

class Rocker : public juce::ToggleButton
{
public:
    Rocker (RockerColour c, bool vertical, float padShare);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool hitTest (int x, int y) override;
    RockerColour colour;
    bool vertical;
    float padShare; // the glow's margin, as a share of the component's width
    juce::Rectangle<float> body() const;
};

// A bat-lever toggle stepping through a choice parameter (NOTE PRIORITY).
class Toggle : public juce::Component
{
public:
    Toggle (juce::RangedAudioParameter& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getIndex() const { return index; }

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    int index = 0;
};

// PITCH (springs back to the centre) or MODULATION.
class Wheel : public juce::Component
{
public:
    Wheel (juce::RangedAudioParameter& p, bool detent);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    bool detent;
    float value = 0.0f, startValue = 0.0f;
};
// The CA-72 front panel, drawn at any width with the panel's proportions (H / W).
// Owned by the live panels, so its GPU image is freed with the editor and not at DLL unload.
struct PanelTexture
{
    PanelTexture();
    juce::Image image;
};

class Panel : public juce::Component, private juce::Timer
{
public:
    explicit Panel (std::shared_ptr<ca72Lib::Synth>);
    ~Panel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    const ca72Lib::Synth* getSynth() const { return synth.get(); }

    // Called on the timer after panel edits, so the host parameters can follow.
    std::function<void()> onEdited;

private:
    struct Placed
    {
        juce::Component* comp;
        float x, y, w, h; // centre and size in drawing units
        float pad = 0.0f;  // room around the control for its glow
    };

    void timerCallback() override;
    void addKnob (const juce::String& pid, float x, float y, Dial dial, bool big = false);
    void addSelector (const juce::String& pid, float x, float y, Legends legends);
    void addRocker (const juce::String& pid, float x, float y, float w, float h, RockerColour c, bool vertical = false);
    void drawPanel (juce::Graphics&) const;
    void rebuildBackground();

    juce::SharedResourcePointer<PanelTexture> texture;
    std::shared_ptr<ca72Lib::Synth> synth;
    juce::AudioProcessorValueTreeState& apvts;
    std::unique_ptr<Look> look;
    juce::OwnedArray<juce::Component> controls;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    std::vector<Placed> placed;
    std::vector<std::tuple<float, float, Dial, bool>> dials;
    std::vector<std::tuple<float, float, Legends>> selectors;

    struct OnLabel
    {
        Rocker* rocker;
        float x, y, size; // x: the right edge of an ON, else the legend's centre
        juce::String text = "ON";
        bool alignRight = true;
        bool litWhenOn = true;
        Toggle* toggle = nullptr; // set: lit while the toggle is at `position`
        int position = 0;
    };
    std::map<juce::String, Rocker*> rockers;
    std::map<juce::String, Knob*> knobs;
    std::vector<OnLabel> onLabels;
    uint32_t litMask = 0;
    Toggle* priority = nullptr;

    juce::Image background;
    int rebuildTicks = 0; // timer ticks until the background is redrawn at the new size
    float backgroundScale = 0.0f; // device pixels per point the background was drawn at
    float paintScale = 0.0f;
    float texelUnits = 1.0f; // drawing units per texture texel (half a point)
};
} // namespace ca72ui
