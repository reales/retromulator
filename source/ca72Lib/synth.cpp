/**
 * CA-72 engine host, after the CA-72 plugin's processor.
 *
 * Based on the CA-72 by Idle Foundry (GPL-3.0-or-later)
 */
#include "synth.h"
#include "dsp/freq_dial.h"

using namespace ca72;
using voice::Waveform;
using tuning::Range;

namespace
{
const juce::StringArray rangeNames { "LO", "32'", "16'", "8'", "4'", "2'" };
const juce::StringArray waveNames12 { "Triangle", "Shark tooth", "Sawtooth", "Square", "Wide rect", "Narrow rect" };
const juce::StringArray waveNames3 { "Triangle", "Reverse saw", "Sawtooth", "Square", "Wide rect", "Narrow rect" };
const juce::StringArray qualityNames { "LO", "HI", "ULTRA" };

Waveform waveAt (int osc, int i)
{
    static const Waveform w12[] { Waveform::Triangle, Waveform::SharkTooth, Waveform::Sawtooth,
                                  Waveform::Square, Waveform::WideRectangle, Waveform::NarrowRectangle };
    static const Waveform w3[] { Waveform::Triangle, Waveform::ReverseSawtooth, Waveform::Sawtooth,
                                 Waveform::Square, Waveform::WideRectangle, Waveform::NarrowRectangle };
    return osc == 2 ? w3[i] : w12[i];
}

juce::String id (const char* prefix, int osc) { return juce::String (prefix) + juce::String (osc + 1); }

// A contour time knob (0..10) as its dial is printed: 10 ms, 200 ms, 600 ms, 1 s, 5 s and 10 s at
// their marks (upstream's art.rs), logarithmic between them.
juce::String dialTime (float v)
{
    static const std::array<std::pair<double, double>, 6> marks { { { -120, 10 }, { -90, 200 }, { -30, 600 }, { 30, 1000 }, { 67, 5000 }, { 108, 10000 } } };
    const double angle = -150.0 + 30.0 * (double) v;
    size_t i = 0;
    while (i + 2 < marks.size() && angle > marks[i + 1].first)
        ++i;
    const auto [a0, t0] = marks[i];
    const auto [a1, t1] = marks[i + 1];
    const double ms = t0 * std::pow (t1 / t0, (angle - a0) / (a1 - a0));
    return ms < 1000.0 ? juce::String (juce::roundToInt (ms)) + " ms" : juce::String (ms / 1000.0, 1) + " s";
}

// DRIVE's level compensation: how much louder DRIVE makes the output, every 3 dB of DRIVE,
// the mean over ten notes of five patches measured on the HI voice. The output is turned down
// by as much, so DRIVE changes the tone, not the level.
double driveMakeup (double driveDb)
{
    static constexpr std::array<double, 9> rise { 0.0, 2.65, 5.05, 7.09, 8.71, 9.91, 10.73, 11.24, 11.52 };
    const double x = juce::jlimit (0.0, 24.0, driveDb) / 3.0;
    const int i = std::min (7, (int) x);
    return -(rise[(size_t) i] + (rise[(size_t) i + 1] - rise[(size_t) i]) * (x - i));
}

// MAIN OUTPUT VOLUME as upstream: R20 (5K audio taper) into the jack's 10K load.
double outputGain (double volume, bool on)
{
    if (! on)
        return 0.0;
    const double t = voice::audio_taper (volume);
    const double top = 5e3 * (1.0 - t), bottom = 5e3 * t;
    const double load = bottom * 10e3 / (bottom + 10e3);
    return load <= 0.0 ? 0.0 : load / (top + load);
}
} // namespace

