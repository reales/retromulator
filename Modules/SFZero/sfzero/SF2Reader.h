/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef SF2READER_H_INCLUDED
#define SF2READER_H_INCLUDED

#include "SF2.h"

namespace sfzero
{

class SF2Sound;
class Sample;
struct Region;

class SF2Reader
{
public:
  SF2Reader(SF2Sound *sound, const juce::File &file);
  SF2Reader(SF2Sound *sound, std::unique_ptr<juce::InputStream> stream);
  virtual ~SF2Reader();

  void read();
  juce::AudioSampleBuffer *readSamples(double *progressVar = nullptr, juce::Thread *thread = nullptr);

private:
  SF2Sound *sound_;
  std::unique_ptr<juce::InputStream> file_;

  void addGeneratorToRegion(word genOper, SF2::genAmountType *amount, Region *region);
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SF2Reader)
};
}

#endif // SF2READER_H_INCLUDED
