#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cvMap.h"
#include "types.h"

namespace matrixClean::analog
{
	// Analog spread model (Research/spread-design.md): CEM3396 and board part tolerances drawn per
	// unit from a seed, plus a calibration that corrects codes against the nominal chip. Off or
	// amount 0 gives the measured codes and the nominal voice model exactly.
	constexpr uint32_t ModelVersion = 1;    // draw specs, ids and calibration procedures
	// session chunk layout. A newer format only appends fields: an older reader with the same
	// model version loads the fields it knows and ignores the rest
	constexpr uint32_t FormatVersion = 1;

	enum class Residual : uint8_t { Drawn, Reference };   // VCF base table: drawn per unit or the measured one
	enum class Thermal : uint8_t { Settled };              // every die at its steady temperature, calibrated there

	enum ConvParam : uint8_t
	{
		CViGain, CCt, CRt, CR36, CWsOffset, CShaperScale, CShaperMismatch, CPwOffset,
		ConvParamCount
	};
	enum VoiceParam : uint8_t
	{
		VF0Oct, VScale, VScaleTc, VResThreshold, VResLevel, VLinThreshold, VLinFull, VLogSlope,
		VBalanceSlope, VBalanceOffset, VOutGain, VThermal,
		VoiceParamCount
	};
	enum GlobalParam : uint8_t { GRail, GlobalParamCount };

	enum class Dist : uint8_t { Normal, Uniform };

	struct Spec
	{
		const char* name;
		Dist dist;
		double nominal;
		double sigma;     // Normal: sigma around nominal, redrawn outside lo..hi
		double lo, hi;    // datasheet limits, also applied after the amount
	};

	const Spec& convSpec(ConvParam _p);
	const Spec& voiceSpec(VoiceParam _p);
	const Spec& globalSpec(GlobalParam _p);

	// drawn values at amount 1, model version 1. Saved with the session, never redrawn on load
	struct Draws
	{
		float conv[VoiceCount][2][ConvParamCount] = {};
		float voice[VoiceCount][VoiceParamCount] = {};
		float global[GlobalParamCount] = {};
		int32_t vcfResidual[VoiceCount][9] = {};   // Residual::Drawn: base minus the voice mean, 1/8 code
	};

	// effective chip and board values for one voice
	struct Chip
	{
		float f0Scale = 1.0f;       // multiplies the voice model's VCF cutoff at 0 V
		float voltsPerOctScale = 1.0f;   // divides the voice model's octaves per volt
		float resScale = 1.0f;
		float resLevelDb = 0.0f;
		float shaperPeak[2] = {2.5f, 2.5f};
		float shaperEnd[2] = {5.0f, 5.0f};
		float wsOffset[2] = {};
		float pwOffset[2] = {};
		float linThreshold = 0.0f;
		float linFull = 4.5f;
		float logSlope = 22.0f;
		float balanceSlope = 4.6f;
		float balanceOffset = 0.0f;
		float rtCt[2][2] = {};      // [converter][range], seconds
		float gain = 1.0f;          // summing gain
	};

	// code corrections, all exactly neutral for the nominal chip
	struct Calibration
	{
		int32_t wsNum[VoiceCount][2][2] = {};   // [voice][converter][range]
		int32_t wsDen[VoiceCount][2][2] = {};
		int32_t pwOffset[VoiceCount][2] = {};   // DAC codes added to pulse-on PW codes
		int32_t vcf[VoiceCount][9] = {};        // VCF table, cvFilterAmp vcfCal unit
	};

	// VCF cutoff law the calibration works with, fitted from the published vcf renders by
	// ab/vcffit.py (ab/vcffit.csv): cutoff at 0 V pin voltage and octaves per volt
	constexpr double FitF0 = 978.771;
	constexpr double FitOctPerVolt = 1.69832;

