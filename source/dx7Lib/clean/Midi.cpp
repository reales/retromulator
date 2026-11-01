#include "Midi.h"

namespace dx7clean
{
namespace
{
int dataBytes(uint8_t status)
{
	switch (status & 0xf0)
	{
	case 0xc0:
	case 0xd0:
		return 1;
	case 0xf0:
		switch (status)
		{
		case 0xf1:
		case 0xf3:
			return 1;
		case 0xf2:
			return 2;
		default:
			return 0;
		}
	default:
		return 2;
	}
}
}

void MidiParser::reset()
{
	m_status = 0;
	m_count = 0;
	m_inSysex = false;
	m_sysexOverflow = false;
	m_sysex.clear();
}

void MidiParser::feed(const uint8_t* data, size_t len)
{
	for (size_t i = 0; i < len; ++i)
		feed(data[i]);
}

void MidiParser::feed(uint8_t b)
{
	if (b >= 0xf8)
	{
		m_handler.onRealtime(b);
		return;
	}

	if (b & 0x80)
	{
		if (m_inSysex)
		{
			m_inSysex = false;
			if (b == 0xf7 && !m_sysexOverflow)
			{
				m_sysex.push_back(0xf7);
				m_handler.onSysex(m_sysex.data(), m_sysex.size());
			}
			m_sysex.clear();
			if (b == 0xf7)
				return;
		}

		if (b == 0xf0)
		{
			m_inSysex = true;
			m_sysexOverflow = false;
			m_sysex.clear();
			m_sysex.push_back(b);
			m_status = 0;
			return;
		}
		if (b == 0xf7)
			return;

		m_status = b;
		m_count = 0;
		if (dataBytes(b) == 0)
		{
			m_status = 0;
		}
		return;
	}

	if (m_inSysex)
	{
		if (m_sysex.size() < kMaxSysex)
			m_sysex.push_back(b);
		else
			m_sysexOverflow = true;
		return;
	}

	if (!m_status)
		return;

	m_data[m_count++] = b;
	if (m_count < dataBytes(m_status))
		return;
	m_count = 0;
	dispatch();
	if (m_status >= 0xf0)
		m_status = 0;
}

void MidiParser::dispatch()
{
	const uint8_t ch = m_status & 15;
	switch (m_status & 0xf0)
	{
	case 0x80: m_handler.onNoteOff(ch, m_data[0], m_data[1]); break;
	case 0x90:
		if (m_data[1])
			m_handler.onNoteOn(ch, m_data[0], m_data[1]);
		else
			m_handler.onNoteOff(ch, m_data[0], 0x40);
		break;
	case 0xa0: m_handler.onPolyPressure(ch, m_data[0], m_data[1]); break;
	case 0xb0: m_handler.onControlChange(ch, m_data[0], m_data[1]); break;
	case 0xc0: m_handler.onProgramChange(ch, m_data[0]); break;
	case 0xd0: m_handler.onChannelPressure(ch, m_data[0]); break;
	case 0xe0: m_handler.onPitchBend(ch, uint16_t(m_data[0] | m_data[1] << 7)); break;
	default: break;
	}
}
}
