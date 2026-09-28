/**
 * OpenWurli DSP — Electrostatic pickup model (overlap law, 2026-09 revision)
 * Ported from Rust openwurli-dsp 0.9.0 pickup.rs (GPL v3)
 *
 * The reed swings through a slot between two pickup fingers. Its capacitance
 * follows the field-solved overlap law (owPickupTables.h) and is the SOURCE
 * term; the plate network is nearly LTI, a differentiator inside a one-pole
 * lag with its corner at PICKUP_FC.
 */
#pragma once

#include "owPickupTables.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace openWurli
{

static constexpr double C_TOTAL = 240.0e-12;
static constexpr double C2_BASE = 220.0e-12;
static constexpr double FC_REF_C2_GROUND = 880.5;
static constexpr double C_TOTAL_REF = 240.0e-12;

/// Small-signal corner of the pickup network (C-2 returned to ground).
static constexpr double PICKUP_FC = FC_REF_C2_GROUND * (C_TOTAL_REF + C2_BASE) / (C_TOTAL + C2_BASE);
static constexpr double PICKUP_TAU = 1.0 / (2.0 * M_PI * PICKUP_FC);
static constexpr double V_POLARIZING = 147.0;
static constexpr double C_REED0 = 3.0e-12;
static constexpr double C_REED_RATIO = C_REED0 / C_TOTAL;
static constexpr double PICKUP_SENSITIVITY = V_POLARIZING * C_REED0 / C_TOTAL;

/// Vertical rest offset between the reed and the finger band (mm).
static constexpr double PICKUP_REST_OFFSET_MM = 0.085;

/// Normalised capacitance at penetration u (mm), natural cubic spline; held at the end knots.
inline double pickupLawEval(const PickupLawTable& law, double u)
{
	const size_t last = law.n - 2;
	const double pos = std::clamp((u - law.u0) / law.du, 0.0, static_cast<double>(last + 1));
	const size_t i = std::min(static_cast<size_t>(pos), last);
	const double t = pos - static_cast<double>(i);
	const double a = 1.0 - t;
	const double h2 = law.du * law.du / 6.0;
	return a * law.y[i] + t * law.y[i + 1]
	     + ((a * a * a - a) * law.m[i] + (t * t * t - t) * law.m[i + 1]) * h2;
}

inline double pickupLawUMax(const PickupLawTable& law)
{
	return law.u0 + law.du * static_cast<double>(law.n - 1);
}

/// The law for a note's reed: one table per measured reed range (reed 1 = MIDI 33).
inline const PickupLawTable& pickupLawFor(uint8_t midi)
{
	const int reed = std::max(static_cast<int>(midi) - 32, 0);
	if (reed <= 14) return pickupTables::R01_14;
	if (reed <= 20) return pickupTables::R15_20;
	if (reed <= 42) return pickupTables::R21_42;
	return pickupTables::R43_64;
}

/// +1 where the pickup sits above the reed (bass, reeds 1-20), -1 where below (treble).
inline double pickupBandSign(uint8_t midi)
{
	return (std::max(static_cast<int>(midi) - 32, 0) <= 20) ? 1.0 : -1.0;
}

class Pickup
{
public:
	Pickup() = default;

	void init(double sampleRate, uint8_t midi, double amplitudeMm)
	{
		const double dt = 1.0 / sampleRate;
		m_law = &pickupLawFor(midi);
		m_w = 0.0;
		m_sPrev = 0.0;
		m_mPrev = 1.0;
		m_beta = dt / (2.0 * PICKUP_TAU);
		m_bandSign = pickupBandSign(midi);
		m_cRest = pickupLawEval(*m_law, -PICKUP_REST_OFFSET_MM);
		m_amplitudeMm = amplitudeMm;
	}

	void setAmplitudeMm(double amplitudeMm) { m_amplitudeMm = amplitudeMm; }

	/// In-place: reed displacement (model units, positive = upward) → pickup voltage.
	void process(double* buffer, size_t numSamples)
	{
		const double beta = m_beta;
		const double scale = m_bandSign * m_amplitudeMm;
		const PickupLawTable& law = *m_law;

		for (size_t i = 0; i < numSamples; i++)
		{
			const double u = scale * buffer[i] - PICKUP_REST_OFFSET_MM;
			const double s = pickupLawEval(law, u) - m_cRest;

			const double m = 1.0 + C_REED_RATIO * s;
			const double mAvg = 0.5 * (m + m_mPrev);

			m_w = (m_w * (mAvg - beta) - (s - m_sPrev)) / (mAvg + beta);

			m_sPrev = s;
			m_mPrev = m;
			buffer[i] = m_w * PICKUP_SENSITIVITY;
		}
	}

	void reset()
	{
		m_w = 0.0;
		m_sPrev = 0.0;
		m_mPrev = 1.0;
	}

private:
	const PickupLawTable* m_law = &pickupTables::R21_42;
	double m_w = 0.0;
	double m_sPrev = 0.0;
	double m_mPrev = 1.0;
	double m_beta = 0.0;
	double m_bandSign = -1.0;
	double m_cRest = 0.0;
	double m_amplitudeMm = 1.0;
};

} // namespace openWurli
