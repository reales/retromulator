/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#include "SFZDebug.h"
#include "SFZAkaiSincResampler.h"
#include "SFZRegion.h"
#include "SFZSample.h"
#include "SFZSound.h"
#include "SFZVoice.h"
#include <math.h>

static const float globalGain = -1.0;

sfzero::Voice::Voice()
    : region_(nullptr), trigger_(0), curMidiNote_(0), curPitchWheel_(0), pitchRatio_(0), noteGainLeft_(0), noteGainRight_(0),
      sourceSamplePosition_(0), sampleEnd_(0), loopStart_(0), loopEnd_(0),
      ccVolume_(100.0f / 127.0f), ccExpression_(1.0f), ccPan_(0.0f), ccModWheel_(0),
      numLoops_(0), curVelocity_(0)
{
  ampeg_.setExponentialDecay(true);
}

sfzero::Voice::~Voice() {}

bool sfzero::Voice::canPlaySound(juce::SynthesiserSound *sound) { return dynamic_cast<sfzero::Sound *>(sound) != nullptr; }

void sfzero::Voice::startNote(int midiNoteNumber, float floatVelocity, juce::SynthesiserSound *soundIn,
                              int currentPitchWheelPosition)
{
  sfzero::Sound *sound = dynamic_cast<sfzero::Sound *>(soundIn);

  if (sound == nullptr)
  {
    killNote();
    return;
  }

  int velocity = juce::roundToInt(floatVelocity * 127.0f);
  curVelocity_ = velocity;
  if (region_ == nullptr)
  {
    region_ = sound->getRegionFor(midiNoteNumber, velocity);
  }
  if ((region_ == nullptr) || (region_->sample == nullptr) || (region_->sample->getBuffer() == nullptr))
  {
    killNote();
    return;
  }
  if (region_->negative_end)
  {
    killNote();
    return;
  }

  // Pitch.
  curMidiNote_ = midiNoteNumber;
  curPitchWheel_ = currentPitchWheelPosition;
  calcPitchRatio();

  // Gain.
  double noteGainDB = globalGain + region_->volume;
  // Thanks to <http:://www.drealm.info/sfz/plj-sfz.xhtml> for explaining the
  // velocity curve in a way that I could understand, although they mean
  // "log10" when they say "log".
  double velocityGainDB = -20.0 * log10((127.0 * 127.0) / (velocity * velocity));
  velocityGainDB *= region_->amp_veltrack / 100.0;
  noteGainDB += velocityGainDB;
  noteGainLeft_ = noteGainRight_ = static_cast<float>(juce::Decibels::decibelsToGain(noteGainDB));
  // The SFZ spec is silent about the pan curve, but a 3dB pan law seems
  // common.  This sqrt() curve matches what Dimension LE does; Alchemy Free
  // seems closer to sin(adjustedPan * pi/2).
  double adjustedPan = (region_->pan + 100.0) / 200.0;
  noteGainLeft_ *= static_cast<float>(sqrt(1.0 - adjustedPan));
  noteGainRight_ *= static_cast<float>(sqrt(adjustedPan));
  ampeg_.setExponentialDecay(!region_->linearEnvelope);
  ampeg_.startNote(&region_->ampeg, floatVelocity, getSampleRate(), &region_->ampeg_veltrack);

  // Offset/end.
  sourceSamplePosition_ = static_cast<double>(region_->offset);
  sampleEnd_ = region_->sample->getSampleLength();
  if ((region_->end > 0) && (region_->end < sampleEnd_))
  {
    sampleEnd_ = region_->end + 1;
  }

  // Loop.
  loopStart_ = loopEnd_ = 0;
  sfzero::Region::LoopMode loopMode = region_->loop_mode;
  if (loopMode == sfzero::Region::sample_loop)
  {
    if (region_->sample->getLoopStart() < region_->sample->getLoopEnd())
    {
      loopMode = sfzero::Region::loop_continuous;
    }
    else
    {
      loopMode = sfzero::Region::no_loop;
    }
  }
  if ((loopMode != sfzero::Region::no_loop) && (loopMode != sfzero::Region::one_shot))
  {
    if (region_->loop_start < region_->loop_end)
    {
      loopStart_ = region_->loop_start;
      loopEnd_ = region_->loop_end;
    }
    else
    {
      loopStart_ = region_->sample->getLoopStart();
      loopEnd_ = region_->sample->getLoopEnd();
    }
  }
  numLoops_ = 0;
}

