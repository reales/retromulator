#include "cvDco.h"

#include <cmath>

namespace matrixClean::dco
{
	namespace
	{
		int32_t clampi(const int32_t _v, const int32_t _lo, const int32_t _hi) { return _v < _lo ? _lo : (_v > _hi ? _hi : _v); }

		// count per semitone, index 0 = key 24: 2 MHz / (110 Hz * 2^((key - 57) / 12)), capped at 65535.
		// Three entries differ from the rounded formula in the measured curve.
		uint16_t tableCount(const int32_t _i)
		{
			const int32_t key = _i + 24;
			if(key == 53) return 22907;
			if(key == 61) return 14430;
			if(key == 91) return 2557;
			const double c = std::round(2000000.0 / (110.0 * std::exp2((key - 57) / 12.0)));
			return static_cast<uint16_t>(c > 65535.0 ? 65535.0 : c);
		}

		struct CountTable
		{
			uint16_t c[129];
			CountTable() { for(int32_t i = 0; i < 129; ++i) c[i] = tableCount(i); }
		};
		const CountTable g_counts;

		// waveshape frequency value per semitone, index 0 = key 24 (ws2/, ws/, dco2/): 2048 at key 40,
		// doubling per octave; the rounding of single entries is measured, not a formula
		constexpr uint16_t g_shapeFreq[73] = {
			813, 861, 912, 966, 1024, 1084, 1149, 1218, 1290, 1367, 1448, 1534, 1625, 1722, 1824, 1933,
			2048, 2170, 2299, 2435, 2580, 2734, 2896, 3068, 3251, 3444, 3649, 3866, 4096, 4339, 4597, 4871,
			5160, 5467, 5792, 6137, 6502, 6888, 7298, 7732, 8192, 8679, 9195, 9742, 10321, 10935, 11585, 12274,
			13004, 13777, 14596, 15464, 16384, 17358, 18390, 19484, 20643, 21870, 23170, 24548, 26008, 27555, 29193, 30929,
			32768, 34716, 36781, 38968, 41286, 43740, 46341, 49097, 52016};
		constexpr int32_t HighRangeShift = 11648;   // high range: pitch 45.5 semitones lower
		constexpr int32_t ShapeFreqMax = 52000;      // 51993..52009 (dco2/ P00 sweep, wrapped pitch)

		int32_t shapeFreq(const int32_t _pitch)
		{
			if(_pitch < 0)
				return g_shapeFreq[0];
			const int32_t i = _pitch >> 8, f = _pitch & 255;
			if(i >= 72)
				return ShapeFreqMax;
			const int32_t a = g_shapeFreq[i], b = g_shapeFreq[i + 1];
			const int32_t v = a + (((b - a) * f) >> 8);
			return v > ShapeFreqMax ? ShapeFreqMax : v;
		}
	}

	int32_t keyPitch(const int32_t _key256, const int _freq)
	{
		const int32_t key = _key256 < 12 * PitchUnit ? 12 * PitchUnit : _key256;
		return key + (_freq - 24) * PitchUnit;
	}

	int32_t floorPitch(const int32_t _pitch)
	{
		// key + frequency below the table holds at 0 (count 65535); from about -82 semitones the
		// value wraps to the top (count 31, nrpn/incdec)
		if(_pitch >= 0) return _pitch;
		return _pitch >= -82 * PitchUnit ? 0 : 32768;
	}

	int32_t tuneFine(const int _tune)
	{
		return _tune * 4;   // pitch2/rotate_mastertune: exact for -31..31
	}

	int32_t detuneFine(const int _detune)
	{
		// pitch2/rotate_detune: a table remains. g = twice the offset in 1/64 semitone; positive
		// values round down, negative ones away from zero
		static constexpr uint8_t g[32] = {0, 1, 1, 1, 2, 3, 3, 3, 4, 5, 6, 7, 9, 10, 13, 15,
			17, 20, 23, 25, 27, 30, 33, 35, 37, 40, 43, 45, 47, 50, 55, 63};
		const int d = _detune < -31 ? -31 : (_detune > 31 ? 31 : _detune);
		return d >= 0 ? (g[d] >> 1) * 4 : -((g[-d] + 1) >> 1) * 4;
	}

	int32_t unisonSpread(const uint32_t _voice)
	{
		// pitch2/unison_detune: DCO2 of voice v moves 6 v units, odd voices down
		const auto o = static_cast<int32_t>(_voice) * 6;
		return (_voice & 1) ? -o : o;
	}

	int32_t bendFine(const int32_t _bend, const int _range)
	{
		// pitch2/, ws/: 8 bit bend, 2 * range units per step, the last step at each end is full scale
		int32_t v = _bend - 8192;
		if(v <= -8128) v = -8192;
		if(v >= 8128) v = 8192;
		return (v >> 6) * _range * 2;
	}

	uint16_t count(const int32_t _pitch)
	{
		if(_pitch < 0 || _pitch >= 32768)
			return 31;
		const int32_t i = _pitch >> 8, f = _pitch & 255;
		const int32_t a = g_counts.c[i], b = g_counts.c[i + 1];
		return static_cast<uint16_t>(a - (((a - b) * f) >> 8));
	}

	bool rangeHigh(const int32_t _pitch)
	{
		return count(_pitch) <= 3715;
	}

