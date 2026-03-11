/*
BEGIN_JUCE_MODULE_DECLARATION
    ID:               SFZero
    vendor:           discoDSP
    version:          3.0.0
    name:             SFZero sample player
    description:      SFZ/SF2/ZBP/ZBB sample player with 8-point sinc resampler, MIDI CC support, pitch bend, mod wheel vibrato, sustain pedal. Forked from github.com/stevefolta/SFZero, JUCE module by Leo Olivers, extended by discoDSP.
    website:          https://www.discodsp.com
    dependencies:     juce_gui_basics, juce_audio_basics, juce_audio_processors
    license:          MIT
END_JUCE_MODULE_DECLARATION
*/

#ifndef INCLUDED_SFZERO_H
#define INCLUDED_SFZERO_H

#include "sfzero/RIFF.h"
#include "sfzero/SF2.h"
#include "sfzero/SF2Generator.h"
#include "sfzero/SF2Reader.h"
#include "sfzero/SF2Sound.h"
#include "sfzero/SF2WinTypes.h"
#include "sfzero/SFZCommon.h"
#include "sfzero/SFZDebug.h"
#include "sfzero/SFZEG.h"
#include "sfzero/SFZReader.h"
#include "sfzero/SFZRegion.h"
#include "sfzero/SFZSample.h"
#include "sfzero/SFZSound.h"
#include "sfzero/SFZSynth.h"
#include "sfzero/SFZVoice.h"
#include "sfzero/ZBPReader.h"
#include "sfzero/ZBPSound.h"


#endif   // INCLUDED_SFZERO_H

