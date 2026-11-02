#pragma once

#include <cstdint>
#include <string>

namespace matrixClean
{
	constexpr uint32_t VoiceCount = 6;
	constexpr uint32_t BusClock = 2000000;

	// modulation values: unipolar 0..ModFull, bipolar -ModFull..ModFull
	using Mod = int32_t;
	constexpr Mod ModFull = 1 << 14;

	// INTERFACE.md 4.8 table 2
	enum class Source : uint8_t
	{
		None, Env1, Env2, Env3, Lfo1, Lfo2, Vibrato, Ramp1, Ramp2, Keyboard, Portamento, Tracking,
		KeyGate, Velocity, ReleaseVelocity, Pressure, Pedal1, Pedal2, Lever1, Lever2, Lever3,
		Count
	};

	// INTERFACE.md 4.8 table 3
	enum class Dest : uint8_t
	{
		None, Dco1Freq, Dco1Pw, Dco1Shape, Dco2Freq, Dco2Pw, Dco2Shape, Mix, VcfFm, VcfFreq, VcfRes,
		Vca1, Vca2,
		Env1Delay, Env1Attack, Env1Decay, Env1Release, Env1Amp,
		Env2Delay, Env2Attack, Env2Decay, Env2Release, Env2Amp,
		Env3Delay, Env3Attack, Env3Decay, Env3Release, Env3Amp,
		Lfo1Speed, Lfo1Amp, Lfo2Speed, Lfo2Amp, PortaTime,
		Count
	};

	enum class KeyboardMode : uint8_t { Reassign, Rotate, Unison, ReassignRob };
	enum class LfoWave : uint8_t { Triangle, UpSaw, DownSaw, Square, Random, Noise, Sampled };
	enum class LfoTrigger : uint8_t { None, Single, Multi, External };
	enum class RampTrigger : uint8_t { Single, Multi, External, GatedExternal };
	enum class LagMode : uint8_t { ConstSpeed, ConstTime, Expo1, Expo2 };

	namespace envTrig { constexpr uint8_t Reset = 1, Multi = 2, External = 4; }
	namespace envLfo { constexpr uint8_t Gated = 1, LfoTrigger = 2; }
	namespace envMode { constexpr uint8_t Dadr = 1, FreeRun = 2; }

	// CV index inside a voice's 8 channels (INTERFACE.md 2.1)
	enum CvIndex : uint8_t { CvWsA, CvFreq, CvLog, CvPwA, CvLin, CvPwB, CvMod, CvWsB };
	constexpr uint32_t voiceCv(uint32_t _voice, CvIndex _cv) { return _voice * 8 + _cv; }
	constexpr uint32_t balanceCv(uint32_t _voice) { return 48 + _voice; }
	constexpr uint32_t resonanceCv(uint32_t _voice) { return 54 + _voice; }

	constexpr uint8_t RangeA7 = 1;
	constexpr uint8_t RangeA8 = 2;
	constexpr uint16_t CvSlow = 0x1000;

	constexpr uint8_t CtlWaveMask = 0x30;
	constexpr uint8_t CtlSync1Off = 0x08;
	constexpr uint8_t CtlSync2Off = 0x04;
	constexpr uint8_t CtlMute = 0x02;

	// one hardware write, same targets as the write log (ab/voiceboard/README.md)
	enum class Target : uint8_t { Cv, Mux, Pit, DcoMode, Dco, Range, Noise, Ctrl, Idle, Latch };

	// Vintage: measured refresh schedule, voice stagger and DAC codes (all A/B runs).
	// Smooth: every voice updated each step, no stagger, no refresh latency.
	enum class OutputMode : uint8_t { Vintage, Smooth };

	struct HwWrite
	{
		double timeUs = 0.0;
		Target target = Target::Cv;
		uint16_t channel = 0;     // cv: 0-63; dcomode/dco/range: voice * 2 + dco; pit: chip * 4 + reg; latch: offset
		uint32_t value = 0;       // cv: code (bit 12 slow) | range << 13
	};

	const char* targetName(Target _t);
	std::string toCsvLine(const HwWrite& _w);
}