	int32_t shapeRoute(const int _level, const int _amount)
	{
		// matrix/amount_sweep, ws4/ (40, 48, 54-63): shape change at full source per amount
		static constexpr int32_t t[64] = {0, 0, 0, 20, 41, 41, 41, 61, 81, 81, 122, 122, 142, 163, 203, 203,
			244, 264, 284, 325, 366, 386, 406, 447, 508, 569, 650, 691, 772, 833, 894, 975,
			1016, 1097, 1138, 1219, 1260, 1341, 1422, 1463, 1600, 1869, 2113, 2438, 2763, 3129, 3576, 4104,
			4672, 5323, 6014, 6908, 7883, 8209, 9216, 10744, 12288, 14336, 16384, 18432, 20480, 24576, 28672, 32768};
		if(!_level || !_amount)
			return 0;
		const int a = _amount < 0 ? -_amount : (_amount > 63 ? 63 : _amount);
		const int32_t mag = ((_level < 0 ? -_level : _level) * t[a > 63 ? 63 : a]) >> 8;
		return (_level < 0) != (_amount < 0) ? -mag : mag;
	}

	int32_t shapeTop(const int32_t _sum)
	{
		// ws4/, ws3/, matrix/twobus: buses sum unclipped; past the top (64) the sum keeps its
		// position within 2 shape units; an excess past 128 reads 66 (ws4/ two buses, twobus)
		if(_sum <= 64 * 256)
			return _sum;
		const int32_t e = _sum - 64 * 256;
		return e > 128 * 256 ? 66 * 256 : 64 * 256 + e % 512;
	}

	uint32_t waveshape(const int32_t _pitch, const int32_t _shape256)
	{
		const int32_t s = clampi(_shape256, -520, 68 * 256);   // matrix/: modulated shape -2.03..66 (shapeAdd)
		// 16 x 16 bit product of the frequency value and (62 + shape) * 128 without the low byte x
		// low byte partial product, top bits taken (ws2/: every 1/256 step at 8 shapes)
		const bool wrapped = _pitch < 0 || _pitch >= 32768;
		const int32_t fv = wrapped ? ShapeFreqMax : shapeFreq(rangeHigh(_pitch) ? _pitch - HighRangeShift : _pitch);
		const int32_t m = (62 * 256 + s) >> 1;
		const int64_t v = (static_cast<int64_t>(fv) * m - (fv & 255) * (m & 255)) >> 17;
		return static_cast<uint32_t>(clampi(static_cast<int32_t>(v), 0, 4095)) | CvSlow;
	}

	uint32_t pulseWidth(const int32_t _pw512, const int32_t _shape256, const bool _on)
	{
		// (pw + 1) * (62 + shape) / 2: scales with the ramp like the waveshape. pw in 1/512 steps,
		// the matrix accumulator's resolution (an odd accumulator moves the code: lfo/, matrix/)
		if(!_on)
			return CvSlow | (RangeA7 << 13);
		const int64_t pw = clampi(_pw512, -512, 2 * 63 * 256 - 1) + 512;   // matrix modulation reaches 1983 (matrix/)
		const int64_t s = clampi(_shape256, 0, 63 * 256) + 62 * 256;
		return static_cast<uint32_t>(clampi(static_cast<int32_t>(pw * s / (4 * 256 * 256)), 0, 4095)) | CvSlow;
	}

	uint32_t balance(const int32_t _mix256)
	{
		return static_cast<uint32_t>(clampi(32 + _mix256 / 4, 0, 4095)) | CvSlow | (RangeA8 << 13);
	}

	uint8_t controlLatch(const uint8_t _wave1, const uint8_t _wave2, const uint8_t _sync, const bool _mute)
	{
		// bits 5-4: 3 = converter A sloped only, 2 = none, 1 = B only, 0 = both
		const bool a = (_wave1 & 2) != 0;
		const bool b = (_wave2 & 6) != 0;
		uint8_t v = static_cast<uint8_t>((((a != b) ? 1 : 0) | (b ? 0 : 2)) << 4);
		v |= 1 | CtlSync1Off | CtlSync2Off;
		if(_sync & 1) v &= ~CtlSync2Off;
		if(_sync & 2) v &= ~CtlSync1Off;
		if(_mute) v |= CtlMute;
		return v;
	}

	bool noiseLatch(const uint8_t _wave2)
	{
		return (_wave2 & 4) != 0;
	}

	bool pulseOn(const uint32_t _dco, const uint8_t _wave)
	{
		return (_wave & 1) && !(_dco == 1 && (_wave & 4));
	}

	uint16_t phaseCount(const uint32_t _dco, const uint16_t _old, const uint16_t _read, const uint16_t _new)
	{
		if(_read < 128 || (_dco == 1 && _old == 65535) || _old == 0)
			return 0;
		const int32_t k = _dco == 0 ? 745 : 1385;
		const int32_t q = static_cast<int32_t>(std::floor((static_cast<int32_t>(_read) - k) * 256.0 / _old));
		const int32_t f = q & 255;
		const int32_t x = (static_cast<int32_t>(_new) * f + 128) >> 8;
		return static_cast<uint16_t>(x < 2 ? 2 : x);
	}
}
