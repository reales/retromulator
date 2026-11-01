#include "Alloc.h"

#include <algorithm>

namespace dx7clean
{
void PolyAlloc::reset()
{
	m_key.fill(kNone);
	m_sustained.fill(false);
	m_last = regs::kVoices - 1;
	m_pedal = false;
}

void PolyAlloc::noteOn(uint8_t note, uint8_t velocity, std::vector<AllocAction>& out)
{
	for (int i = 1; i <= regs::kVoices; ++i)
	{
		const int v = (m_last + i) % regs::kVoices;
		if (m_key[v] != kNone)
			continue;
		m_key[v] = note;
		m_sustained[v] = false;
		m_last = v;
		out.push_back({AllocAction::KeyOn, uint8_t(v), note, velocity});
		return;
	}
}

void PolyAlloc::noteOff(uint8_t note, std::vector<AllocAction>& out)
{
	for (int v = 0; v < regs::kVoices; ++v)
	{
		if (m_key[v] != note)
			continue;
		m_key[v] = kNone;
		if (m_pedal)
			m_sustained[v] = true;
		else
			out.push_back({AllocAction::KeyOff, uint8_t(v), note, 0});
		return;
	}
}

void PolyAlloc::sustain(bool on, std::vector<AllocAction>& out)
{
	m_pedal = on;
	if (on)
		return;
	for (int v = 0; v < regs::kVoices; ++v)
	{
		if (!m_sustained[v])
			continue;
		m_sustained[v] = false;
		out.push_back({AllocAction::KeyOff, uint8_t(v), 0, 0});
	}
}

void PolyAlloc::allNotesOff(std::vector<AllocAction>& out)
{
	for (int v = 0; v < regs::kVoices; ++v)
	{
		if (m_key[v] == kNone)
			continue;
		const auto note = uint8_t(m_key[v]);
		m_key[v] = kNone;
		if (m_pedal)
			m_sustained[v] = true;
		else
			out.push_back({AllocAction::KeyOff, uint8_t(v), note, 0});
	}
}

bool PolyAlloc::anyKeyHeld() const
{
	return std::any_of(m_key.begin(), m_key.end(), [](int k) { return k != kNone; });
}

void MonoAlloc::reset()
{
	m_held.clear();
	m_current = -1;
	m_up = true;
	m_sounding = false;
	m_pedal = false;
}

void MonoAlloc::noteOn(uint8_t note, uint8_t velocity, std::vector<AllocAction>& out)
{
	if (m_held.size() >= kMaxKeys)
		return;

	if (m_held.empty())
	{
		m_current = note;
		m_velocity = velocity;
		m_sounding = true;
		out.push_back({AllocAction::KeyOn, 0, note, velocity});
	}
	else if (note != m_current)
	{
		const auto [lo, hi] = std::minmax_element(m_held.begin(), m_held.end());
		bool take;
		if (m_held.size() == 1)
		{
			m_up = note > m_current;
			take = true;
		}
		else
			take = m_up ? note > *hi : note < *lo;

		if (take)
		{
			m_current = note;
			out.push_back({AllocAction::Legato, 0, note, velocity});
		}
	}
	m_held.push_back(note);
}

void MonoAlloc::noteOff(uint8_t note, std::vector<AllocAction>& out)
{
	const auto it = std::find(m_held.begin(), m_held.end(), note);
	if (it == m_held.end())
		return;
	m_held.erase(it);

	if (note != m_current || std::find(m_held.begin(), m_held.end(), note) != m_held.end())
		return;

	if (!m_held.empty())
	{
		const auto [lo, hi] = std::minmax_element(m_held.begin(), m_held.end());
		m_current = m_up ? *hi : *lo;
		out.push_back({AllocAction::Legato, 0, uint8_t(m_current), m_velocity});
	}
	else if (!m_pedal)
		release(out);
}

void MonoAlloc::sustain(bool on, std::vector<AllocAction>& out)
{
	m_pedal = on;
	if (!on && m_held.empty())
		release(out);
}

void MonoAlloc::allNotesOff(std::vector<AllocAction>& out)
{
	m_held.clear();
	if (!m_pedal)
		release(out);
}

void MonoAlloc::release(std::vector<AllocAction>& out)
{
	if (!m_sounding)
		return;
	m_sounding = false;
	out.push_back({AllocAction::KeyOff, 0, uint8_t(m_current < 0 ? 0 : m_current), 0});
}
}
