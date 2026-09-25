#include "patch.h"

#include <cctype>

namespace matrixLib::patch
{
	bool isSinglePatch(const uint8_t* _sysex, const size_t _size)
	{
		return _size == SysexSize && _sysex[0] == 0xf0 && _sysex[1] == IdOberheim && _sysex[2] == IdMatrix &&
			(_sysex[3] == OpSinglePatch || _sysex[3] == OpEditBuffer) && _sysex[_size - 1] == 0xf7;
	}

	std::optional<Data> decode(const uint8_t* _sysex, const size_t _size)
	{
		if(!isSinglePatch(_sysex, _size))
			return {};

		Data d{};
		for(size_t i = 0; i < DataSize; ++i)
			d[i] = static_cast<uint8_t>((_sysex[5 + i * 2] & 0x0f) | ((_sysex[6 + i * 2] & 0x0f) << 4));
		return d;
	}

	std::vector<uint8_t> encode(const Data& _data, const uint8_t _opcode, const uint8_t _number)
	{
		std::vector<uint8_t> s = {0xf0, IdOberheim, IdMatrix, _opcode, static_cast<uint8_t>(_number & 0x7f)};
		s.reserve(SysexSize);
		uint32_t sum = 0;
		for(const auto b : _data)
		{
			s.push_back(b & 0x0f);
			s.push_back(b >> 4);
			sum += b;
		}
		s.push_back(static_cast<uint8_t>(sum & 0x7f));
		s.push_back(0xf7);
		return s;
	}

	std::string getName(const Data& _data)
	{
		std::string name;
		for(size_t i = 0; i < NameLength; ++i)
		{
			const auto c = _data[i] & 0x3f;
			name += static_cast<char>(c < 0x20 ? c + 0x40 : c);
		}
		const auto end = name.find_last_not_of(' ');
		return end == std::string::npos ? std::string() : name.substr(0, end + 1);
	}

	void setName(Data& _data, const std::string& _name)
	{
		for(size_t i = 0; i < NameLength; ++i)
		{
			auto c = static_cast<uint8_t>(i < _name.size() ? std::toupper(static_cast<unsigned char>(_name[i])) : ' ');
			if(c < 0x20 || c > 0x5f)
				c = ' ';
			_data[i] = c & 0x3f;
		}
	}

	std::string getName(const uint8_t* _sysex, const size_t _size)
	{
		const auto d = decode(_sysex, _size);
		return d ? getName(*d) : std::string();
	}

