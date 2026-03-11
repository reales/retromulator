/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef RIFF_H_INCLUDED
#define RIFF_H_INCLUDED

#include "SF2WinTypes.h"

namespace sfzero
{

struct RIFFChunk
{
  enum Type
  {
    RIFF,
    LIST,
    Custom
  };

  fourcc id;
  dword size;
  Type type;
  juce::int64 start;

  void readFrom(juce::InputStream *file);
  void seek(juce::InputStream *file);
  void seekAfter(juce::InputStream *file);

  juce::int64 end() { return (start + size); }
  juce::String readString(juce::InputStream *file);
};
}

#endif // RIFF_H_INCLUDED