void sfzero::Voice::stopNote(float /*velocity*/, bool allowTailOff)
{
  if (!allowTailOff || (region_ == nullptr))
  {
    killNote();
    return;
  }

  if (region_->loop_mode != sfzero::Region::one_shot)
  {
    ampeg_.noteOff();
  }
  if (region_->loop_mode == sfzero::Region::loop_sustain)
  {
    // Continue playing, but stop looping.
    loopEnd_ = loopStart_;
  }
}

void sfzero::Voice::stopNoteForGroup()
{
  if (region_->off_mode == sfzero::Region::fast)
  {
    ampeg_.fastRelease();
  }
  else
  {
    ampeg_.noteOff();
  }
}

void sfzero::Voice::stopNoteQuick() { ampeg_.fastRelease(); }
void sfzero::Voice::pitchWheelMoved(int newValue)
{
  if (region_ == nullptr)
  {
    return;
  }

  curPitchWheel_ = newValue;
  calcPitchRatio();
}

void sfzero::Voice::controllerMoved(int controllerNumber, int newValue)
{
  switch (controllerNumber)
  {
    case 1:   // Mod wheel
      ccModWheel_ = newValue;
      break;
    case 7:   // Channel volume
      ccVolume_ = newValue / 127.0f;
      break;
    case 10:  // Pan
      ccPan_ = (newValue - 64) / 64.0f; // -1..+1
      break;
    case 11:  // Expression
      ccExpression_ = newValue / 127.0f;
      break;
    default:
      break;
  }
}
void sfzero::Voice::renderNextBlock(juce::AudioSampleBuffer &outputBuffer, int startSample, int numSamples)
{
  if (region_ == nullptr)
  {
    return;
  }

  juce::AudioSampleBuffer *buffer = region_->sample->getBuffer();
  const float *inL = buffer->getReadPointer(0, 0);
  const float *inR = buffer->getNumChannels() > 1 ? buffer->getReadPointer(1, 0) : nullptr;

  float *outL = outputBuffer.getWritePointer(0, startSample);
  float *outR = outputBuffer.getNumChannels() > 1 ? outputBuffer.getWritePointer(1, startSample) : nullptr;

  int bufferNumSamples = buffer->getNumSamples();
  int prePad = region_->sample->getPrePad();

  // ── CC-based gain & pan modifiers ────────────────────────────────────────
  float ccGain = ccVolume_ * ccExpression_;
  // CC10 pan combined with region pan: region pan is baked into noteGainL/R,
  // CC pan shifts the stereo balance on top.
  float ccPanL = 1.0f, ccPanR = 1.0f;
  if (ccPan_ < 0.0f)
    ccPanR = 1.0f + ccPan_; // attenuate right
  else if (ccPan_ > 0.0f)
    ccPanL = 1.0f - ccPan_; // attenuate left

  // ── Mod-wheel vibrato ──────────────────────────────────────────────────
  // Depth: up to ±50 cents at full mod wheel (standard GM-like vibrato).
  // Rate: ~5.5 Hz.
  const double vibratoDepthSemitones = (ccModWheel_ / 127.0) * 0.5; // 50 cents max
  const double vibratoRateHz = 5.5;
  const double vibratoPhaseInc = vibratoRateHz * 2.0 * juce::MathConstants<double>::pi / getSampleRate();

  // Cache some values, to give them at least some chance of ending up in
  // registers.
  double sourceSamplePosition = this->sourceSamplePosition_;
  float ampegGain = ampeg_.getLevel();
  float ampegSlope = ampeg_.getSlope();
  int samplesUntilNextAmpSegment = ampeg_.getSamplesUntilNextSegment();
  bool ampSegmentIsExponential = ampeg_.getSegmentIsExponential();
  double loopStart = static_cast<double>(this->loopStart_);
  double loopEnd = static_cast<double>(this->loopEnd_);
  float sampleEnd = static_cast<float>(this->sampleEnd_);
  bool looping = (loopStart < loopEnd);
  double loopLength = loopEnd - loopStart;

  // Vibrato phase — use sample position as a simple phase source so it
  // doesn't need an extra member variable.
  double vibratoPhase = sourceSamplePosition * vibratoPhaseInc;

  while (--numSamples >= 0)
  {
    // ── Per-sample pitch ratio with vibrato ──────────────────────────────
    double curPitchRatio = pitchRatio_;
    if (ccModWheel_ > 0)
    {
      double vibrato = sin(vibratoPhase) * vibratoDepthSemitones;
      curPitchRatio *= pow(2.0, vibrato / 12.0);
      vibratoPhase += vibratoPhaseInc;
    }

    // 8-point windowed-sinc interpolation with anti-aliasing.
    // Offset by prePad since sample data starts at buffer[prePad].
    // Loop boundaries are handled at load time: the sample buffer has
    // wrapped loop data past loopEnd so the filter reads correct samples.
    double bufPos = sourceSamplePosition + prePad;
    float l = sfzero::akaiSincInterpolate(inL, bufPos, bufferNumSamples, curPitchRatio);
    float r = inR ? sfzero::akaiSincInterpolate(inR, bufPos, bufferNumSamples, curPitchRatio) : l;

    float gainLeft = noteGainLeft_ * ampegGain * ccGain * ccPanL;
    float gainRight = noteGainRight_ * ampegGain * ccGain * ccPanR;
    l *= gainLeft;
    r *= gainRight;
    // Shouldn't we dither here?

    if (outR)
    {
      *outL++ += l;
      *outR++ += r;
    }
    else
    {
      *outL++ += (l + r) * 0.5f;
    }

    // Next sample.
    sourceSamplePosition += curPitchRatio;
    if (looping && (sourceSamplePosition > loopEnd))
    {
      sourceSamplePosition -= loopLength;
      numLoops_ += 1;
    }

    // Update EG.
    if (ampSegmentIsExponential)
    {
      ampegGain *= ampegSlope;
    }
    else
    {
      ampegGain += ampegSlope;
    }
    if (--samplesUntilNextAmpSegment < 0)
    {
      ampeg_.setLevel(ampegGain);
      ampeg_.nextSegment();
      ampegGain = ampeg_.getLevel();
      ampegSlope = ampeg_.getSlope();
      samplesUntilNextAmpSegment = ampeg_.getSamplesUntilNextSegment();
      ampSegmentIsExponential = ampeg_.getSegmentIsExponential();
    }

    if ((sourceSamplePosition >= sampleEnd) || ampeg_.isDone())
    {
      killNote();
      break;
    }
  }

  this->sourceSamplePosition_ = sourceSamplePosition;
  ampeg_.setLevel(ampegGain);
  ampeg_.setSamplesUntilNextSegment(samplesUntilNextAmpSegment);
}

