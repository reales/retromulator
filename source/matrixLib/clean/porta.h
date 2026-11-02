#pragma once

#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// INTERFACE.md 4.7, from measurements/porta. Pitch is kept in 1/512 semitone (note * 512),
	// the output is in 1/256 semitone. One update per refresh cycle.
	class Portamento
	{
	public:
		static constexpr double UpdateUs = 12000.0;   // shortest measured refresh cycle

		// per update coefficient for rate 0-63 (0 = fastest); above 63 (modulated) 641
		static uint16_t coefficient(int _rate);
		// rate offset from velocity (velocitySource) or a matrix bus (source 0-127)
		static int rateOffset(int _source, int _amount);
		static int velocitySource(int _velocity);
		static int modulatedRate(int _rate, int _offset);
		// unison spreads the coefficient per voice, 16 bit wraparound
		static uint16_t unisonCoefficient(uint16_t _coeff, uint32_t _voice);

		void reset(int32_t _pitch256);
		// leftover pitch at the end of the cold boot (porta4/); the target stays the idle key
		static int32_t bootPitch(uint32_t _voice);
		void setBootPitch(int32_t _pitch256, int32_t _startOffset512, uint16_t _bootStep, int32_t _driftRef512)
		{
			m_pitch = clampPitch(_pitch256 * 2);
			m_distance = static_cast<uint16_t>(m_pitch > m_target ? m_pitch - m_target : m_target - m_pitch);
			m_start = clampPitch(m_pitch + _startOffset512);
			m_bootStep = _bootStep;
			m_driftRef = _driftRef512;
		}
		// the first constant time glide after boot measures its distance from this start (porta2/long_*)
		static int32_t bootStartOffset(uint32_t _voice);
		static uint16_t bootStep(uint32_t _voice);
		static int32_t bootDriftRef(uint32_t _voice);
		void setTarget(int32_t _pitch256, bool _glide);
		// one controller update
		void update(uint16_t _coeff, LagMode _mode);
		// Vintage: one update per call once UpdateUs has elapsed; Smooth: one update per UpdateUs
		// of accumulated time, any step size. Returns the output
		int32_t tick(uint16_t _coeff, LagMode _mode, double _dtUs, bool _vintage);
		int32_t tick(const uint16_t _coeff, const LagMode _mode, const double _dtUs) { return tick(_coeff, _mode, _dtUs, _dtUs >= UpdateUs * 0.5); }

		int32_t value() const { return static_cast<int32_t>(m_pitch >> 1); }
		bool gliding() const { return m_pitch != m_target; }

	private:
		static uint16_t clampPitch(int32_t _p) { return static_cast<uint16_t>(_p < 0 ? 0 : (_p > 0xffff ? 0xffff : _p)); }

		uint16_t m_pitch = 57 * 512;
		uint16_t m_target = 57 * 512;
		uint16_t m_start = 0;        // distance reference of the first glide after boot (0 = none)
		int32_t m_driftRef = 0;
		uint16_t m_bootStep = 0;     // latched constant time step of the boot glide
		uint16_t m_distance = 0;     // interval at the start of the glide (constant time mode)
		bool m_first = false;
		double m_acc = 0.0;
	};
}