namespace ca72Lib
{
Synth::Synth()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("CA-72 voice builder"),
      apvts (*this, nullptr, "CA72", createLayout())
{
    for (auto* p : getParameters())
        p->addListener (this);
}

Synth::~Synth()
{
    for (auto* p : getParameters())
        p->removeListener (this);
    stopThread (-1);
}

void Synth::parameterValueChanged (int parameterIndex, float)
{
    if (applyingPreset.load() || parameterIndex < 0 || parameterIndex >= 64)
        return;
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating(); mm != nullptr && mm->isThisTheMessageThread())
        edited.fetch_or (uint64_t (1) << parameterIndex);
}

juce::AudioProcessorValueTreeState::ParameterLayout Synth::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto num = [] (int decimals, const String& unit)
    {
        return [decimals, unit] (float v, int)
        {
            // A fixed number of decimals, so the readout keeps its width as the value moves.
            auto s = String (v, decimals);
            if (s.startsWith ("-") && s.removeCharacters ("-0.").isEmpty())
                s = s.substring (1);
            return s + (unit.isEmpty() ? "" : " " + unit);
        };
    };
    auto add = [&l] (const String& pid, const String& name, NormalisableRange<float> r, float def,
                     std::function<String (float, int)> text)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { pid, 1 }, name, r, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    // The panel's dials read 0 to 10, CUTOFF -5 to +5, as printed on the instrument.
    auto dial = [&] (const String& pid, const String& name, float def) { add (pid, name, { 0.0f, 10.0f, 0.01f }, def, num (1, "")); };
    auto sw = [&l] (const String& pid, const String& name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { pid, 1 }, name, def)); };
    auto choice = [&l] (const String& pid, const String& name, const StringArray& c, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { pid, 1 }, name, c, def)); };
    auto percent = [&] (const String& pid, const String& name, float def) { add (pid, name, { 0.0f, 100.0f, 1.0f }, def, num (0, "%")); };

    choice ("quality", "Quality", qualityNames, 1);
    add ("tune", "Tune", { -2.5f, 2.5f, 0.01f }, 0.0f, num (1, ""));
    dial ("glide", "Glide", 0.0f);
    sw ("glideOn", "Glide on", false);
    sw ("decay", "Decay", true);
    dial ("modMix", "Modulation mix", 0.0f);
    percent ("modWheelKnob", "Mod wheel", 0.0f);
    sw ("oscMod", "Osc modulation", false);
    sw ("filterMod", "Filter modulation", false);
    sw ("osc3Control", "Osc 3 control", true);

    for (int o = 0; o < 3; ++o)
    {
        const auto n = "Osc " + String (o + 1);
        choice (id ("range", o), n + " range", rangeNames, 3);
        choice (id ("wave", o), n + " waveform", o == 2 ? waveNames3 : waveNames12, 2);
        sw (id ("on", o), n + " on", o == 0);
        dial (id ("vol", o), n + " volume", 8.0f);
        if (o > 0)
        {
            const double end = freq_dial::DIAL_END;
            add (id ("freq", o), n + " frequency", { (float) -end, (float) end, 0.001f }, 0.0f,
                 [o] (float v, int)
                 {
                     // In semitones through the pot's law: exact intervals when Shift-snapped.
                     const freq_dial::Pot pot (o == 1 ? freq_dial::Pot::Two : freq_dial::Pot::Three);
                     const double st = pot.semitones (((double) v + freq_dial::DIAL_END) / (2.0 * freq_dial::DIAL_END));
                     auto s = String (st, 1);
                     if (s == "-0.0") s = "0.0";
                     return (st >= 0.05 ? "+" : "") + s + " st";
                 });
        }
    }

    dial ("extVol", "External input volume", 5.0f);
    sw ("extOn", "External input on", false);
    dial ("feedback", "Feedback", 0.0f);
    add ("drive", "Drive", { 0.0f, 24.0f, 1.0f }, 0.0f, num (0, "dB"));
    sw ("noiseOn", "Noise on", false);
    dial ("noiseVol", "Noise volume", 8.0f);
    sw ("noisePink", "Noise pink", false);

    add ("cutoff", "Cutoff", { -5.0f, 5.0f, 0.01f }, 0.0f, num (1, ""));
    dial ("emphasis", "Emphasis", 0.0f);
    dial ("contourAmt", "Amount of contour", 5.0f);
    sw ("kbd1", "Keyboard control 1", true);
    sw ("kbd2", "Keyboard control 2", false);
    sw ("filterHi", "Filter mode HI", false);
    // The contours' times read as their dials print them.
    auto timeDial = [&] (const String& pid, const String& name, float def) { add (pid, name, { 0.0f, 10.0f, 0.01f }, def, [] (float v, int) { return dialTime (v); }); };
    timeDial ("fAttack", "Filter attack", 0.0f);
    timeDial ("fDecay", "Filter decay", 4.0f);
    dial ("fSustain", "Filter sustain", 5.0f);
    timeDial ("lAttack", "Loudness attack", 0.0f);
    timeDial ("lDecay", "Loudness decay", 4.0f);
    dial ("lSustain", "Loudness sustain", 8.0f);
    choice ("priority", "Note priority", { "LOW", "LAST", "HIGH" }, 0);
    sw ("retrigger", "Retrigger", false);

    sw ("poly", "Poly", false);
    sw ("unison", "Unison", false);
    sw ("doubled", "Double", false);
    add ("detune", "Double detune", { 0.0f, (float) ca72poly::DOUBLE_CENTS, 1.0f }, 0.0f, num (0, "cents"));
    percent ("spread", "Spread", 0.0f);
    percent ("entropy", "Entropy", 0.0f);

    dial ("volume", "Main output volume", 8.0f);
    add ("level", "Level", { -30.0f, 12.0f, 1.0f }, 0.0f, num (0, "dB"));
    sw ("a440", "A-440", false);
    add ("pitchWheel", "Pitch wheel", { -1.0f, 1.0f, 0.001f }, 0.0f, num (1, ""));
    return l;
}

