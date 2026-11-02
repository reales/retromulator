#pragma once

#include <cstdint>

#include "types.h"

namespace matrixClean::dco
{
	// DCO pitch: 1/256 semitone, 0 = key 24 at frequency 0 (lowest table entry), signed 16 bit range
	constexpr int32_t PitchUnit = 256;
	constexpr int32_t IdleKey = 57;          // pitch of a voice that has not played yet
	constexpr int32_t FixedKey = 24;         // DCO2 with keyboard tracking off

	// integer part of the pitch from key (MIDI note after transpose) and frequency 0-63
	int32_t keyPitch(int32_t _key256, int _freq);
	int32_t floorPitch(int32_t _pitch);      // before tune/detune: 0 below the table, wraps below -82
	int32_t tuneFine(int _tune);             // master tune -31..31
	int32_t detuneFine(int _detune);         // DCO2 detune -31..31
	int32_t unisonSpread(uint32_t _voice);   // DCO2 offset per voice in unison
	int32_t bendFine(int32_t _bend, int _range);   // 14 bit bend, range in semitones

	// [R-PITCH] notes.csv: exact on every row
	uint16_t count(int32_t _pitch);
	bool rangeHigh(int32_t _pitch);

	// CV codes: 13 bit (bit 12 slow) | range << 13
	uint32_t waveshape(int32_t _pitch, int32_t _shape256);
	// shape change in 1/256 step of one matrix bus: source level -256..255, amount -63..63
	int32_t shapeRoute(int _level, int _amount);
	int32_t shapeTop(int32_t _sum);   // the shape sum past the top
	uint32_t pulseWidth(int32_t _pw512, int32_t _shape256, bool _on);   // pulse width in 1/512 steps
	uint32_t balance(int32_t _mix256);

	// patch wave enables (B13, B18) and sync (B25)
	uint8_t controlLatch(uint8_t _wave1, uint8_t _wave2, uint8_t _sync, bool _mute);
	bool noiseLatch(uint8_t _wave2);
	bool pulseOn(uint32_t _dco, uint8_t _wave);

	constexpr uint8_t NormalMode[2] = {2, 3};

	// restart after a note on or a jump: count read back at the latch (_read), period in effect
	// (_old), new count; returns the phase count written before the new count, 0 = write the new
	// count directly
	uint16_t phaseCount(uint32_t _dco, uint16_t _old, uint16_t _read, uint16_t _new);
}
