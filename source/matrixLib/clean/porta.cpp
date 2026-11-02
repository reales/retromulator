#include "porta.h"

#include <cmath>
#include <cstdlib>
#include <iterator>

namespace matrixClean
{
	namespace
	{
		// effective amount (x 100) for the rate modulation, porta2/ vel and bus grids
		constexpr int32_t g_amtX[] = {0, 9, 16, 18, 27, 32, 36, 45, 54, 63};
		constexpr int32_t g_amtY[] = {0, 917, 1600, 1945, 2880, 3300, 3820, 5000, 10790, 25700};

		int32_t effectiveAmount100(int _a)
		{
			_a = _a < 0 ? -_a : _a;
			if(_a > 63) _a = 63;
			for(size_t i = 1; i < std::size(g_amtX); ++i)
			{
				if(_a <= g_amtX[i])
					return g_amtY[i - 1] + (g_amtY[i] - g_amtY[i - 1]) * (_a - g_amtX[i - 1]) / (g_amtX[i] - g_amtX[i - 1]);
			}
			return g_amtY[std::size(g_amtY) - 1];
		}
	}

	int32_t Portamento::bootPitch(const uint32_t _voice)
	{
		// 1/256 semitone: rate 63 glides with DCO1 frequency raised (porta6/), voice 2 from porta4/
		constexpr int32_t p[VoiceCount] = {11453, 6589, 27142, 18303, 7167, 21089};
		return p[_voice % VoiceCount];
	}

	int32_t Portamento::bootDriftRef(const uint32_t _voice)
	{
		// a new glide during the boot drift measures its distance from this far above the pitch (porta5/reassign_distinct)
		constexpr int32_t c[VoiceCount] = {130, 179, 117, 80, 558, 102};
		return c[_voice % VoiceCount];
	}

	uint16_t Portamento::bootStep(const uint32_t _voice)
	{
		// constant time step of the boot glide towards the idle key, the same at every rate (porta5/)
		constexpr uint16_t s[VoiceCount] = {165, 421, 666, 197, 388, 349};
		return s[_voice % VoiceCount];
	}

	int32_t Portamento::bootStartOffset(const uint32_t _voice)
	{
		// 1/512 semitone, constant per voice over rates 18-48 (porta2/long_*); voices 1, 2, 4 not resolvable
		constexpr int32_t o[VoiceCount] = {138, 0, 0, 3, 0, 65};
		return o[_voice % VoiceCount];
	}

	uint16_t Portamento::coefficient(const int _rate)
	{
		// porta2/long_*: coefficient = s^2, s linear in three ranges, table at both ends
		if(_rate <= 0) return 0xffff;
		if(_rate <= 2) return 254 * 254;
		if(_rate <= 17) { const int s = 260 - 10 * _rate; return static_cast<uint16_t>(s * s); }
		if(_rate <= 22) { constexpr int s[] = {79, 69, 60, 52, 47}; return static_cast<uint16_t>(s[_rate - 18] * s[_rate - 18]); }
		if(_rate <= 52) { const int s = 69 - _rate; return static_cast<uint16_t>(s * s); }
		if(_rate <= 63) { constexpr uint16_t r[] = {170, 100, 50, 30, 20, 12, 10, 8, 6, 4, 2}; return r[_rate - 53]; }
		return 641;   // modulated past 63
	}

	int Portamento::rateOffset(const int _source, const int _amount)
	{
		// positive: floor(source * effective amount / 512); negative rounds half away
		if(!_amount || _source <= 0)
			return 0;
		const int64_t x = static_cast<int64_t>(_source) * effectiveAmount100(_amount);
		return static_cast<int>(_amount > 0 ? x / 51200 : -((x + 25600) / 51200));
	}

	int Portamento::velocitySource(const int _velocity)
	{
		// velocity 1-127: 1, 1, 3, 3, ... (porta3/vel_*)
		return _velocity < 1 ? 0 : (((_velocity - 1) >> 1) << 1) + 1;
	}

	int Portamento::modulatedRate(const int _rate, const int _offset)
	{
		// modulated: twice the rate, 7 bit wrap to 6 bit (porta3/pv*, bus*: 50 -> 36, 60 -> 56)
		return ((_rate * 2) & 63) + _offset;
	}

	uint16_t Portamento::unisonCoefficient(const uint16_t _coeff, const uint32_t _voice)
	{
		// voices alternate around voice 0: 0, -1, +3, -4, +6, -7
		const int mag = static_cast<int>(_voice + _voice / 2);
		const int off = (_voice & 1) ? -mag : mag;
		return static_cast<uint16_t>(_coeff + off);
	}

	void Portamento::reset(const int32_t _pitch256)
	{
		m_pitch = m_target = clampPitch(_pitch256 * 2);
		m_distance = 0;
		m_start = 0;
		m_bootStep = 0;
		m_first = false;
		m_acc = 0.0;
	}

	void Portamento::setTarget(const int32_t _pitch256, const bool _glide)
	{
		m_target = clampPitch(_pitch256 * 2);
		if(!_glide)
			m_pitch = m_target;
		const int32_t from = m_start ? m_start : (m_bootStep ? m_pitch + m_driftRef : m_pitch);
		m_distance = static_cast<uint16_t>(std::abs(static_cast<int32_t>(m_target) - from));
		m_bootStep = 0;
		m_first = true;
	}

	void Portamento::update(const uint16_t _coeff, const LagMode _mode)
	{
		m_start = 0;   // kept until the glide first moves
		if(m_pitch == m_target)
			return;
		const int32_t d = static_cast<int32_t>(m_target) - m_pitch;
		const uint32_t rem = static_cast<uint32_t>(std::abs(d));
		const uint32_t r = _coeff;
		uint32_t step;
		switch(_mode)
		{
		case LagMode::ConstSpeed:
			step = r;
			break;
		case LagMode::ConstTime:
			step = m_bootStep ? m_bootStep : ((m_distance * r) >> 16) + 1;
			break;
		default:
			if(m_first)
				step = ((m_distance * r) >> 16) + 1;
			else
			{
				// remaining distance in steps of 256, rounded towards lower pitch
				const uint32_t q = d < 0 ? (rem + 255) >> 8 : rem >> 8;
				step = q ? ((q * r) >> 8) + 1 : (r >> 8) + 2;
			}
			break;
		}
		m_first = false;
		if(step >= rem) m_pitch = m_target;
		else m_pitch = static_cast<uint16_t>(d > 0 ? m_pitch + step : m_pitch - step);
	}

	int32_t Portamento::tick(const uint16_t _coeff, const LagMode _mode, const double _dtUs, const bool _vintage)
	{
		// Vintage calls once per refresh cycle; Smooth steps accumulate to whole cycles
		m_acc += _dtUs;
		if(_vintage)
		{
			if(_dtUs >= UpdateUs * 0.5 || m_acc >= UpdateUs)
			{
				m_acc = 0.0;
				update(_coeff, _mode);
			}
			return value();
		}
		while(m_acc >= UpdateUs)
		{
			m_acc -= UpdateUs;
			update(_coeff, _mode);
		}
		return value();
	}
}
