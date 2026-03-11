/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "SF2Reader.h"
#include "RIFF.h"
#include "SF2.h"
#include "SF2Generator.h"
#include "SF2Sound.h"
#include "SFZAkaiSincResampler.h"

sfzero::SF2Reader::SF2Reader(sfzero::SF2Sound *soundIn, const juce::File &fileIn) : sound_(soundIn)
{
  file_ = fileIn.createInputStream();
}

sfzero::SF2Reader::SF2Reader(sfzero::SF2Sound *soundIn, std::unique_ptr<juce::InputStream> stream) : sound_(soundIn)
{
  file_ = std::move(stream);
}

sfzero::SF2Reader::~SF2Reader() {}

void sfzero::SF2Reader::read()
{
  if (file_ == nullptr)
  {
    sound_->addError("Couldn't open file.");
    return;
  }

  // Read the hydra.
  sfzero::SF2::Hydra hydra;
  file_->setPosition(0);
  sfzero::RIFFChunk riffChunk;
  riffChunk.readFrom(file_.get());
  while (file_->getPosition() < riffChunk.end())
  {
    sfzero::RIFFChunk chunk;
    chunk.readFrom(file_.get());
    if (FourCCEquals(chunk.id, "pdta"))
    {
      hydra.readFrom(file_.get(), chunk.end());
      break;
    }
    chunk.seekAfter(file_.get());
  }
  if (!hydra.isComplete())
  {
    sound_->addError("Invalid SF2 file (missing or incomplete hydra).");
    return;
  }

  // Read each preset.
  for (int whichPreset = 0; whichPreset < hydra.phdrNumItems - 1; ++whichPreset)
  {
    sfzero::SF2::phdr *phdr = &hydra.phdrItems[whichPreset];
    sfzero::SF2Sound::Preset *preset = new sfzero::SF2Sound::Preset(phdr->presetName, phdr->bank, phdr->preset);
    sound_->addPreset(preset);

    // Zones.
    //*** TODO: Handle global zone (modulators only).
    int zoneEnd = phdr[1].presetBagNdx;
    for (int whichZone = phdr->presetBagNdx; whichZone < zoneEnd; ++whichZone)
    {
      sfzero::SF2::pbag *pbag = &hydra.pbagItems[whichZone];
      sfzero::Region presetRegion;
      presetRegion.clearForRelativeSF2();

      // Generators.
      int genEnd = pbag[1].genNdx;
      for (int whichGen = pbag->genNdx; whichGen < genEnd; ++whichGen)
      {
        sfzero::SF2::pgen *pgen = &hydra.pgenItems[whichGen];

        // Instrument.
        if (pgen->genOper == sfzero::SF2Generator::instrument)
        {
          sfzero::word whichInst = pgen->genAmount.wordAmount;
          if (whichInst < hydra.instNumItems)
          {
            sfzero::Region instRegion;
            instRegion.clearForSF2();
            // Preset generators are supposed to be "relative" modifications of
            // the instrument settings, but that makes no sense for ranges.
            // For those, we'll have the instrument's generator take
            // precedence, though that may not be correct.
            instRegion.lokey = presetRegion.lokey;
            instRegion.hikey = presetRegion.hikey;
            instRegion.lovel = presetRegion.lovel;
            instRegion.hivel = presetRegion.hivel;

            sfzero::SF2::inst *inst = &hydra.instItems[whichInst];
            int firstZone = inst->instBagNdx;
            int zoneEnd2 = inst[1].instBagNdx;
            for (int whichZone2 = firstZone; whichZone2 < zoneEnd2; ++whichZone2)
            {
              sfzero::SF2::ibag *ibag = &hydra.ibagItems[whichZone2];

              // Generators.
              sfzero::Region zoneRegion = instRegion;
              bool hadSampleID = false;
              int genEnd2 = ibag[1].instGenNdx;
              for (int whichGen2 = ibag->instGenNdx; whichGen2 < genEnd2; ++whichGen2)
              {
                sfzero::SF2::igen *igen = &hydra.igenItems[whichGen2];
                if (igen->genOper == sfzero::SF2Generator::sampleID)
                {
                  int whichSample = igen->genAmount.wordAmount;
                  sfzero::SF2::shdr *shdr = &hydra.shdrItems[whichSample];
                  zoneRegion.addForSF2(&presetRegion);
                  zoneRegion.sf2ToSFZ();
                  zoneRegion.offset += shdr->start;
                  zoneRegion.end += shdr->end;
                  zoneRegion.loop_start += shdr->startLoop;
                  zoneRegion.loop_end += shdr->endLoop;
                  if (shdr->endLoop > 0)
                  {
                    zoneRegion.loop_end -= 1;
                  }
                  if (zoneRegion.pitch_keycenter == -1)
                  {
                    zoneRegion.pitch_keycenter = shdr->originalPitch;
                  }
                  zoneRegion.tune += shdr->pitchCorrection;

                  // Pin initialAttenuation to max +6dB.
                  if (zoneRegion.volume > 6.0)
                  {
                    zoneRegion.volume = 6.0;
                    sound_->addUnsupportedOpcode("extreme gain in initialAttenuation");
                  }

                  sfzero::Region *newRegion = new sfzero::Region();
                  *newRegion = zoneRegion;
                  newRegion->sample = sound_->sampleFor(shdr->sampleRate);
                  preset->addRegion(newRegion);
                  hadSampleID = true;
                }
                else
                {
                  addGeneratorToRegion(igen->genOper, &igen->genAmount, &zoneRegion);
                }
              }

              // Handle instrument's global zone.
              if ((whichZone2 == firstZone) && !hadSampleID)
              {
                instRegion = zoneRegion;
              }

              // Modulators.
              int modEnd = ibag[1].instModNdx;
              int whichMod = ibag->instModNdx;
              if (whichMod < modEnd)
              {
                sound_->addUnsupportedOpcode("any modulator");
              }
            }
          }
          else
          {
            sound_->addError("Instrument out of range.");
          }
        }
        // Other generators.
        else
        {
          addGeneratorToRegion(pgen->genOper, &pgen->genAmount, &presetRegion);
        }
      }

      // Modulators.
      int modEnd = pbag[1].modNdx;
      int whichMod = pbag->modNdx;
      if (whichMod < modEnd)
      {
        sound_->addUnsupportedOpcode("any modulator");
      }
    }
  }
}

