#include "cvMap.h"
#include "cvDco.h"

#include "cvFilterAmp.h"

#include <cmath>

namespace matrixClean::cvMap
{
	namespace
	{
		// pin voltage to log value (code | range << 13); range 0: 0..5.42 V, A8: -2.966..+2.943 V
		uint32_t range0(const double _volts)
		{
			const auto c = std::lround(_volts / 5.42 * 4095.0);
			return static_cast<uint32_t>(c < 0 ? 0 : (c > 4095 ? 4095 : c));
		}
		uint32_t rangeA8(const double _volts)
		{
			const auto c = std::lround((_volts + 2.966) / 5.909 * 4095.0);
			return static_cast<uint32_t>(c < 0 ? 0 : (c > 4095 ? 4095 : c)) | (RangeA8 << 13);
		}
		double unit63(const int32_t _v) { const double x = _v / (63.0 * 256.0); return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
		double hz(const int32_t _pitch) { return 440.0 * std::pow(2.0, (_pitch / 256.0 - 69.0) / 12.0); }

		// Stubs [R-DCO] [R-VCF] [R-VCA] following the CEM3396 model's pin behaviour only:
		// the waveshape pin sets the ramp current, so it tracks frequency for a constant peak;
		// the pulse comparator compares against the ramp; higher VCF voltage lowers the cutoff.
		constexpr double RangeSwitchHz = 600.0;
		constexpr double PeakVolts = 5.0;
		double rtCt(const double _hz) { return _hz > RangeSwitchHz ? 1.0934e-4 : 1.517e-3; }
	}

	uint32_t cvValue(const uint32_t _voice, const CvIndex _index, const VoiceControls& _c)
	{
		switch(_index)
		{
		case CvWsA: return dco::waveshape(_c.pitch[0], _c.shape[0]);
		case CvWsB: return dco::waveshape(_c.pitch[1], _c.shape[1]);
		case CvPwA: return dco::pulseWidth(_c.pw[0], _c.shape[0], _c.pulse[0]);
		case CvPwB: return dco::pulseWidth(_c.pw[1], _c.shape[1], _c.pulse[1]);
		case CvFreq: return cvFilterAmp::vcfValue(_voice, _c);
		case CvLog: return cvFilterAmp::vca1Value(_c);
		case CvLin: return cvFilterAmp::vca2Value(_c);
		case CvMod: return cvFilterAmp::fmValue(_c);
		}
		return 0;
	}

	uint32_t balanceValue(const uint32_t, const VoiceControls& _c)
	{
		return dco::balance(_c.mix);
	}

	uint32_t resonanceValue(const uint32_t, const VoiceControls& _c)
	{
		return cvFilterAmp::resonanceValue(_c);
	}

	DcoTimer dcoTimer(const uint32_t, const uint32_t _dco, const int32_t _pitch, const GlobalControls&)
	{
		DcoTimer t;
		t.count = dco::count(_pitch);
		t.rangeHigh = dco::rangeHigh(_pitch);
		t.mode = dco::NormalMode[_dco & 1];
		return t;
	}

	uint8_t controlLatch(const GlobalControls& _g)
	{
		return dco::controlLatch(_g.wave1, _g.wave2, _g.sync, _g.mute);
	}

	uint8_t dacRange(const uint32_t _channel, const uint32_t _dac12)
	{
		if(_channel >= 54) return 0;
		if(_channel >= 48) return RangeA8;
		switch(_channel % 8)
		{
		case CvFreq: return RangeA8;
		case CvMod: return RangeA7;
		case CvPwA:
		case CvPwB: return _dac12 == 0 ? RangeA7 : 0;
		default: return 0;
		}
	}

	bool noiseLatch(const GlobalControls& _g)
	{
		return dco::noiseLatch(_g.wave2);
	}
}