voice::Panel Synth::readPanel() const
{
    auto v = [this] (const juce::String& pid) { return (double) apvts.getRawParameterValue (pid)->load(); };
    auto b = [this] (const juce::String& pid) { return apvts.getRawParameterValue (pid)->load() > 0.5f; };
    auto ten = [&v] (const juce::String& pid) { return v (pid) / 10.0; };

    auto p = voice::Panel::default_();
    for (int o = 0; o < 3; ++o)
    {
        auto& op = p.osc[(size_t) o];
        op.range = static_cast<Range::Value> ((int) v (id ("range", o)));
        op.waveform = waveAt (o, (int) v (id ("wave", o)));
        op.on = b (id ("on", o));
        op.volume = ten (id ("vol", o));
        if (o > 0)
            op.freq = (v (id ("freq", o)) + freq_dial::DIAL_END) / (2.0 * freq_dial::DIAL_END);
    }
    p.osc3_control = b ("osc3Control");
    p.cutoff = (v ("cutoff") + 5.0) / 10.0;
    p.emphasis = ten ("emphasis");
    p.contour_amount = ten ("contourAmt");
    p.keyboard_control_1 = b ("kbd1");
    p.keyboard_control_2 = b ("kbd2");
    p.filter_contour = { ten ("fAttack"), ten ("fDecay"), ten ("fSustain") };
    p.loudness_contour = { ten ("lAttack"), ten ("lDecay"), ten ("lSustain") };
    p.glide = ten ("glide");
    p.glide_on = b ("glideOn");
    p.decay = b ("decay");
    p.noise_on = b ("noiseOn");
    p.ext_on = b ("extOn");
    p.ext_volume = ten ("extVol");
    p.noise_volume = ten ("noiseVol");
    p.noise_pink = b ("noisePink");
    p.mod_mix = ten ("modMix");
    p.osc_mod = b ("oscMod");
    p.filter_mod = b ("filterMod");
    p.filter_hi = b ("filterHi");
    p.tune = v ("tune") / 2.5;
    p.priority = static_cast<voice::Priority> ((int) v ("priority"));
    p.retrigger = b ("retrigger");
    p.a440 = b ("a440");
    p.pitch_wheel = juce::jlimit (-1.0, 1.0, (double) bend + v ("pitchWheel"));
    p.mod_wheel = std::max ((double) modWheel, v ("modWheelKnob") / 100.0);
    return p;
}