juce::AudioSampleBuffer *sfzero::SF2Reader::readSamples(double *progressVar, juce::Thread *thread)
{
  static const int bufferSize = 32768;
  // Number of extra samples to duplicate past loop endpoints for the
  // Akai S1000 SINC resampler (8 taps per phase).
  static const int kLoopPadding = sfzero::kAkaiNumTaps;

  if (file_ == nullptr)
  {
    sound_->addError("Couldn't open file.");
    return nullptr;
  }

  // Find the "sdta" chunk and locate "smpl" and optional "sm24" sub-chunks.
  file_->setPosition(0);
  sfzero::RIFFChunk riffChunk;
  riffChunk.readFrom(file_.get());
  bool found = false;
  sfzero::RIFFChunk chunk;
  while (file_->getPosition() < riffChunk.end())
  {
    chunk.readFrom(file_.get());
    if (FourCCEquals(chunk.id, "sdta"))
    {
      found = true;
      break;
    }
    chunk.seekAfter(file_.get());
  }
  if (!found)
  {
    sound_->addError("SF2 is missing its \"sdta\" chunk.");
    return nullptr;
  }
  juce::int64 sdtaEnd = chunk.end();

  // Scan for smpl and sm24 sub-chunks within sdta.
  sfzero::RIFFChunk smplChunk, sm24Chunk;
  bool foundSmpl = false, foundSm24 = false;
  while (file_->getPosition() < sdtaEnd)
  {
    sfzero::RIFFChunk subChunk;
    subChunk.readFrom(file_.get());
    if (FourCCEquals(subChunk.id, "smpl"))
    {
      smplChunk = subChunk;
      foundSmpl = true;
    }
    else if (FourCCEquals(subChunk.id, "sm24"))
    {
      sm24Chunk = subChunk;
      foundSm24 = true;
    }
    subChunk.seekAfter(file_.get());
  }
  if (!foundSmpl)
  {
    sound_->addError("SF2 is missing its \"smpl\" chunk.");
    return nullptr;
  }

  // Allocate the AudioSampleBuffer with pre-padding for the SINC resampler.
  // The 8-tap filter reads 3 samples before the integer position, so we
  // prepend kAkaiTapOffset zeros so that samples at the very start of the
  // buffer (and each SF2 region that begins at sample 0) are interpolated
  // correctly instead of falling back to nearest-sample.
  int numSamples = static_cast<int>(smplChunk.size / sizeof(short));
  int prePad = sfzero::kAkaiTapOffset;
  juce::AudioSampleBuffer *sampleBuffer = new juce::AudioSampleBuffer(1, prePad + numSamples);
  sampleBuffer->clear();

  // Validate sm24 chunk size: must have exactly one byte per sample.
  bool useSm24 = foundSm24 && (static_cast<int>(sm24Chunk.size) >= numSamples);

  // Read 16-bit samples from smpl chunk and convert to float.
  file_->setPosition(smplChunk.start);
  short *buffer = new short[bufferSize];
  int samplesLeft = numSamples;
  float *out = sampleBuffer->getWritePointer(0) + prePad;
  while (samplesLeft > 0)
  {
    int samplesToRead = bufferSize;
    if (samplesToRead > samplesLeft)
    {
      samplesToRead = samplesLeft;
    }
    file_->read(buffer, samplesToRead * sizeof(short));

    int samplesToConvert = samplesToRead;
    short *in = buffer;
    for (; samplesToConvert > 0; --samplesToConvert)
    {
      *out++ = *in++ / 32767.0f;
    }

    samplesLeft -= samplesToRead;

    if (progressVar)
    {
      *progressVar = static_cast<float>(numSamples - samplesLeft) / numSamples * (useSm24 ? 0.8 : 1.0);
    }
    if (thread && thread->threadShouldExit())
    {
      delete[] buffer;
      delete sampleBuffer;
      return nullptr;
    }
  }
  delete[] buffer;

  // If sm24 chunk is present, add the low 8 bits of precision to each sample.
  // The smpl chunk provides the upper 16 bits and sm24 provides the next 8 bits,
  // giving 24-bit resolution per the SF2.04 specification.
  if (useSm24)
  {
    file_->setPosition(sm24Chunk.start);
    juce::HeapBlock<sfzero::byte> sm24Buffer(bufferSize);
    samplesLeft = numSamples;
    out = sampleBuffer->getWritePointer(0) + prePad;
    while (samplesLeft > 0)
    {
      int samplesToRead = juce::jmin(bufferSize, samplesLeft);
      file_->read(sm24Buffer.getData(), samplesToRead);

      for (int i = 0; i < samplesToRead; ++i)
      {
        // The 16-bit value occupies bits 8-23 of the 24-bit sample.
        // sm24 provides bit 0-7. Combine: the low byte contributes
        // 1/256th of one 16-bit LSB to the float value.
        out[i] += static_cast<float>(sm24Buffer[i]) / (32767.0f * 256.0f);
      }

      out += samplesToRead;
      samplesLeft -= samplesToRead;

      if (progressVar)
      {
        *progressVar = 0.8 + 0.2 * static_cast<float>(numSamples - samplesLeft) / numSamples;
      }
      if (thread && thread->threadShouldExit())
      {
        delete sampleBuffer;
        return nullptr;
      }
    }
  }

  // Fix loop boundaries: copy wrapped loop data past each sample's loopEnd
  // so the Akai SINC resampler reads correct data instead of zeros.
  // Parse the shdr chunk to find loop points for each sample.
  file_->setPosition(0);
  riffChunk.readFrom(file_.get());
  while (file_->getPosition() < riffChunk.end())
  {
    chunk.readFrom(file_.get());
    if (FourCCEquals(chunk.id, "pdta"))
    {
      juce::int64 pdtaEnd = chunk.end();
      while (file_->getPosition() < pdtaEnd)
      {
        sfzero::RIFFChunk subChunk;
        subChunk.readFrom(file_.get());
        if (FourCCEquals(subChunk.id, "shdr"))
        {
          int numHeaders = static_cast<int>(subChunk.size) / sfzero::SF2::shdr::sizeInFile;
          float *sampleData = sampleBuffer->getWritePointer(0) + prePad;
          for (int i = 0; i < numHeaders; ++i)
          {
            sfzero::SF2::shdr header;
            header.readFrom(file_.get());
            if (header.startLoop >= header.endLoop)
              continue;
            if (header.endLoop >= static_cast<sfzero::dword>(numSamples))
              continue;

            int loopStart = static_cast<int>(header.startLoop);
            int loopEnd = static_cast<int>(header.endLoop);
            int loopLen = loopEnd - loopStart;
            if (loopLen < kLoopPadding)
              continue;

            // Copy loop-start samples to positions after loopEnd so the
            // interpolation filter reads wrapped loop data instead of zeros.
            int copyLen = juce::jmin(kLoopPadding, numSamples - loopEnd);
            for (int j = 0; j < copyLen; ++j)
              sampleData[loopEnd + j] = sampleData[loopStart + (j % loopLen)];
          }
          break;
        }
        subChunk.seekAfter(file_.get());
      }
      break;
    }
    chunk.seekAfter(file_.get());
  }

  if (progressVar)
  {
    *progressVar = 1.0;
  }

  return sampleBuffer;
}

