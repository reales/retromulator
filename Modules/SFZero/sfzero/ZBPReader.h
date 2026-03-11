/*************************************************************************************
 * ZBP/ZBB Reader - Reads Bliss .zbp (preset) and .zbb (bank) files
 * Copyright (C) 2024-2026 discoDSP
 * MIT License — see LICENSE file in parent directory
 *
 * Format: ZIP archive containing XML metadata + FLAC audio samples.
 * See Bliss_ZBP_ZBB_Format_Specification.md for full details.
 *************************************************************************************/
#ifndef ZBPREADER_H_INCLUDED
#define ZBPREADER_H_INCLUDED

#include "SFZCommon.h"

namespace sfzero
{

class ZBPSound;
class Sample;
struct Region;

class ZBPReader
{
public:
  explicit ZBPReader(ZBPSound *sound);
  virtual ~ZBPReader();

  void read(const juce::File &file);
  void readFromMemory(const juce::MemoryBlock &data);

private:
  ZBPSound *sound_;

  void readZBP(juce::ZipFile &zip, int programIndex = 0);
  void readZBB(juce::ZipFile &zip);

  struct ZBPSound_Preset;
  void parseProgram(juce::XmlElement *programXml, juce::ZipFile &zip, int programIndex);

  // Loads the FLAC sample for a zone and returns the Sample pointer (or nullptr)
  Sample *loadZoneSample(juce::XmlElement *zoneXml, juce::ZipFile &zip, int programIndex, int zoneIndex);

  // Fills common region fields from zone XML (envelope, volume, pan, etc.)
  void fillRegionFromZone(Region *region, juce::XmlElement *zoneXml, Sample *sample);

  // Value conversion helpers (from Bliss format spec)
  static float envTimeToSeconds(float x);
  static float filterCutoffToHz(float x);

  // White-key cue mapping (Bliss maps cue indices to white keys only)
  static const int whiteKeyMap[12];

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZBPReader)
};
}

#endif // ZBPREADER_H_INCLUDED
