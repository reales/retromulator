#include "tracking.h"

namespace matrixClean
{
	int bipolarDiv(const Source _s)
	{
		if(_s == Source::Lever1) return 64;
		return _s == Source::Lfo1 || _s == Source::Lfo2 || _s == Source::Vibrato ? 128 : 0;
	}

	Mod trackingGenerator(const Mod _input, const int _bipolarDiv, const std::array<uint8_t, 5>& _points)
	{
		const bool bipolar = _bipolarDiv != 0;
		int32_t s = bipolar ? _input / _bipolarDiv + 128 : _input / 64;
		s = s < 0 ? 0 : (s > 255 ? 255 : s);

		// points to 8 bit, 63 is full scale; four segments of 64, delta truncated toward zero
		auto p8 = [&](uint32_t _i) { const int32_t p = _points[_i] & 63; return p >= 63 ? 255 : p * 4; };
		const uint32_t seg = static_cast<uint32_t>(s) >> 6;
		const int32_t a = p8(seg), b = p8(seg + 1);
		const int32_t out = a + (b - a) * (s & 63) / 64;
		return (bipolar ? out - 128 : out) * 64;
	}
}
