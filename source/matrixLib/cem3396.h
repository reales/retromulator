#pragma once

#include <cstdint>

// 1: the voice board renders with the scalar per voice model only (reference path). The SIMD
// path is the default where it is known to match: GCC/Clang vector extensions and MSVC x64 SSE2
// (no FMA contraction, i.e. no /fp:contract or /fp:fast)
#ifndef MATRIX_VOICE_REFERENCE
#if defined(__clang__) || defined(__GNUC__) || defined(_M_X64)
#define MATRIX_VOICE_REFERENCE 0
#else
#define MATRIX_VOICE_REFERENCE 1
#endif
#endif

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

		// Chip parameters for the analog spread model. Defaults are the nominal constants process()
		// uses; only processSpread() reads them
		struct Params
		{
			float f0 = 1008.0f;            // VCF cutoff at 0 V
			float octPerVolt = 1.721f;
			float resScale = 1.0f;         // 4 V / oscillation threshold
			float resLevelDb = 0.0f;       // output level change at full resonance
			float shaperPeak[2] = {2.5f, 2.5f};
			float shaperEnd[2] = {5.0f, 5.0f};
			float wsOffset[2] = {};        // V/I converter input offset, volts
			float pwOffset[2] = {};        // pulse comparator offset, volts
			float linThreshold = 0.0f;     // linear VCA on threshold
			float linFull = 4.5f;          // linear VCA full gain
			float logSlope = 22.0f;        // audio taper VCA, dB/V below 3.3 V
			float balanceSlope = 4.6f;
			float balanceOffset = 0.0f;
			float inputBias = 0.3f;
		};

		Cem3396() { setParams(Params()); }

		void setSamplerate(float _rate);
		void reset();
		void setParams(const Params& _p);
		const Params& getParams() const { return m_p; }

		// renders one sample with the nominal chip
		float process(const Controls& _in, const Discharges& _discharges, float _noise);
		// same with the chip parameters from setParams()
		float processSpread(const Controls& _in, const Discharges& _discharges, float _noise);

	private:
		struct Derived
		{
			float shape[2][6];             // peak, end, rise, fall, kink at peak, kink at end
			float resLevelLog2 = 0.0f;
			float resLevelK = -1.0f;       // resonance of the cached level gain
			float resLevelGain = 1.0f;
			float linInvSpan = 1.0f / 4.5f;
			float logK = 22.0f * 0.05f;
			float biasOut = 0.0f;
			float biasGain = 1.0f;
		};

		template<bool Spread> float processT(const Controls& _in, const Discharges& _discharges, float _noise);
		template<bool Spread> float filterT(float _in, float _cutoff, float _k);

		struct Converter
		{
			double volts = 0.0;       // timing capacitor
			bool pulseHigh = false;   // pulse comparator output
		};

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

		Params m_p;
		Derived m_d;
	};

	// Six CEM3396 voices rendered together, one SIMD lane per voice. Per voice the output is bit
	// identical to Cem3396::process() / processSpread(): same operations in the same order,
	// libm per lane (results of exp, exp2, pow and tan are reused while their input stays the same)
	class Cem3396Bank
	{
	public:
		static constexpr uint32_t Voices = 6;
		static constexpr uint32_t Lanes = 8;

		struct Controls
		{
			float periodA[Lanes] = {};
			float periodB[Lanes] = {};
			float rtCtA[Lanes] = {};
			float rtCtB[Lanes] = {};
			float wsA[Lanes] = {};
			float wsB[Lanes] = {};
			float pwA[Lanes] = {};
			float pwB[Lanes] = {};
			float balance[Lanes] = {};
			float freq[Lanes] = {};
			float resonance[Lanes] = {};
			float mod[Lanes] = {};
			float logGain[Lanes] = {};
			float linGain[Lanes] = {};
			bool slopedA = true;
			bool slopedB = false;
			bool noiseB = false;
		};

		Cem3396Bank();

		void setSamplerate(float _rate);
		void reset();
		void setParams(uint32_t _voice, const Cem3396::Params& _p);

		// one sample of every voice into _out[0..Voices), _discharges per voice
		void process(const Controls& _in, const Cem3396::Discharges* _discharges, float _noise, float* _out);
		void processSpread(const Controls& _in, const Cem3396::Discharges* _discharges, float _noise, float* _out);

	private:
		template<bool Spread> void processT(const Controls& _in, const Cem3396::Discharges* _discharges, float _noise, float* _out);

		struct Memo
		{
			uint32_t in[Lanes];
			float out[Lanes];
		};

		float m_rate = 48000.0f;
		float m_invRate = 1.0f / 48000.0f;
		float m_damp = 0.7f;
		float m_dampInv = 1.0f / 0.7f;
		float m_cvCoeff = 0.02f;
		float m_acCoeff = 0.999f;
		bool m_smoothInit = false;

		double m_volts[2][Lanes] = {};
		int32_t m_pulseHigh[2][Lanes] = {};   // -1 high, 0 low

		// [converter A sloped, B sloped, A pulse, B pulse][sample n-2, n-1, n]
		float m_hist[4][3][Lanes] = {};
		float m_s[4][Lanes] = {};
		float m_logGain[Lanes] = {};
		float m_linGain[Lanes] = {};
		float m_acIn[Lanes] = {};
		float m_acOut[Lanes] = {};

		// chip parameters per lane, spread only
		float m_shape[2][6][Lanes] = {};
		float m_wsOffset[2][Lanes] = {};
		float m_pwOffset[2][Lanes] = {};
		float m_f0[Lanes] = {};
		float m_octPerVolt[Lanes] = {};
		float m_resScale[Lanes] = {};
		float m_balanceSlope[Lanes] = {};
		float m_balanceOffset[Lanes] = {};
		float m_inputBias[Lanes] = {};
		float m_biasOut[Lanes] = {};
		float m_biasGain[Lanes] = {};
		float m_linThreshold[Lanes] = {};
		float m_linInvSpan[Lanes] = {};
		float m_logK[Lanes] = {};
		float m_resLevelLog2[Lanes] = {};
		float m_resLevelK[Lanes] = {};
		float m_resLevelGain[Lanes] = {};

		Memo m_memoBalance, m_memoCutoff, m_memoTan, m_memoLog;
	};
}
