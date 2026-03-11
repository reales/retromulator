/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef SFZVOICE_H_INCLUDED
#define SFZVOICE_H_INCLUDED

#include "SFZEG.h"

namespace sfzero
{
struct Region;

class Voice : public juce::SynthesiserVoice
{
public:
  Voice();
  virtual ~Voice();

  bool canPlaySound(juce::SynthesiserSound *sound) override;
  void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound *sound, int currentPitchWheelPosition) override;
  void stopNote(float velocity, bool allowTailOff) override;
  void stopNoteForGroup();
  void stopNoteQuick();
  void pitchWheelMoved(int newValue) override;
  void controllerMoved(int controllerNumber, int newValue) override;
  void renderNextBlock(juce::AudioSampleBuffer &outputBuffer, int startSample, int numSamples) override;
  bool isPlayingNoteDown();
  bool isPlayingOneShot();

  int getGroup();
  juce::uint64 getOffBy();

  // Set the region to be used by the next startNote().
  void setRegion(Region *nextRegion);

  juce::String infoString();

private:
  Region *region_;
  int trigger_;
  int curMidiNote_, curPitchWheel_;
  double pitchRatio_;
  float noteGainLeft_, noteGainRight_;
  double sourceSamplePosition_;
  EG ampeg_;
  juce::int64 sampleEnd_;
  juce::int64 loopStart_, loopEnd_;

  // CC state (applied per-voice from last known values).
  float ccVolume_;      // CC7  – channel volume (0..1)
  float ccExpression_;  // CC11 – expression (0..1)
  float ccPan_;         // CC10 – pan (-1..+1, 0 = centre)
  int   ccModWheel_;    // CC1  – mod wheel (0..127)

  // Info only.
  int numLoops_;
  int curVelocity_;

  void calcPitchRatio();
  void killNote();
  double fractionalMidiNoteInHz(double note, double freqOfA = 440.0);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Voice)
};
}

#endif // SFZVOICE_H_INCLUDED
