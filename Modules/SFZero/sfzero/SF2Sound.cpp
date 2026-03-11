/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "SF2Sound.h"
#include "SF2Reader.h"
#include "SFZAkaiSincResampler.h"
#include "SFZSample.h"

sfzero::SF2Sound::SF2Sound(const juce::File &file) : sfzero::Sound(file), selectedPreset_(0) {}

sfzero::SF2Sound::~SF2Sound()
{
  // "presets" owns the regions, so clear them out of "regions" so ~SFZSound()
  // doesn't try to delete them.
  getRegions().clear();

  // The samples all share a single buffer, so make sure they don't all delete
  // it.
  juce::AudioSampleBuffer *buffer = nullptr;
  for (juce::HashMap<int, sfzero::Sample *>::Iterator i(samplesByRate_); i.next();)
  {
    buffer = i.getValue()->detachBuffer();
  }
  delete buffer;
}

class PresetComparator
{
public:
  static int compareElements(const sfzero::SF2Sound::Preset *first, const sfzero::SF2Sound::Preset *second)
  {
    int cmp = first->bank - second->bank;

    if (cmp != 0)
    {
      return cmp;
    }
    return first->preset - second->preset;
  }
};

void sfzero::SF2Sound::loadRegions()
{
  if (memoryData_.getSize() > 0)
  {
    sfzero::SF2Reader reader(this, std::make_unique<juce::MemoryInputStream>(memoryData_, false));
    reader.read();
  }
  else
  {
    sfzero::SF2Reader reader(this, getFile());
    reader.read();
  }

  // Sort the presets.
  PresetComparator comparator;
  presets_.sort(comparator);

  useSubsound(0);
}

void sfzero::SF2Sound::loadSamples(juce::AudioFormatManager * /*formatManager*/, double *progressVar, juce::Thread *thread)
{
  std::unique_ptr<sfzero::SF2Reader> readerPtr;
  if (memoryData_.getSize() > 0)
    readerPtr = std::make_unique<sfzero::SF2Reader>(this, std::make_unique<juce::MemoryInputStream>(memoryData_, false));
  else
    readerPtr = std::make_unique<sfzero::SF2Reader>(this, getFile());

  juce::AudioSampleBuffer *buffer = readerPtr->readSamples(progressVar, thread);

  if (buffer)
  {
    // All the SFZSamples will share the buffer (which includes pre-padding
    // for the SINC resampler).
    for (juce::HashMap<int, sfzero::Sample *>::Iterator i(samplesByRate_); i.next();)
    {
      i.getValue()->setBuffer(buffer, sfzero::kAkaiTapOffset);
    }
  }

  if (progressVar)
  {
    *progressVar = 1.0;
  }
}

void sfzero::SF2Sound::addPreset(sfzero::SF2Sound::Preset *preset) { presets_.add(preset); }

int sfzero::SF2Sound::numSubsounds() { return presets_.size(); }

juce::String sfzero::SF2Sound::subsoundName(int whichSubsound)
{
  Preset *preset = presets_[whichSubsound];
  return preset->name;
}

void sfzero::SF2Sound::useSubsound(int whichSubsound)
{
  selectedPreset_ = whichSubsound;
  getRegions().clear();
  getRegions().addArray(presets_[whichSubsound]->regions);
}

int sfzero::SF2Sound::selectedSubsound() { return selectedPreset_; }

sfzero::Sample *sfzero::SF2Sound::sampleFor(double sampleRate)
{
  sfzero::Sample *sample = samplesByRate_[static_cast<int>(sampleRate)];

  if (sample == nullptr)
  {
    sample = new sfzero::Sample(sampleRate);
    samplesByRate_.set(static_cast<int>(sampleRate), sample);
  }
  return sample;
}

void sfzero::SF2Sound::setSamplesBuffer(juce::AudioSampleBuffer *buffer)
{
  for (juce::HashMap<int, sfzero::Sample *>::Iterator i(samplesByRate_); i.next();)
  {
    i.getValue()->setBuffer(buffer, sfzero::kAkaiTapOffset);
  }
}
