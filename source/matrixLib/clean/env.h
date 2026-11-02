#pragma once

#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// Envelopes and ramps (INTERFACE.md 4.3, 4.5), fitted to measurements env/ and ramp/.
	// The unit updates a voice's modulators once per voice block, whatever the time since the
	// last one: a tick of VintageMinUs or more is one step (Vintage), shorter ticks run a continuous
	// version scaled by _dtUs / StepUs (Smooth).
	namespace envCurve
	{
		constexpr int32_t EnvFull = 64512;    // 63 * 1024, sustain s = s * 1024
		constexpr int32_t RampFull = 65535;
		constexpr double StepUs = 12255.0;    // refresh cycle with one note held (timing/)
		constexpr double VintageMinUs = 2000.0;

		int rate(int _value);                 // stage multiplier r, attack step r * r
		int delaySteps(int _value);           // refresh steps spent in the delay stage
		int32_t sustainLevel(int _value);
		int32_t fallStep(int32_t _distance, int _r);   // decay/release step towards the target
		int rampStep(int _value);

		// amplitude multiplier 0..255: amp 0-62 = 4 * amp, 63 = 255, plus velocity and matrix
		int ampMultiplier(int _amp, uint8_t _velocity, int _byVelocity, int _matrixMul);
		// (level * ampMul) >> 10: full level, amplitude 63 = 63 * 255 = 16065 (the vcf/ and vca/ grids)
		Mod output(int32_t _level, int _ampMul);
	}

	class Envelope
	{
	public:
		enum class Stage : uint8_t { Idle, Delay, Attack, Decay, Sustain, Release };

		struct Params
		{
			int delay = 0, attack = 0, decay = 0, release = 0;   // 0-63 incl. matrix offsets
			int sustain = 0;                                      // 0-63
			// matrix: stage rate offsets in rate units (positive = slower), delay offset in steps
			int delayMod = 0, attackMod = 0, decayMod = 0, releaseMod = 0;
			uint8_t trigger = 0, lfoTrigger = 0, mode = 0;
		};

		void reset();
		// _legato: the voice already had a key (retarget)
		void gateOn(const Params& _p, bool _legato);
		void gateOff(const Params& _p);
		void externalGate(const Params& _p, bool _down);
		// LFO1 of the voice: cycle start and half cycle
		// true when the stage changed (the Core steps it again in the same block)
		bool lfoEvent(const Params& _p, bool _wrapped, bool _halfCrossed, bool _keyHeld);
		// cold boot leftover (env3/, env4/): with LFO trigger 2 an envelope that has had no LFO1 event
		// since the boot jumps to sustain a fixed time after the patch load (Core::bootSustainUs)
		void bootSustain(const Params& _p);
		void setBootArmed(bool _armed) { m_bootArmed = _armed; }
		bool bootArmed() const { return m_bootArmed; }
		// _vintage: one step per call; otherwise the continuous version for any _dtUs
		Mod tick(const Params& _p, double _dtUs, bool _vintage);
		// mode from _dtUs (VintageMinUs or more = Vintage)
		Mod tick(const Params& _p, const double _dtUs) { return tick(_p, _dtUs, _dtUs >= envCurve::VintageMinUs); }

		Stage stage() const { return m_stage; }
		int32_t level() const { return m_level; }   // 0..EnvFull
		Mod value() const;

	private:
		void start(const Params& _p);
		void release();
		void step(const Params& _p);
		void advance(const Params& _p, double _steps);
		void endDecay(const Params& _p);

		Stage m_stage = Stage::Idle;
		int32_t m_level = 0;
		double m_levelF = 0.0;
		double m_delayLeft = 0.0;
		bool m_gate = false;
		bool m_bootArmed = false;
	};

	class Ramp
	{
	public:
		void reset();
		void keyTrigger(RampTrigger _t, bool _legato);
		void externalTrigger(RampTrigger _t, bool _keyHeld);
		Mod tick(int _rate, double _dtUs, bool _vintage);
		Mod tick(const int _rate, const double _dtUs) { return tick(_rate, _dtUs, _dtUs >= envCurve::VintageMinUs); }
		int32_t level() const { return m_level; }   // 0..RampFull

	private:
		int32_t m_level = envCurve::RampFull;
		double m_levelF = envCurve::RampFull;
		bool m_running = false;
	};
}