ca72poly::Settings Synth::readSettings() const
{
    auto v = [this] (const char* pid) { return (double) apvts.getRawParameterValue (pid)->load(); };
    ca72poly::Settings s;
    // UNISON stacks the mono voice; POLY plays without it.
    s.poly = v ("poly") > 0.5;
    s.unison = ! s.poly && v ("unison") > 0.5;
    s.voices = s.unison ? ca72poly::UNISON_VOICES : ca72poly::POLY_VOICES;
    s.doubled = v ("doubled") > 0.5;
    s.detune = v ("detune") / ca72poly::DOUBLE_CENTS;
    s.spread = v ("spread") / 100.0;
    s.entropy = v ("entropy") / 100.0;
    s.drive = std::pow (10.0, v ("drive") / 20.0);
    s.feedback = voice::feedback_law (v ("feedback") / 10.0);
    return s;
}

void Synth::requestEngine (QualityMode mode)
{
    if (wantedMode.exchange ((int) mode) == (int) mode && (isThreadRunning() || engine != nullptr))
        return;
    building = true;
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
    else
        notify();
}

std::unique_ptr<ca72poly::Engine> Synth::buildEngine (QualityMode mode, double rate) const
{
    auto e = std::make_unique<ca72poly::Engine>();
    switch (mode)
    {
        case qualityLo: e->build (rate, 1, true, voice::Quality::Potato, 2, false, seed); break;
        case qualityHi: e->build (rate, 1, false, voice::Quality::Potato, 2, false, seed); break;
        case qualityUltra: e->build (rate, 1, false, voice::Quality::NoCompromises, 2, false, seed); break;
        case qualityUltra4x: e->build (rate, 4, false, voice::Quality::NoCompromises, 4, true, seed); break;
    }
    e->mode = (int) mode;
    // Slot threads join the host's audio workgroup (AU, standalone) to run with the audio thread.
    e->startThreads ([this]
    {
        thread_local juce::WorkgroupToken token;
        juce::AudioWorkgroup wg;
        {
            const juce::SpinLock::ScopedLockType sl (workgroupLock);
            wg = workgroup;
        }
        if (wg)
            wg.join (token);
        else
            token.reset();
        return (bool) wg;
    });
    return e;
}

void Synth::audioWorkgroupContextChanged (const juce::AudioWorkgroup& wg)
{
    {
        const juce::SpinLock::ScopedLockType sl (workgroupLock);
        workgroup = wg;
    }
    const juce::SpinLock::ScopedLockType sl (swapLock);
    for (auto* e : { engine.get(), pending.get() })
        if (e != nullptr)
            e->rejoinThreads();
}

Synth::QualityMode Synth::modeNow() const
{
    // Offline renders always take the full-path 4x engine; the switch picks the live one.
    return isNonRealtime() ? qualityUltra4x : (QualityMode) juce::jlimit (0, 2, (int) apvts.getRawParameterValue ("quality")->load());
}

void Synth::run()
{
    while (! threadShouldExit())
    {
        const auto mode = (QualityMode) wantedMode.load();
        const double rate = hostRate;

        auto e = buildEngine (mode, rate);

        {
            const juce::SpinLock::ScopedLockType sl (swapLock);
            pending = std::move (e);
            fresh = true;
        }
        if (wantedMode.load() == (int) mode && hostRate == rate)
        {
            building = false;
            wait (-1);
        }
    }
}

