#pragma once

#include <cstdint>

namespace matrixLib
{
	// One CEM3396 voice: two waveform converters, balance, 4-pole low-pass, log and lin VCA.
	// All control inputs are pin voltages.
	class Cem3396
	{
	public:
		struct Controls
		{
			float periodA = 0.0f;     // timer period in seconds, 0 = stopped; only centres the pulse
			float periodB = 0.0f;
			float rtCtA = 0.0f;       // conversion resistance times timing capacitor
			float rtCtB = 0.0f;
			float wsA = 0.0f;         // waveshape (V/I converter) inputs
			float wsB = 0.0f;
			float pwA = 0.0f;         // pulse width comparator inputs
			float pwB = 0.0f;
			bool slopedA = true;      // wave select, from the quad level driver
			bool slopedB = false;
			bool noiseB = false;      // converter B capacitor driven by the noise source
			float balance = 0.0f;
			float freq = 0.0f;
			float resonance = 0.0f;
			float mod = 0.0f;
			float logGain = 0.0f;
			float linGain = 0.0f;
		};

		// capacitor discharge pulses inside one sample, as fractions of it in ascending order
		struct Discharges
		{
			static constexpr uint32_t Max = 24;
			float a[Max];
			float b[Max];
			uint32_t countA = 0;
			uint32_t countB = 0;
		};

		void setSamplerate(float _rate);
		void reset();

		// renders one sample
		float process(const Controls& _in, const Discharges& _discharges, float _noise);

	private:
		struct Converter
		{
			double volts = 0.0;       // timing capacitor
			bool pulseHigh = false;   // pulse comparator output
		};

		float filter(float _in, float _cutoff, float _k);

		float m_rate = 48000.0f;
		float m_invRate = 1.0f / 48000.0f;

		Converter m_a, m_b;

		// converter outputs held back two samples for the pre-event half of each BLEP/BLAMP
		// residual: samples n-2, n-1, n
		float m_histSlopedA[3] = {};
		float m_histSlopedB[3] = {};
		float m_histPulseA[3] = {};
		float m_histPulseB[3] = {};

		float m_s[4] = {};
		float m_damp = 0.7f;
		float m_dampInv = 1.0f / 0.7f;

		float m_logGain = 0.0f;
		float m_linGain = 0.0f;
		bool m_smoothInit = false;
		float m_cvCoeff = 0.02f;

		float m_acIn = 0.0f;
		float m_acOut = 0.0f;
		float m_acCoeff = 0.999f;
	};
}
