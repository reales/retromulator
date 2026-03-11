/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef SF2SOUND_H_INCLUDED
#define SF2SOUND_H_INCLUDED

#include "SFZSound.h"

namespace sfzero
{

class SF2Sound : public Sound
{
public:
  explicit SF2Sound(const juce::File &file);
  virtual ~SF2Sound();

  void loadRegions() override;
  void loadSamples(juce::AudioFormatManager *formatManager, double *progressVar = nullptr, juce::Thread *thread = nullptr) override;

  // Load from in-memory SF2 data (used on iOS)
  void setMemoryData(juce::MemoryBlock &&data) { memoryData_ = std::move(data); }

  struct Preset
  {
    juce::String name;
    int bank;
    int preset;
    juce::OwnedArray<Region> regions;

    Preset(juce::String nameIn, int bankIn, int presetIn) : name(nameIn), bank(bankIn), preset(presetIn) {}
    ~Preset() {}
    void addRegion(Region *region) { regions.add(region); }
  };
  void addPreset(Preset *preset);

  int numSubsounds() override;
  juce::String subsoundName(int whichSubsound) override;
  void useSubsound(int whichSubsound) override;
  int selectedSubsound() override;

  Sample *sampleFor(double sampleRate);
  void setSamplesBuffer(juce::AudioSampleBuffer *buffer);

private:
  juce::MemoryBlock memoryData_;
  juce::OwnedArray<Preset> presets_;
  juce::HashMap<int, Sample *> samplesByRate_;
  int selectedPreset_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SF2Sound)
};
}

#endif // SF2SOUND_H_INCLUDED
