#include "midiIn.h"

namespace matrixClean
{
	void MidiIn::reset()
	{
		m_status = 0;
		m_count = 0;
		m_inSysex = false;
		m_sysexOverflow = false;
		m_sysex.clear();
	}

	void MidiIn::begin(MidiMessage& _out, const uint8_t _status)
	{
		_out.status = _status;
		_out.data1 = _out.data2 = 0;
		_out.sysex.clear();
	}

	uint32_t MidiIn::dataCount(const uint8_t _status)
	{
		switch(_status & 0xf0)
		{
		case 0xc0:
		case 0xd0: return 1;
		case 0xf0:
			switch(_status)
			{
			case 0xf1: case 0xf3: return 1;
			case 0xf2: return 2;
			default: return 0;
			}
		default: return 2;
		}
	}

	bool MidiIn::push(const uint8_t _byte, MidiMessage& _out)
	{
		if(_byte >= 0xf8)
		{
			begin(_out, _byte);
			return true;
		}

		if(_byte == 0xf0)
		{
			m_inSysex = true;
			m_sysexOverflow = false;
			m_sysex.assign(1, 0xf0);
			m_status = 0;
			return false;
		}

		if(m_inSysex)
		{
			if(_byte == 0xf7)
			{
				m_inSysex = false;
				if(m_sysexOverflow)
					return false;
				m_sysex.push_back(0xf7);
				begin(_out, 0xf0);
				_out.sysex.assign(m_sysex.begin(), m_sysex.end());
				return true;
			}
			if(_byte & 0x80)
				m_inSysex = false;   // aborted by another status, handled below
			else
			{
				if(m_sysex.size() + 1 >= MaxSysex)
					m_sysexOverflow = true;   // room for the F7 is kept
				else
					m_sysex.push_back(_byte);
				return false;
			}
		}

		if(_byte & 0x80)
		{
			m_count = 0;
			if(_byte >= 0xf0)
			{
				m_status = 0;
				if(dataCount(_byte) == 0)
				{
					begin(_out, _byte);
					return true;
				}
				return false;   // system common with data: ignored
			}
			m_status = _byte;
			return false;
		}

		if(!m_status)
			return false;

		m_data[m_count++] = _byte;
		if(m_count < dataCount(m_status))
			return false;

		begin(_out, m_status);
		_out.data1 = m_data[0];
		_out.data2 = m_count > 1 ? m_data[1] : 0;
		m_count = 0;
		return true;
	}
}
