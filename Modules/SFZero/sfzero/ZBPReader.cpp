/*************************************************************************************
 * ZBP/ZBB Reader - Reads Bliss .zbp (preset) and .zbb (bank) files
 * Copyright (C) 2024-2026 discoDSP
 * MIT License — see LICENSE file in parent directory
 *
 * ZBP files are ZIP archives containing:
 *   - program.xml          (program metadata + zone parameters)
 *   - program_000/zone_NNN.flac  (audio samples)
 *
 * ZBB files are ZIP archives containing:
 *   - bank.xml             (bank metadata with multiple programs)
 *   - program_NNN/zone_MMM.flac  (audio samples per program)
 *
 * Cue point handling:
 *   Bliss zones can contain cue/slice markers. Each cue defines a sample offset.
 *   When num_cues > 1, the zone is expanded into multiple SFZ regions, one per
 *   slice, each mapped to a consecutive MIDI key starting from midi_root_key.
 *   With cue_white=1, slices map only to white keys (black keys are skipped).
 *   With cue_stop=1, each slice region's end is set to the next cue boundary.
 *************************************************************************************/
#include "ZBPReader.h"
#include "ZBPSound.h"
#include "SFZRegion.h"
#include "SFZSample.h"

// White-key mapping: for each semitone offset (0-11), the white key index
// (-1 = black key). Same as Bliss keysIndex[].
const int sfzero::ZBPReader::whiteKeyMap[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

sfzero::ZBPReader::ZBPReader(ZBPSound *sound) : sound_(sound) {}
sfzero::ZBPReader::~ZBPReader() {}

void sfzero::ZBPReader::read(const juce::File &file)
{
  auto ext = file.getFileExtension().toLowerCase();

  juce::ZipFile zip(file);
  if (zip.getNumEntries() == 0)
  {
    sound_->addError("Could not open ZIP archive: " + file.getFileName());
    return;
  }

  if (ext == ".zbb")
    readZBB(zip);
  else
    readZBP(zip); // .zbp default
}

void sfzero::ZBPReader::readFromMemory(const juce::MemoryBlock &data)
{
  auto memStream = std::make_unique<juce::MemoryInputStream>(data, false);
  juce::ZipFile zip(memStream.get(), false);

  if (zip.getNumEntries() == 0)
  {
    sound_->addError("Could not open ZBP/ZBB from memory");
    return;
  }

  // Detect format: if bank.xml exists, it's a ZBB
  if (zip.getIndexOfFileName("bank.xml") >= 0)
    readZBB(zip);
  else
    readZBP(zip);
}

void sfzero::ZBPReader::readZBP(juce::ZipFile &zip, int programIndex)
{
  int xmlIndex = zip.getIndexOfFileName("program.xml");
  if (xmlIndex < 0)
  {
    sound_->addError("No program.xml found in ZBP archive");
    return;
  }

  std::unique_ptr<juce::InputStream> xmlStream(zip.createStreamForEntry(xmlIndex));
  if (xmlStream == nullptr)
  {
    sound_->addError("Cannot read program.xml from ZBP archive");
    return;
  }

  juce::String xmlString = xmlStream->readEntireStreamAsString();
  xmlStream.reset();

  auto xml = juce::XmlDocument::parse(xmlString);
  if (xml == nullptr)
  {
    sound_->addError("Failed to parse program.xml");
    return;
  }

  if (xml->getTagName() != "program")
  {
    sound_->addError("Expected <program> root element, got <" + xml->getTagName() + ">");
    return;
  }

  parseProgram(xml.get(), zip, programIndex);
}

void sfzero::ZBPReader::readZBB(juce::ZipFile &zip)
{
  int xmlIndex = zip.getIndexOfFileName("bank.xml");
  if (xmlIndex < 0)
  {
    sound_->addError("No bank.xml found in ZBB archive");
    return;
  }

  std::unique_ptr<juce::InputStream> xmlStream(zip.createStreamForEntry(xmlIndex));
  if (xmlStream == nullptr)
  {
    sound_->addError("Cannot read bank.xml from ZBB archive");
    return;
  }

  juce::String xmlString = xmlStream->readEntireStreamAsString();
  xmlStream.reset();

  auto xml = juce::XmlDocument::parse(xmlString);
  if (xml == nullptr)
  {
    sound_->addError("Failed to parse bank.xml");
    return;
  }

  if (xml->getTagName() != "bank")
  {
    sound_->addError("Expected <bank> root element in ZBB");
    return;
  }

  // Parse <programs> child
  auto *programsXml = xml->getChildByName("programs");
  if (programsXml == nullptr)
  {
    sound_->addError("No <programs> element in bank.xml");
    return;
  }

  int programIndex = 0;
  for (auto *programXml : programsXml->getChildWithTagNameIterator("program"))
  {
    juce::String name = programXml->getStringAttribute("name", "---");
    int numZones = programXml->getIntAttribute("num_zones", 0);

    // Skip empty/default programs
    if (numZones > 0 || name != "---")
      parseProgram(programXml, zip, programIndex);

    programIndex++;
  }
}

void sfzero::ZBPReader::parseProgram(juce::XmlElement *programXml, juce::ZipFile &zip, int programIndex)
{
  juce::String name = programXml->getStringAttribute("name", "---");

  auto *preset = new ZBPSound::Preset(name, programIndex);

  // Parse zones — regions are owned by the preset (OwnedArray), not the base Sound
  auto *zonesXml = programXml->getChildByName("zones");
  if (zonesXml != nullptr)
  {
    int zoneIndex = 0;
    for (auto *zoneXml : zonesXml->getChildWithTagNameIterator("zone"))
    {
      // Load the FLAC sample for this zone
      Sample *sample = loadZoneSample(zoneXml, zip, programIndex, zoneIndex);

      // Read cue point info
      int numCues = zoneXml->getIntAttribute("num_cues", 0);
      int cueStop = zoneXml->getIntAttribute("cue_stop", 1);
      int cueWhite = zoneXml->getIntAttribute("cue_white", 0);
      int rootKey = zoneXml->getIntAttribute("midi_root_key", 60);
      int numSamples = zoneXml->getIntAttribute("num_samples", 0);

      // Read cue positions from <cue_pos> child element
      juce::Array<int> cuePositions;
      auto *cuePosXml = zoneXml->getChildByName("cue_pos");
      if (cuePosXml != nullptr && numCues > 0)
      {
        for (int c = 0; c < numCues; ++c)
        {
          juce::String attrName = "val" + juce::String(c);
          cuePositions.add(cuePosXml->getIntAttribute(attrName, 0));
        }
      }

      if (numCues > 1 && cuePositions.size() > 1)
      {
        // --- Sliced zone: expand into one region per cue slice ---
        // Each slice is mapped to a consecutive key starting from rootKey.
        // With cue_white, only white keys are used.

        int midiKey = rootKey;
        for (int c = 0; c < cuePositions.size(); ++c)
        {
          if (midiKey > 127)
            break;

          // Skip black keys in white-key mode
          if (cueWhite)
          {
            while (midiKey <= 127 && whiteKeyMap[midiKey % 12] < 0)
              midiKey++;
            if (midiKey > 127)
              break;
          }

          auto *region = new Region();
          region->clear();
          fillRegionFromZone(region, zoneXml, sample);

          // Override key range: one key per slice
          region->lokey = midiKey;
          region->hikey = midiKey;

          // Override pitch: slices play at original pitch (no pitch tracking)
          region->pitch_keycenter = midiKey;
          region->pitch_keytrack = 0;

          // Set sample offset to cue start
          region->offset = cuePositions[c];

          // Set sample end to next cue (or end of sample)
          if (cueStop && (c + 1) < cuePositions.size())
            region->end = cuePositions[c + 1];
          else if (numSamples > 0)
            region->end = numSamples - 1;

          // Slices play once but still respond to note-off envelope release
          region->loop_mode = Region::no_loop;

          preset->addRegion(region);
          midiKey++;
        }
      }
      else if (numCues == 1 && cuePositions.size() == 1)
      {
        // Single cue point: offset to cue position, play to end
        auto *region = new Region();
        region->clear();
        fillRegionFromZone(region, zoneXml, sample);
        region->offset = cuePositions[0];
        preset->addRegion(region);
      }
      else
      {
        // --- Normal zone (no cues): single region ---
        auto *region = new Region();
        region->clear();
        fillRegionFromZone(region, zoneXml, sample);
        preset->addRegion(region);
      }

      zoneIndex++;
    }
  }

  sound_->addPreset(preset);
}

sfzero::Sample *sfzero::ZBPReader::loadZoneSample(juce::XmlElement *zoneXml, juce::ZipFile &zip,
                                                    int programIndex, int zoneIndex)
{
  juce::String zonePath = "program_"
    + juce::String(programIndex).paddedLeft('0', 3)
    + "/zone_"
    + juce::String(zoneIndex).paddedLeft('0', 3)
    + ".flac";

  int sampleIdx = zip.getIndexOfFileName(zonePath);
  if (sampleIdx >= 0)
  {
    std::unique_ptr<juce::InputStream> flacStream(zip.createStreamForEntry(sampleIdx));
    if (flacStream != nullptr)
    {
      juce::MemoryBlock flacData;
      flacStream->readIntoMemoryBlock(flacData);

      Sample *sample = sound_->addSample(zonePath);
      sample->setMemoryData(std::move(flacData));
      return sample;
    }
  }

  // Fallback: external path reference
  juce::String externalPath = zoneXml->getStringAttribute("path", "");
  if (externalPath.isNotEmpty())
    return sound_->addSample(externalPath);

  return nullptr;
}

void sfzero::ZBPReader::fillRegionFromZone(Region *region, juce::XmlElement *zoneXml, Sample *sample)
{
  region->sample = sample;

  // --- MIDI key/velocity ranges from child elements ---
  auto *loRange = zoneXml->getChildByName("lo_input_range");
  auto *hiRange = zoneXml->getChildByName("hi_input_range");

  if (loRange != nullptr)
  {
    region->lokey = loRange->getIntAttribute("midi_key", 0);
    region->lovel = loRange->getIntAttribute("midi_vel", 0);
  }
  if (hiRange != nullptr)
  {
    region->hikey = hiRange->getIntAttribute("midi_key", 127);
    region->hivel = hiRange->getIntAttribute("midi_vel", 127);
  }

  // --- Root key, tuning ---
  region->pitch_keycenter = zoneXml->getIntAttribute("midi_root_key", 60);
  region->pitch_keytrack = zoneXml->getIntAttribute("midi_keycents", 100);
  region->tune = zoneXml->getIntAttribute("midi_fine_tune", 0);
  region->transpose = zoneXml->getIntAttribute("midi_coarse_tune", 0);

  // --- Trigger mode ---
  int midiTrigger = zoneXml->getIntAttribute("midi_trigger", 0);
  region->trigger = (midiTrigger == 1) ? Region::release : Region::attack;

  // --- Loop ---
  int loopMode = zoneXml->getIntAttribute("loop_mode", 0);
  switch (loopMode)
  {
    case 0: // OneShot — plays once, no looping, but envelope still responds to note-off
      region->loop_mode = Region::no_loop;
      break;
    case 1: // Forward loop
    case 2: // Bidirectional (SFZero doesn't distinguish, map to continuous)
    case 5: // Crossfade loop (map to continuous)
      region->loop_mode = Region::loop_continuous;
      break;
    case 3: // Backward (map to continuous; SFZero doesn't have reverse)
      region->loop_mode = Region::loop_continuous;
      break;
    case 4: // Sustained — loops while held, plays to end on release
      region->loop_mode = Region::loop_sustain;
      break;
    default:
      region->loop_mode = Region::no_loop;
      break;
  }

  region->loop_start = zoneXml->getIntAttribute("loop_start", 0);
  region->loop_end = zoneXml->getIntAttribute("loop_end", 0);

  // --- Volume & Pan ---
  region->volume = static_cast<float>(zoneXml->getIntAttribute("mp_gain", 0)); // dB
  region->pan = static_cast<float>(zoneXml->getIntAttribute("mp_pan", 0));     // -100 to +100

  // --- Velocity tracking ---
  float velAmp = (float)zoneXml->getDoubleAttribute("vel_amp", 1.0);
  region->amp_veltrack = velAmp * 100.0f; // SFZ uses 0-100 scale

  // --- Pitch bend range ---
  float pitRng = (float)zoneXml->getDoubleAttribute("sys_pit_rng", 0.0833333f);
  int bendCents = static_cast<int>(pitRng * 2400.0f);
  region->bend_up = bendCents;
  region->bend_down = -bendCents;

  // --- Resource groups (SFZ group/off_by) ---
  region->group = zoneXml->getIntAttribute("res_group", 0);
  region->off_by = zoneXml->getIntAttribute("res_offby", 0);

  // Bliss uses linear envelope curves by default (shape=0.5)
  region->linearEnvelope = true;

  // --- Amplitude Envelope (RTPAR child elements) ---
  // Bliss normalized 0-1: sec = 0.001 + x^4 * 15.999
  auto *ampAtt = zoneXml->getChildByName("amp_env_att");
  auto *ampDec = zoneXml->getChildByName("amp_env_dec");
  auto *ampSus = zoneXml->getChildByName("amp_env_sus");
  auto *ampRel = zoneXml->getChildByName("amp_env_rel");

  if (ampAtt != nullptr)
    region->ampeg.attack = envTimeToSeconds((float)ampAtt->getDoubleAttribute("value", 0.0));
  if (ampDec != nullptr)
    region->ampeg.decay = envTimeToSeconds((float)ampDec->getDoubleAttribute("value", 0.5));
  if (ampSus != nullptr)
    region->ampeg.sustain = (float)ampSus->getDoubleAttribute("value", 1.0) * 100.0f;
  if (ampRel != nullptr)
    region->ampeg.release = envTimeToSeconds((float)ampRel->getDoubleAttribute("value", 0.2));
}

// --- Value conversion helpers ---

float sfzero::ZBPReader::envTimeToSeconds(float x)
{
  if (x <= 0.0f)
    return 0.001f;
  float x2 = x * x;
  return 0.001f + x2 * x2 * 15.999f;
}

float sfzero::ZBPReader::filterCutoffToHz(float x)
{
  return 20.0f + (x * x) * (22050.0f - 20.0f);
}
