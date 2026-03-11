/*************************************************************************************
 * ZBP/ZBB Sound - Bliss preset/bank format support for SFZero
 * Copyright (C) 2024-2026 discoDSP
 * MIT License — see LICENSE file in parent directory
 * Reads .zbp (single program) and .zbb (bank) files
 *************************************************************************************/
#ifndef ZBPSOUND_H_INCLUDED
#define ZBPSOUND_H_INCLUDED

#include "SFZSound.h"

namespace sfzero
{

class ZBPSound : public Sound
{
public:
  explicit ZBPSound(const juce::File &file);
  virtual ~ZBPSound();

  void loadRegions() override;
  void loadSamples(juce::AudioFormatManager *formatManager, double *progressVar = nullptr,
                   juce::Thread *thread = nullptr) override;

  // Load from in-memory data (used on iOS)
  void setMemoryData(juce::MemoryBlock &&data) { memoryData_ = std::move(data); }

  struct Preset
  {
    juce::String name;
    int index;
    juce::OwnedArray<Region> regions;

    Preset(juce::String nameIn, int indexIn) : name(nameIn), index(indexIn) {}
    ~Preset() {}
    void addRegion(Region *region) { regions.add(region); }
  };
  void addPreset(Preset *preset);

  int numSubsounds() override;
  juce::String subsoundName(int whichSubsound) override;
  void useSubsound(int whichSubsound) override;
  int selectedSubsound() override;

private:
  juce::MemoryBlock memoryData_;
  juce::OwnedArray<Preset> presets_;
  int selectedPreset_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZBPSound)
};
}

#endif // ZBPSOUND_H_INCLUDED
