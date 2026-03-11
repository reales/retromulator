/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef SFZSOUND_H_INCLUDED
#define SFZSOUND_H_INCLUDED

#include "SFZRegion.h"

namespace sfzero
{

class Sample;

class Sound : public juce::SynthesiserSound
{
public:
  explicit Sound(const juce::File &file);
  virtual ~Sound();

  typedef juce::ReferenceCountedObjectPtr<Sound> Ptr;

  bool appliesToNote(int midiNoteNumber) override;
  bool appliesToChannel(int midiChannel) override;

  void addRegion(Region *region); // Takes ownership of the region.
  Sample *addSample(juce::String path, juce::String defaultPath = "");
  void addError(const juce::String &message);
  void addUnsupportedOpcode(const juce::String &opcode);

  virtual void loadRegions();
  void loadRegionsFromText(const char *text, unsigned int length);
  virtual void loadSamples(juce::AudioFormatManager *formatManager, double *progressVar = nullptr,
                           juce::Thread *thread = nullptr);

  Region *getRegionFor(int note, int velocity, Region::Trigger trigger = Region::attack);
  int getNumRegions();
  Region *regionAt(int index);

  const juce::StringArray &getErrors() { return errors_; }
  const juce::StringArray &getWarnings() { return warnings_; }

  virtual int numSubsounds();
  virtual juce::String subsoundName(int whichSubsound);
  virtual void useSubsound(int whichSubsound);
  virtual int selectedSubsound();

  juce::String dump();
  juce::Array<Region *> &getRegions() { return regions_; }
  juce::File &getFile() { return file_; }
  juce::HashMap<juce::String, Sample *> &getSamples() { return samples_; }

private:
  juce::File file_;
  juce::Array<Region *> regions_;
  juce::HashMap<juce::String, Sample *> samples_;
  juce::StringArray errors_;
  juce::StringArray warnings_;
  juce::HashMap<juce::String, juce::String> unsupportedOpcodes_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sound)
};
}

#endif // SFZSOUND_H_INCLUDED