void Synth::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratch.setSize (2, std::max (8192, samplesPerBlock));
    if (sampleRate != hostRate || engine == nullptr)
    {
        hostRate = sampleRate;
        wantedMode = -1;
    }
    if (isNonRealtime())
    {
        // Built here, before the render's first block, rather than on the builder thread.
        engine = buildEngine (qualityUltra4x, hostRate);
        wantedMode = qualityUltra4x;
        return;
    }
    requestEngine (modeNow());
}

bool Synth::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}


void Synth::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto started = ca72poly::Workers::Clock::now();
    buffer.clear();
    const int total = buffer.getNumSamples();
    if (scratch.getNumSamples() < total)
        return;

    if (isNonRealtime() && (engine == nullptr || engine->mode != qualityUltra4x))
    {
        engine = buildEngine (qualityUltra4x, hostRate);
        wantedMode = qualityUltra4x;
    }
    else if (! isNonRealtime())
        requestEngine (modeNow());
    {
        const juce::SpinLock::ScopedTryLockType sl (swapLock);
        if (! isNonRealtime() && sl.isLocked() && fresh.exchange (false))
            std::swap (engine, pending);
    }
    if (engine == nullptr)
        return;

    auto& e = *engine;
    auto panel = readPanel();
    const auto settings = readSettings();
    auto* l = scratch.getWritePointer (0);
    auto* r = scratch.getWritePointer (1);
    e.begin (total, settings);
    for (const auto meta : midi)
    {
        e.advance (std::min (meta.samplePosition, total), panel);
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            e.noteOn (m.getNoteNumber());
        else if (m.isNoteOff())
            e.noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
        {
            bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            panel = readPanel();
        }
        else if (m.isController() && m.getControllerNumber() == 1)
        {
            modWheel = (float) m.getControllerValue() / 127.0f;
            panel = readPanel();
        }
        else if (m.isAllSoundOff())
            e.allSoundOff();
        else if (m.isAllNotesOff())
            e.allNotesOff();
        else if (m.isResetAllControllers())
        {
            bend = 0.0f;
            modWheel = 0.0f;
            panel = readPanel();
        }
    }
    e.advance (total, panel);
    // Workers' voices are waited for until 75 % of the block's period (upstream's DEADLINE); offline, for all.
    std::optional<ca72poly::Workers::Clock::time_point> deadline;
    if (! isNonRealtime())
        deadline = started + std::chrono::nanoseconds ((int64_t) (0.75e9 * total / hostRate));
    e.finish (l, r, deadline);

    const float gain = (float) (outputGain (apvts.getRawParameterValue ("volume")->load() / 10.0, true)
                                * juce::Decibels::decibelsToGain ((double) apvts.getRawParameterValue ("level")->load()
                                                                  + driveMakeup (apvts.getRawParameterValue ("drive")->load())));
    // Ramped over the block so MAIN OUTPUT VOLUME and LEVEL move without steps.
    const float from = lastGain < 0.0f ? gain : lastGain;
    lastGain = gain;
    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFromWithRamp (0, 0, l, total, from, gain);
        buffer.copyFromWithRamp (1, 0, r, total, from, gain);
    }
    else
    {
        buffer.copyFromWithRamp (0, 0, l, total, 0.5f * from, 0.5f * gain);
        buffer.addFromWithRamp (0, 0, r, total, 0.5f * from, 0.5f * gain);
    }
}

void Synth::applyPreset (const std::map<juce::String, float>& values)
{
    applyingPreset = true;
    // QUALITY, POLY and UNISON are how it is played: a preset leaves them unless it sets them.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && rp->paramID != "quality" && rp->paramID != "poly" && rp->paramID != "unison")
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (const auto& [pid, value] : values)
        if (pid != "quality")
            if (auto* rp = apvts.getParameter (pid))
                rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    applyingPreset = false;
}

std::map<juce::String, float> Synth::getPresetValues() const
{
    std::map<juce::String, float> out;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && rp->paramID != "quality")
            out[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
    return out;
}

void Synth::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void Synth::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            applyingPreset = true;
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            applyingPreset = false;
        }
}
} // namespace ca72Lib
