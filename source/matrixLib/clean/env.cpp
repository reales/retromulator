#include "env.h"

#include <algorithm>
#include <cmath>

namespace matrixClean
{
	namespace envCurve
	{
		int rate(int _v)
		{
			_v = std::clamp(_v, 0, 63);
			if(_v <= 2) return 254;
			if(_v <= 17) return 260 - 10 * _v;
			if(_v <= 21) { static constexpr int r[] = {79, 69, 60, 52}; return r[_v - 18]; }
			return 69 - _v;
		}

		int delaySteps(int _v)
		{
			// table above 44 (roughly 2^(v/8)), measured env*_delay_*
			static constexpr int slow[] = {49, 53, 58, 63, 69, 75, 82, 89, 97, 106, 126, 137, 149, 162, 177, 193, 210, 229, 256};
			_v = std::clamp(_v, 0, 63);
			if(_v == 0) return 0;
			return _v <= 44 ? _v + 1 : slow[_v - 45];
		}

		// matrix modulated stage rate (matrix2/ amount_sweep2): offset on the rate, kept above 0
		int modRate(const int _v, const int _mod)
		{
			return std::clamp(rate(_v) - _mod, 1, 255);
		}

		int32_t sustainLevel(const int _v) { return std::clamp(_v, 0, 63) * 1024; }

		int32_t fallStep(const int32_t _d, const int _r)
		{
			return std::max((((_d >> 8) * _r) >> 8) * _r, (_r >> 2) + 1);
		}

		int rampStep(const int _v)
		{
			if(_v <= 0) return RampFull;
			const int r = rate(_v) - 2;
			return r * r;
		}

		int ampMultiplier(const int _amp, const uint8_t _velocity, const int _byVelocity, const int _matrixMul)
		{
			auto mul = [](const int _a) { return _a >= 63 ? 255 : 4 * std::max(_a, 0); };
			int a = mul(_amp);
			if(_byVelocity)
			{
				// velocity byte 2v times |amount|, 63 counts as 255 (env/, env2/ at 16, 32, 63)
				const int amt = std::abs(_byVelocity) >= 63 ? 255 : std::abs(_byVelocity);
				const int m = (_velocity * 2 * amt) >> 8;
				a += _byVelocity < 0 ? -m : m;
			}
			a += _matrixMul;   // matrix offset in multiplier steps, summed before the clamp (matrix/ fixed_routes)
			return std::clamp(a, 0, 255);
		}

		Mod output(const int32_t _level, const int _ampMul)
		{
			return static_cast<Mod>((static_cast<int64_t>(_level) * _ampMul) >> 10);
		}
	}

	using namespace envCurve;

	void Envelope::reset()
	{
		*this = Envelope();
	}

	Mod Envelope::value() const
	{
		return static_cast<Mod>(static_cast<int64_t>(m_level) * ModFull / EnvFull);
	}

	void Envelope::start(const Params& _p)
	{
		m_bootArmed = false;
		if(_p.trigger & envTrig::Reset)
			m_level = 0;
		m_levelF = m_level;
		m_delayLeft = std::max(0, delaySteps(_p.delay) + _p.delayMod);
		m_stage = m_delayLeft > 0 ? Stage::Delay : Stage::Attack;
	}

	void Envelope::release()
	{
		if(m_stage != Stage::Idle)
			m_stage = Stage::Release;
	}

	void Envelope::gateOn(const Params& _p, const bool _legato)
	{
		m_gate = true;
		if((_p.trigger & envTrig::External) || _p.lfoTrigger)
			return;
		if(!(_p.trigger & envTrig::Multi) && _legato && m_stage != Stage::Idle && m_stage != Stage::Release)
			return;
		start(_p);
	}

	void Envelope::gateOff(const Params& _p)
	{
		m_gate = false;
		if((_p.trigger & envTrig::External) || _p.lfoTrigger == envLfo::LfoTrigger)
			return;
		if((_p.mode & envMode::FreeRun) && m_stage != Stage::Sustain)
			return;
		release();
	}

	void Envelope::externalGate(const Params& _p, const bool _down)
	{
		if(!(_p.trigger & envTrig::External))
			return;
		m_gate = _down;
		if(!_down)
		{
			if(!(_p.mode & envMode::FreeRun) || m_stage == Stage::Sustain)
				release();
			return;
		}
		if((_p.trigger & envTrig::Multi) || m_stage == Stage::Idle || m_stage == Stage::Release)
			start(_p);
	}

	bool Envelope::lfoEvent(const Params& _p, const bool _wrapped, const bool _halfCrossed, const bool _keyHeld)
	{
		// lfo trigger 1 and 3 measured alike: triggered by LFO1 while a key is held; 2 also without keys
		if(!_p.lfoTrigger || (_p.trigger & envTrig::External))
			return false;
		m_bootArmed = false;
		const bool gated = (_p.lfoTrigger & envLfo::Gated) != 0;
		if(_wrapped && (!gated || _keyHeld))
		{
			m_gate = true;
			start(_p);
			return true;
		}
		else if(_halfCrossed)
		{
			m_gate = false;
			if((_p.mode & envMode::FreeRun) && m_stage != Stage::Sustain)
				return false;
			const bool was = m_stage != Stage::Idle && m_stage != Stage::Release;
			release();
			return was;
		}
		return false;
	}