void sfzero::SF2Reader::addGeneratorToRegion(sfzero::word genOper, sfzero::SF2::genAmountType *amount, sfzero::Region *region)
{
  switch (genOper)
  {
  case sfzero::SF2Generator::startAddrsOffset:
    region->offset += amount->shortAmount;
    break;

  case sfzero::SF2Generator::endAddrsOffset:
    region->end += amount->shortAmount;
    break;

  case sfzero::SF2Generator::startloopAddrsOffset:
    region->loop_start += amount->shortAmount;
    break;

  case sfzero::SF2Generator::endloopAddrsOffset:
    region->loop_end += amount->shortAmount;
    break;

  case sfzero::SF2Generator::startAddrsCoarseOffset:
    region->offset += amount->shortAmount * 32768;
    break;

  case sfzero::SF2Generator::endAddrsCoarseOffset:
    region->end += amount->shortAmount * 32768;
    break;

  case sfzero::SF2Generator::pan:
    region->pan = amount->shortAmount * (2.0f / 10.0f);
    break;

  case sfzero::SF2Generator::delayVolEnv:
    region->ampeg.delay = amount->shortAmount;
    break;

  case sfzero::SF2Generator::attackVolEnv:
    region->ampeg.attack = amount->shortAmount;
    break;

  case sfzero::SF2Generator::holdVolEnv:
    region->ampeg.hold = amount->shortAmount;
    break;

  case sfzero::SF2Generator::decayVolEnv:
    region->ampeg.decay = amount->shortAmount;
    break;

  case sfzero::SF2Generator::sustainVolEnv:
    region->ampeg.sustain = amount->shortAmount;
    break;

  case sfzero::SF2Generator::releaseVolEnv:
    region->ampeg.release = amount->shortAmount;
    break;

  case sfzero::SF2Generator::keyRange:
    region->lokey = amount->range.lo;
    region->hikey = amount->range.hi;
    break;

  case sfzero::SF2Generator::velRange:
    region->lovel = amount->range.lo;
    region->hivel = amount->range.hi;
    break;

  case sfzero::SF2Generator::startloopAddrsCoarseOffset:
    region->loop_start += amount->shortAmount * 32768;
    break;

  case sfzero::SF2Generator::initialAttenuation:
    // The spec says "initialAttenuation" is in centibels.  But everyone
    // seems to treat it as millibels.
    region->volume += -amount->shortAmount / 100.0f;
    break;

  case sfzero::SF2Generator::endloopAddrsCoarseOffset:
    region->loop_end += amount->shortAmount * 32768;
    break;

  case sfzero::SF2Generator::coarseTune:
    region->transpose += amount->shortAmount;
    break;

  case sfzero::SF2Generator::fineTune:
    region->tune += amount->shortAmount;
    break;

  case sfzero::SF2Generator::sampleModes:
  {
    sfzero::Region::LoopMode loopModes[] = {sfzero::Region::no_loop, sfzero::Region::loop_continuous, sfzero::Region::no_loop,
                                            sfzero::Region::loop_sustain};
    region->loop_mode = loopModes[amount->wordAmount & 0x03];
  }
  break;

  case sfzero::SF2Generator::scaleTuning:
    region->pitch_keytrack = amount->shortAmount;
    break;

  case sfzero::SF2Generator::exclusiveClass:
    region->off_by = amount->wordAmount;
    region->group = static_cast<int>(region->off_by);
    break;

  case sfzero::SF2Generator::overridingRootKey:
    region->pitch_keycenter = amount->shortAmount;
    break;

  case sfzero::SF2Generator::endOper:
    // Ignore.
    break;

  case sfzero::SF2Generator::modLfoToPitch:
  case sfzero::SF2Generator::vibLfoToPitch:
  case sfzero::SF2Generator::modEnvToPitch:
  case sfzero::SF2Generator::initialFilterFc:
  case sfzero::SF2Generator::initialFilterQ:
  case sfzero::SF2Generator::modLfoToFilterFc:
  case sfzero::SF2Generator::modEnvToFilterFc:
  case sfzero::SF2Generator::modLfoToVolume:
  case sfzero::SF2Generator::unused1:
  case sfzero::SF2Generator::chorusEffectsSend:
  case sfzero::SF2Generator::reverbEffectsSend:
  case sfzero::SF2Generator::unused2:
  case sfzero::SF2Generator::unused3:
  case sfzero::SF2Generator::unused4:
  case sfzero::SF2Generator::delayModLFO:
  case sfzero::SF2Generator::freqModLFO:
  case sfzero::SF2Generator::delayVibLFO:
  case sfzero::SF2Generator::freqVibLFO:
  case sfzero::SF2Generator::delayModEnv:
  case sfzero::SF2Generator::attackModEnv:
  case sfzero::SF2Generator::holdModEnv:
  case sfzero::SF2Generator::decayModEnv:
  case sfzero::SF2Generator::sustainModEnv:
  case sfzero::SF2Generator::releaseModEnv:
  case sfzero::SF2Generator::keynumToModEnvHold:
  case sfzero::SF2Generator::keynumToModEnvDecay:
  case sfzero::SF2Generator::keynumToVolEnvHold:
  case sfzero::SF2Generator::keynumToVolEnvDecay:
  case sfzero::SF2Generator::instrument:
  // Only allowed in certain places, where we already special-case it.
  case sfzero::SF2Generator::reserved1:
  case sfzero::SF2Generator::keynum:
  case sfzero::SF2Generator::velocity:
  case sfzero::SF2Generator::reserved2:
  case sfzero::SF2Generator::sampleID:
  // Only allowed in certain places, where we already special-case it.
  case sfzero::SF2Generator::reserved3:
  case sfzero::SF2Generator::unused5:
  {
    const sfzero::SF2Generator *generator = sfzero::GeneratorFor(static_cast<int>(genOper));
    sound_->addUnsupportedOpcode(generator->name);
  }
  break;
  }
}
