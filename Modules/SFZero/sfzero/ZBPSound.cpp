/*************************************************************************************
 * ZBP/ZBB Sound - Bliss preset/bank format support for SFZero
 * Copyright (C) 2024-2026 discoDSP
 * MIT License — see LICENSE file in parent directory
 *************************************************************************************/
#include "ZBPSound.h"
#include "ZBPReader.h"
#include "SFZSample.h"

sfzero::ZBPSound::ZBPSound(const juce::File &file) : sfzero::Sound(file), selectedPreset_(0) {}

sfzero::ZBPSound::~ZBPSound()
{
  // Presets own the regions, so clear them out of the base class
  // regions array so ~Sound() doesn't try to delete them.
  getRegions().clear();
}

void sfzero::ZBPSound::loadRegions()
{
  sfzero::ZBPReader reader(this);

  if (memoryData_.getSize() > 0)
    reader.readFromMemory(memoryData_);
  else
    reader.read(getFile());

  // Select first preset
  if (presets_.size() > 0)
    useSubsound(0);
}

void sfzero::ZBPSound::loadSamples(juce::AudioFormatManager *formatManager, double *progressVar,
                                     juce::Thread *thread)
{
  if (progressVar)
    *progressVar = 0.0;

  // Samples are loaded during loadRegions() by the ZBPReader (FLAC from ZIP).
  // Just call the base class to load any remaining file-based samples.
  Sound::loadSamples(formatManager, progressVar, thread);

  if (progressVar)
    *progressVar = 1.0;
}

void sfzero::ZBPSound::addPreset(sfzero::ZBPSound::Preset *preset) { presets_.add(preset); }

int sfzero::ZBPSound::numSubsounds() { return presets_.size(); }

juce::String sfzero::ZBPSound::subsoundName(int whichSubsound)
{
  Preset *preset = presets_[whichSubsound];
  if (preset == nullptr)
    return "";
  return preset->name;
}

void sfzero::ZBPSound::useSubsound(int whichSubsound)
{
  if (whichSubsound < 0 || whichSubsound >= presets_.size())
    return;

  selectedPreset_ = whichSubsound;
  getRegions().clear();
  getRegions().addArray(presets_[whichSubsound]->regions);
}

int sfzero::ZBPSound::selectedSubsound() { return selectedPreset_; }
