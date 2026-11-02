#include "voiceAlloc.h"

#include <algorithm>

namespace matrixClean
{
	void VoiceAllocator::reset()
	{
		m_voices = {};
		m_held.clear();
		m_next = 1;
		m_ageCounter = 0;
		m_unisonNote = -1;
	}

	int VoiceAllocator::findFree() const
	{
		for(uint32_t i = 0; i < VoiceCount; ++i)
		{
			const auto v = (m_next + i) % VoiceCount;
			if(!m_voices[v].gate)
				return static_cast<int>(v);
		}
		return -1;
	}

	void VoiceAllocator::assign(const uint32_t _v, const uint8_t _note, const uint8_t _velocity, const bool _legato, std::vector<Event>& _out)
	{
		auto& voice = m_voices[_v];
		const bool gated = voice.gate;
		voice.note = _note;
		voice.gate = true;
		voice.velocity = _velocity;
		voice.age = ++m_ageCounter;
		_out.push_back({Event::Type::On, _v, _note, _velocity, _legato, gated});
	}

	void VoiceAllocator::noteOn(const uint8_t _note, const uint8_t _velocity, std::vector<Event>& _out)
	{
		const bool wasHeld = !m_held.empty();
		m_held.erase(std::remove(m_held.begin(), m_held.end(), _note), m_held.end());
		m_held.push_back(_note);

		if(m_mode == KeyboardMode::Unison)
		{
			unisonUpdate(wasHeld, _velocity, _out);
			return;
		}

		int v = -1;
		if(m_mode != KeyboardMode::Rotate)
		{
			for(uint32_t i = 0; i < VoiceCount && v < 0; ++i)
			{
				if(m_voices[i].note == _note)
					v = static_cast<int>(i);
			}
		}
		if(v < 0)
			v = findFree();
		if(v < 0)
			return;   // all six held: the new note does not sound

		m_next = (static_cast<uint32_t>(v) + 1) % VoiceCount;
		assign(static_cast<uint32_t>(v), _note, _velocity, wasHeld, _out);
	}

	void VoiceAllocator::noteOff(const uint8_t _note, const uint8_t _velocity, std::vector<Event>& _out)
	{
		const auto it = std::find(m_held.begin(), m_held.end(), _note);
		if(it == m_held.end())
			return;
		m_held.erase(it);

		if(m_mode == KeyboardMode::Unison)
		{
			if(m_held.empty())
			{
				for(uint32_t v = 0; v < VoiceCount; ++v)
					release(v, _velocity, _out);
				m_unisonNote = -1;
			}
			else
				unisonUpdate(true, m_voices[0].velocity, _out);
			return;
		}

		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			if(m_voices[v].gate && m_voices[v].note == _note)
				release(v, _velocity, _out);
		}
	}

	void VoiceAllocator::unisonUpdate(const bool _wasHeld, const uint8_t _velocity, std::vector<Event>& _out)
	{
		// lowest held note sounds; releasing it moves to the next lowest without a new attack
		const auto lowest = *std::min_element(m_held.begin(), m_held.end());
		if(lowest == m_unisonNote && _wasHeld)
			return;
		const bool retarget = _wasHeld && m_unisonNote >= 0;
		m_unisonNote = lowest;
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			auto& voice = m_voices[v];
			const bool gated = voice.gate;
			voice.note = lowest;
			voice.gate = true;
			voice.velocity = _velocity;
			voice.age = ++m_ageCounter;
			_out.push_back({retarget ? Event::Type::Retarget : Event::Type::On, v, lowest, _velocity, _wasHeld, gated});
		}
	}

	void VoiceAllocator::release(const uint32_t _v, const uint8_t _velocity, std::vector<Event>& _out)
	{
		auto& voice = m_voices[_v];
		if(!voice.gate)
			return;
		voice.gate = false;
		_out.push_back({Event::Type::Off, _v, static_cast<uint8_t>(voice.note), _velocity, false, true});
	}

	void VoiceAllocator::noteOnVoice(const uint32_t _voice, const uint8_t _note, const uint8_t _velocity, std::vector<Event>& _out)
	{
		auto& voice = m_voices[_voice];
		const bool legato = voice.gate;
		voice.note = _note;
		voice.gate = true;
		voice.velocity = _velocity;
		voice.age = ++m_ageCounter;
		_out.push_back({legato ? Event::Type::Retarget : Event::Type::On, _voice, _note, _velocity, legato, legato});
	}

	void VoiceAllocator::noteOffVoice(const uint32_t _voice, const uint8_t _note, const uint8_t _velocity, std::vector<Event>& _out)
	{
		if(m_voices[_voice].note != _note)
			return;
		release(_voice, _velocity, _out);
	}

	void VoiceAllocator::allNotesOff(std::vector<Event>& _out)
	{
		m_held.clear();
		for(uint32_t v = 0; v < VoiceCount; ++v)
			release(v, 64, _out);
		m_unisonNote = -1;
	}
}