bool sfzero::Voice::isPlayingNoteDown() { return region_ && region_->trigger != sfzero::Region::release; }

bool sfzero::Voice::isPlayingOneShot() { return region_ && region_->loop_mode == sfzero::Region::one_shot; }

int sfzero::Voice::getGroup() { return region_ ? region_->group : 0; }

juce::uint64 sfzero::Voice::getOffBy() { return region_ ? region_->off_by : 0; }

void sfzero::Voice::setRegion(sfzero::Region *nextRegion) { region_ = nextRegion; }

juce::String sfzero::Voice::infoString()
{
  const char *egSegmentNames[] = {"delay", "attack", "hold", "decay", "sustain", "release", "done"};

  const static int numEGSegments(sizeof(egSegmentNames) / sizeof(egSegmentNames[0]));

  const char *egSegmentName = "-Invalid-";
  int egSegmentIndex = ampeg_.segmentIndex();
  if ((egSegmentIndex >= 0) && (egSegmentIndex < numEGSegments))
  {
    egSegmentName = egSegmentNames[egSegmentIndex];
  }

  juce::String info;
  info << "note: " << curMidiNote_ << ", vel: " << curVelocity_ << ", pan: " << region_->pan << ", eg: " << egSegmentName
       << ", loops: " << numLoops_;
  return info;
}

void sfzero::Voice::calcPitchRatio()
{
  double note = curMidiNote_;

  note += region_->transpose;
  note += region_->tune / 100.0;

  double adjustedPitch = region_->pitch_keycenter + (note - region_->pitch_keycenter) * (region_->pitch_keytrack / 100.0);
  if (curPitchWheel_ != 8192)
  {
    double wheel = ((2.0 * curPitchWheel_ / 16383.0) - 1.0);
    if (wheel > 0)
    {
      adjustedPitch += wheel * region_->bend_up / 100.0;
    }
    else
    {
      adjustedPitch += wheel * region_->bend_down / -100.0;
    }
  }
  double targetFreq = fractionalMidiNoteInHz(adjustedPitch);
  double naturalFreq = juce::MidiMessage::getMidiNoteInHertz(region_->pitch_keycenter);
  pitchRatio_ = (targetFreq * region_->sample->getSampleRate()) / (naturalFreq * getSampleRate());
}

void sfzero::Voice::killNote()
{
  region_ = nullptr;
  clearCurrentNote();
}

double sfzero::Voice::fractionalMidiNoteInHz(double note, double freqOfA)
{
  // Like MidiMessage::getMidiNoteInHertz(), but with a float note.
  note -= 69;
  // Now 0 = A
  return freqOfA * pow(2.0, note / 12.0);
}