	const std::vector<Param>& getParams()
	{
		static const std::vector<Param> params =
		{
			{  0,   9,   0,  63, "DCO1 Frequency"},
			{  1,  86, -63,  63, "DCO1 Freq by LFO1"},
			{  2,  25,   0,   3, "DCO Sync"},
			{  3,  11,   0,  63, "DCO1 Pulse Width"},
			{  4,  87, -63,  63, "DCO1 PW by LFO2"},
			{  5,  10,   0,  63, "DCO1 Wave Shape"},
			{  6,  13,   0,   3, "DCO1 Wave Select"},
			{  7,  12,   0,   3, "DCO1 Levers"},
			{  8,  21,   0,   1, "DCO1 Portamento"},
			{  9,  22,   0,   1, "DCO1 Click"},
			{ 10,  14,   0,  63, "DCO2 Frequency"},
			{ 11,  88, -63,  63, "DCO2 Freq by LFO1"},
			{ 12,  19, -31,  31, "DCO2 Detune"},
			{ 13,  16,   0,  63, "DCO2 Pulse Width"},
			{ 14,  89, -63,  63, "DCO2 PW by LFO2"},
			{ 15,  15,   0,  63, "DCO2 Wave Shape"},
			{ 16,  18,   0,   7, "DCO2 Wave Select"},
			{ 17,  17,   0,   3, "DCO2 Levers"},
			{ 18,  23,   0,   3, "DCO2 Keyboard"},
			{ 19,  24,   0,   1, "DCO2 Click"},
			{ 20,  20,   0,  63, "Mix"},
			{ 21,  26,   0, 127, "VCF Frequency"},
			{ 22,  90, -63,  63, "VCF by Env1"},
			{ 23,  91, -63,  63, "VCF by Pressure"},
			{ 24,  27,   0,  63, "VCF Resonance"},
			{ 25,  28,   0,   3, "VCF Levers"},
			{ 26,  29,   0,   3, "VCF Keyboard"},
			{ 27,  31,   0,  63, "VCA1 Amount"},
			{ 28,  92, -63,  63, "VCA1 by Velocity"},
			{ 29,  93, -63,  63, "VCA2 by Env2"},
			{ 30,  30,   0,  63, "FM Amount"},
			{ 31, 100, -63,  63, "FM by Env3"},
			{ 32, 101, -63,  63, "FM by Pressure"},
			{ 33,  76,   0,  20, "Track Input"},
			{ 34,  77,   0,  63, "Track Point 1"},
			{ 35,  78,   0,  63, "Track Point 2"},
			{ 36,  79,   0,  63, "Track Point 3"},
			{ 37,  80,   0,  63, "Track Point 4"},
			{ 38,  81,   0,  63, "Track Point 5"},
			{ 40,  82,   0,  63, "Ramp1 Rate"},
			{ 41,  83,   0,   3, "Ramp1 Trigger"},
			{ 42,  84,   0,  63, "Ramp2 Rate"},
			{ 43,  85,   0,   3, "Ramp2 Trigger"},
			{ 44,  32,   0,  63, "Portamento Rate"},
			{ 45,  99, -63,  63, "Porta by Velocity"},
			{ 46,  33,   0,   3, "Portamento Mode"},
			{ 47,  34,   0,   1, "Legato"},
			{ 48,   8,   0,   3, "Keyboard Mode"},
			{ 50,  50,   0,  63, "Env1 Delay"},
			{ 51,  51,   0,  63, "Env1 Attack"},
			{ 52,  52,   0,  63, "Env1 Decay"},
			{ 53,  53,   0,  63, "Env1 Sustain"},
			{ 54,  54,   0,  63, "Env1 Release"},
			{ 55,  55,   0,  63, "Env1 Amplitude"},
			{ 56,  94, -63,  63, "Env1 Amp by Velocity"},
			{ 57,  49,   0,   7, "Env1 Trigger"},
			{ 58,  57,   0,   3, "Env1 Mode"},
			{ 59,  56,   0,   3, "Env1 LFO Trigger"},
			{ 60,  59,   0,  63, "Env2 Delay"},
			{ 61,  60,   0,  63, "Env2 Attack"},
			{ 62,  61,   0,  63, "Env2 Decay"},
			{ 63,  62,   0,  63, "Env2 Sustain"},
			{ 64,  63,   0,  63, "Env2 Release"},
			{ 65,  64,   0,  63, "Env2 Amplitude"},
			{ 66,  95, -63,  63, "Env2 Amp by Velocity"},
			{ 67,  58,   0,   7, "Env2 Trigger"},
			{ 68,  66,   0,   3, "Env2 Mode"},
			{ 69,  65,   0,   3, "Env2 LFO Trigger"},
			{ 70,  68,   0,  63, "Env3 Delay"},
			{ 71,  69,   0,  63, "Env3 Attack"},
			{ 72,  70,   0,  63, "Env3 Decay"},
			{ 73,  71,   0,  63, "Env3 Sustain"},
			{ 74,  72,   0,  63, "Env3 Release"},
			{ 75,  73,   0,  63, "Env3 Amplitude"},
			{ 76,  96, -63,  63, "Env3 Amp by Velocity"},
			{ 77,  67,   0,   7, "Env3 Trigger"},
			{ 78,  75,   0,   3, "Env3 Mode"},
			{ 79,  74,   0,   3, "Env3 LFO Trigger"},
			{ 80,  35,   0,  63, "LFO1 Speed"},
			{ 81, 102, -63,  63, "LFO1 Speed by Pressure"},
			{ 82,  38,   0,   6, "LFO1 Wave"},
			{ 83,  39,   0,  31, "LFO1 Retrigger Point"},
			{ 84,  41,   0,  63, "LFO1 Amplitude"},
			{ 85,  97, -63,  63, "LFO1 Amp by Ramp1"},
			{ 86,  36,   0,   3, "LFO1 Trigger"},
			{ 87,  37,   0,   1, "LFO1 Lag"},
			{ 88,  40,   0,  20, "LFO1 Sample Source"},
			{ 90,  42,   0,  63, "LFO2 Speed"},
			{ 91, 103, -63,  63, "LFO2 Speed by Keyboard"},
			{ 92,  45,   0,   6, "LFO2 Wave"},
			{ 93,  46,   0,  31, "LFO2 Retrigger Point"},
			{ 94,  48,   0,  63, "LFO2 Amplitude"},
			{ 95,  98, -63,  63, "LFO2 Amp by Ramp2"},
			{ 96,  43,   0,   3, "LFO2 Trigger"},
			{ 97,  44,   0,   1, "LFO2 Lag"},
			{ 98,  47,   0,  20, "LFO2 Sample Source"},
		};
		return params;
	}

	const Param* findParam(const uint8_t _number)
	{
		for(const auto& p : getParams())
		{
			if(p.number == _number)
				return &p;
		}
		return nullptr;
	}

	int getParamValue(const Data& _data, const Param& _param)
	{
		const auto raw = _data[_param.byte];
		const int v = _param.min < 0 ? static_cast<int8_t>(raw) : static_cast<int>(raw);
		return v < _param.min ? _param.min : (v > _param.max ? _param.max : v);
	}

	std::vector<uint8_t> createParamChange(const uint8_t _number, const int _value)
	{
		return {0xf0, IdOberheim, IdMatrix, OpParameter, static_cast<uint8_t>(_number & 0x7f), static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}
}
