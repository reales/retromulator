/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "SFZSample.h"
#include "SFZAkaiSincResampler.h"
#include "SFZDebug.h"

bool sfzero::Sample::load(juce::AudioFormatManager *formatManager)
{
  juce::AudioFormatReader *reader = nullptr;

  if (memoryData_.getSize() > 0)
  {
    // Load from pre-loaded memory data (iOS path)
    auto *memStream = new juce::MemoryInputStream(memoryData_, false);
    reader = formatManager->createReaderFor(std::unique_ptr<juce::InputStream>(memStream));
  }
  else
  {
    reader = formatManager->createReaderFor(file_);
  }

  if (reader == nullptr)
  {
    return false;
  }
  sampleRate_ = reader->sampleRate;
  sampleLength_ = reader->lengthInSamples;
  // Read some extra samples, which will be filled with zeros, so interpolation
  // can be done without having to check for the edge all the time.
  jassert(sampleLength_ < std::numeric_limits<int>::max());

  // Akai S1000 SINC resampler: 8-tap filter centred at tap[3], meaning it
  // reads 3 samples before the integer position and 4 after. We prepend
  // kAkaiTapOffset (3) zero samples and append kAkaiNumTaps (8) samples
  // of padding so the filter never reads out of bounds.
  prePad_ = sfzero::kAkaiTapOffset;
  int totalSamples = prePad_ + static_cast<int>(sampleLength_) + sfzero::kAkaiNumTaps;
  buffer_ = new juce::AudioSampleBuffer(reader->numChannels, totalSamples);
  buffer_->clear();
  // Read sample data starting after the pre-padding region
  reader->read(buffer_, prePad_, static_cast<int>(sampleLength_), 0, true, true);

  juce::StringPairArray *metadata = &reader->metadataValues;
  int numLoops = metadata->getValue("NumSampleLoops", "0").getIntValue();
  if (numLoops > 0)
  {
    loopStart_ = metadata->getValue("Loop0Start", "0").getLargeIntValue();
    loopEnd_ = metadata->getValue("Loop0End", "0").getLargeIntValue();
  }

  // Fix loop boundary for the Akai SINC resampler: copy wrapped loop data
  // past loopEnd so the 8-tap filter reads correct samples instead of zeros.
  // All buffer indices are offset by prePad_ since sample data starts there.
  if (loopStart_ < loopEnd_ && loopEnd_ < sampleLength_)
  {
    juce::uint64 loopLen = loopEnd_ - loopStart_;
    if (loopLen >= static_cast<juce::uint64>(sfzero::kAkaiNumTaps))
    {
      int numChannels = buffer_->getNumChannels();
      int bufLen = buffer_->getNumSamples();
      for (int ch = 0; ch < numChannels; ++ch)
      {
        float *data = buffer_->getWritePointer(ch);
        int padLoopEnd   = prePad_ + static_cast<int>(loopEnd_);
        int padLoopStart = prePad_ + static_cast<int>(loopStart_);
        int copyLen = juce::jmin(sfzero::kAkaiNumTaps, bufLen - padLoopEnd);
        for (int j = 0; j < copyLen; ++j)
          data[padLoopEnd + j] = data[padLoopStart + (j % static_cast<int>(loopLen))];
      }
    }
  }

  return true;
}

sfzero::Sample::~Sample() { delete buffer_; }

juce::String sfzero::Sample::getShortName() { return (file_.getFileName()); }

void sfzero::Sample::setBuffer(juce::AudioSampleBuffer *newBuffer, int prePad)
{
  buffer_ = newBuffer;
  prePad_ = prePad;
  sampleLength_ = buffer_->getNumSamples() - prePad_;
}

juce::AudioSampleBuffer *sfzero::Sample::detachBuffer()
{
  juce::AudioSampleBuffer *result = buffer_;
  buffer_ = nullptr;
  return result;
}

juce::String sfzero::Sample::dump() { return file_.getFullPathName() + "\n"; }

#ifdef JUCE_DEBUG
void sfzero::Sample::checkIfZeroed(const char *where)
{
  if (buffer_ == nullptr)
  {
    sfzero::dbgprintf("SFZSample::checkIfZeroed(%s): no buffer!", where);
    return;
  }

  int samplesLeft = buffer_->getNumSamples();
  juce::int64 nonzero = 0, zero = 0;
  const float *p = buffer_->getReadPointer(0);
  for (; samplesLeft > 0; --samplesLeft)
  {
    if (*p++ == 0.0)
    {
      zero += 1;
    }
    else
    {
      nonzero += 1;
    }
  }
  if (nonzero > 0)
  {
    sfzero::dbgprintf("Buffer not zeroed at %s (%lu vs. %lu).", where, nonzero, zero);
  }
  else
  {
    sfzero::dbgprintf("Buffer zeroed at %s!  (%lu zeros)", where, zero);
  }
}

#endif // JUCE_DEBUG