	// trim points: timer counts of the waveshape trim per range
	constexpr uint32_t TrimCountLow = 30578;     // key 36
	constexpr uint32_t TrimCountHigh = 2273;     // key 81
	constexpr uint32_t TrimPwCode = 3779;        // 5.0 V in range 0
	constexpr float RtCtLow = 1.517e-3f;         // same as the voice board
	constexpr float RtCtHigh = 1.0934e-4f;

	class Unit
	{
	public:
		Unit();

		void setEnabled(bool _on) { m_enabled = _on; }
		bool enabled() const { return m_enabled; }
		// new seed: new draws, then calibration
		void setSeed(uint32_t _seed);
		uint32_t seed() const { return m_seed; }
		// 0 = nominal, 0.5 tight, 1 typical, 1.5 worn; recalibrates
		void setAmount(float _amount);
		float amount() const { return m_amount; }
		void setResidual(Residual _r);
		Residual residual() const { return m_residual; }
		void setThermal(Thermal _t) { m_thermal = _t; }
		Thermal thermal() const { return m_thermal; }

		const Draws& draws() const { return m_draws; }
		const Calibration& calibration() const { return m_cal; }
		const Chip& chip(uint32_t _voice) const { return m_chips[_voice]; }

		// corrected code for a voice CV channel (same format as cvMap::cvValue)
		uint32_t cvValue(uint32_t _voice, CvIndex _index, const VoiceControls& _c) const;

		// draw and calibration procedures, pure functions of their inputs
		static Draws draw(uint32_t _seed);
		static Chip effective(const Draws& _d, uint32_t _voice, float _amount);
		// _f0, _octPerVolt: the nominal VCF law (default the fit); a chip is that law scaled by its draws
		static Calibration calibrate(const Draws& _d, float _amount, Residual _residual, double _f0 = FitF0, double _octPerVolt = FitOctPerVolt);
		static uint32_t trimCode(float _rtCt, float _wsOffset, float _pwOffset, uint32_t _count);

		// session chunk: payload, then u32 payload size, then the magic MXAN, appended after the
		// Core state. Settled: the stored calibration snapshot is used as is
		std::vector<uint8_t> getState() const;
		bool setState(const uint8_t* _data, size_t _size);
		// finds the chunk at the end of _blob, starting right after the Core state (_coreSize);
		// false = no chunk (older session: spread off)
		static bool findState(const std::vector<uint8_t>& _blob, size_t _coreSize, size_t& _offset, size_t& _size);

		// pushes the chip values to a voice board (matrixClean::VoiceBoard or the plugin copy)
		template<typename Board> void applyTo(Board& _b) const
		{
			_b.setSpread(m_enabled);
			for(uint32_t v = 0; v < VoiceCount; ++v)
			{
				const Chip& c = m_chips[v];
				typename Board::ChipParams p;   // defaults: the nominal voice model
				p.f0 = p.f0 * c.f0Scale;
				p.octPerVolt = p.octPerVolt / c.voltsPerOctScale;
				p.resScale = c.resScale;
				p.resLevelDb = c.resLevelDb;
				for(int i = 0; i < 2; ++i)
				{
					p.shaperPeak[i] = c.shaperPeak[i];
					p.shaperEnd[i] = c.shaperEnd[i];
					p.wsOffset[i] = c.wsOffset[i];
					p.pwOffset[i] = c.pwOffset[i];
					_b.setRtCt(v, static_cast<uint32_t>(i), c.rtCt[i][0], c.rtCt[i][1]);
				}
				p.linThreshold = c.linThreshold;
				p.linFull = c.linFull;
				p.logSlope = c.logSlope;
				p.balanceSlope = c.balanceSlope;
				p.balanceOffset = c.balanceOffset;
				_b.setChipParams(v, p);
				_b.setVoiceGain(v, c.gain);
			}
		}

	private:
		void update();

		bool m_enabled = false;
		uint32_t m_seed = 1;
		float m_amount = 1.0f;
		Residual m_residual = Residual::Drawn;
		Thermal m_thermal = Thermal::Settled;
		Draws m_draws;
		Calibration m_cal;
		std::array<Chip, VoiceCount> m_chips;
	};
}