	void Envelope::bootSustain(const Params& _p)
	{
		if(!m_bootArmed || _p.lfoTrigger != envLfo::LfoTrigger || (_p.trigger & envTrig::External))
			return;
		m_bootArmed = false;
		m_gate = true;
		m_stage = Stage::Sustain;
		m_level = sustainLevel(_p.sustain);
		m_levelF = m_level;
	}

	void Envelope::endDecay(const Params& _p)
	{
		m_stage = (_p.mode & envMode::Dadr) || !m_gate ? Stage::Release : Stage::Sustain;
	}

	void Envelope::step(const Params& _p)
	{
		switch(m_stage)
		{
		case Stage::Idle:
			m_level = 0;
			break;
		case Stage::Delay:
			// the level keeps releasing during the delay (env2/delay40_trig0)
			if(m_delayLeft-- > 0)
			{
				m_level = m_level > 0 ? std::max(0, m_level - fallStep(m_level, modRate(_p.release, _p.releaseMod))) : 0;
				break;
			}
			m_stage = Stage::Attack;
			[[fallthrough]];
		case Stage::Attack:
		{
			const int r = modRate(_p.attack, _p.attackMod);
			m_level += r * r;
			if(m_level >= EnvFull)
			{
				m_level = EnvFull;
				m_stage = Stage::Decay;
			}
			break;
		}
		case Stage::Decay:
		{
			const int32_t s = sustainLevel(_p.sustain);
			const int32_t d = m_level - s;
			if(d > 0)
				m_level = s + std::max(0, d - fallStep(d, modRate(_p.decay, _p.decayMod)));
			if(m_level <= s)
			{
				m_level = std::min(m_level, s);
				endDecay(_p);
			}
			break;
		}
		case Stage::Sustain:
			m_level = sustainLevel(_p.sustain);
			break;
		case Stage::Release:
			m_level = std::max(0, m_level - fallStep(m_level, modRate(_p.release, _p.releaseMod)));
			if(m_level == 0)
				m_stage = Stage::Idle;
			break;
		}
		m_levelF = m_level;
	}

	void Envelope::advance(const Params& _p, double _steps)
	{
		auto fall = [](double _d, const int _r, const double _n)
		{
			const double k = std::max(_d * _r * _r / 65536.0, (_r >> 2) + 1.0);
			return std::max(0.0, _d - k * _n);
		};

		while(_steps > 0.0)
		{
			switch(m_stage)
			{
			case Stage::Idle:
				m_levelF = 0.0;
				_steps = 0.0;
				break;
			case Stage::Delay:
			{
				const double n = std::min(_steps, m_delayLeft);
				m_delayLeft -= n;
				m_levelF = fall(m_levelF, modRate(_p.release, _p.releaseMod), n);
				_steps -= n;
				if(m_delayLeft <= 0.0)
					m_stage = Stage::Attack;
				break;
			}
			case Stage::Attack:
			{
				const int r = modRate(_p.attack, _p.attackMod);
				m_levelF += r * r * _steps;
				_steps = 0.0;
				if(m_levelF >= EnvFull)
				{
					m_levelF = EnvFull;
					m_stage = Stage::Decay;
				}
				break;
			}
			case Stage::Decay:
			{
				const double s = sustainLevel(_p.sustain);
				m_levelF = s + fall(m_levelF - s, modRate(_p.decay, _p.decayMod), _steps);
				_steps = 0.0;
				if(m_levelF <= s)
				{
					m_levelF = std::min(m_levelF, s);
					endDecay(_p);
				}
				break;
			}
			case Stage::Sustain:
				m_levelF = sustainLevel(_p.sustain);
				_steps = 0.0;
				break;
			case Stage::Release:
				m_levelF = fall(m_levelF, modRate(_p.release, _p.releaseMod), _steps);
				_steps = 0.0;
				if(m_levelF <= 0.0)
					m_stage = Stage::Idle;
				break;
			}
		}
		m_level = static_cast<int32_t>(m_levelF);
	}

	Mod Envelope::tick(const Params& _p, const double _dtUs, const bool _vintage)
	{
		if(_vintage)
			step(_p);
		else
			advance(_p, _dtUs / StepUs);
		return value();
	}

	// Ramp

	void Ramp::reset()
	{
		*this = Ramp();
	}

	void Ramp::keyTrigger(const RampTrigger _t, const bool _legato)
	{
		if(_t == RampTrigger::Multi || (_t == RampTrigger::Single && !_legato))
		{
			m_level = 0;
			m_levelF = 0.0;
			m_running = true;
		}
	}

	void Ramp::externalTrigger(const RampTrigger _t, const bool _keyHeld)
	{
		if(_t == RampTrigger::External || (_t == RampTrigger::GatedExternal && _keyHeld))
		{
			m_level = 0;
			m_levelF = 0.0;
			m_running = true;
		}
	}

	Mod Ramp::tick(const int _rate, const double _dtUs, const bool _vintage)
	{
		if(m_running)
		{
			const int inc = rampStep(_rate);
			if(_vintage)
			{
				m_level = std::min(RampFull, m_level + inc);
				m_levelF = m_level;
			}
			else
			{
				m_levelF = std::min<double>(RampFull, m_levelF + inc * _dtUs / StepUs);
				m_level = static_cast<int32_t>(m_levelF);
			}
			if(m_level >= RampFull)
				m_running = false;
		}
		return static_cast<Mod>(static_cast<int64_t>(m_level) * ModFull / RampFull);
	}
}
