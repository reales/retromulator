#include <algorithm>
#include "patch.h"

#include <cctype>

namespace matrixClean
{
	std::string Patch::name() const
	{
		std::string n;
		for(size_t i = 0; i < NameLength; ++i)
		{
			const auto c = data[i] & 0x3f;
			n += static_cast<char>(c < 0x20 ? c + 0x40 : c);
		}
		const auto end = n.find_last_not_of(' ');
		return end == std::string::npos ? std::string() : n.substr(0, end + 1);
	}

	void Patch::setName(const std::string& _name)
	{
		for(size_t i = 0; i < NameLength; ++i)
		{
			auto c = static_cast<uint8_t>(i < _name.size() ? std::toupper(static_cast<unsigned char>(_name[i])) : ' ');
			if(c < 0x20 || c > 0x5f)
				c = ' ';
			data[i] = c & 0x3f;
		}
	}

	Patch::Dco Patch::dco(const uint32_t _index) const
	{
		if(_index == 0)
			return {u(9), u(10), u(11), static_cast<uint8_t>(u(12) & 3), static_cast<uint8_t>(u(13) & 3),
				static_cast<uint8_t>((u(21) & 1) | 2), static_cast<uint8_t>(u(22) & 1), s(86), s(87)};
		return {u(14), u(15), u(16), static_cast<uint8_t>(u(17) & 3), static_cast<uint8_t>(u(18) & 7),
			static_cast<uint8_t>(u(23) & 3), static_cast<uint8_t>(u(24) & 1), s(88), s(89)};
	}

	Patch::Lfo Patch::lfo(const uint32_t _index) const
	{
		const size_t b = 35 + _index * 7;
		Lfo l;
		l.speed = u(b);
		l.trigger = static_cast<LfoTrigger>(u(b + 1) & 3);
		l.lag = (u(b + 2) & 1) != 0;
		l.wave = static_cast<LfoWave>(u(b + 3) & 7);   // 7 acts like 6 (lfo/wave7_*)
		l.retrigger = u(b + 4) & 0x1f;
		l.sampleSource = static_cast<Source>(u(b + 5) % 21);
		l.amp = u(b + 6);
		l.speedMod = s(102 + _index);
		l.ampByRamp = s(97 + _index);
		return l;
	}

	Patch::Env Patch::env(const uint32_t _index) const
	{
		const size_t b = 49 + _index * 9;
		Env e;
		e.trigger = u(b) & 7;
		e.delay = u(b + 1);
		e.attack = u(b + 2);
		e.decay = u(b + 3);
		e.sustain = u(b + 4);
		e.release = u(b + 5);
		e.amp = u(b + 6);
		e.lfoTrigger = u(b + 7) & 3;
		e.mode = u(b + 8) & 3;
		e.ampByVelocity = s(94 + _index);
		return e;
	}

	Patch::Bus Patch::bus(const uint32_t _i) const
	{
		const size_t b = 104 + _i * 3;
		const auto src = u(b) <= 20 ? u(b) : 0;
		const auto dst = u(b + 2) <= 32 ? u(b + 2) : 0;
		return {static_cast<Source>(src), s(b + 1), static_cast<Dest>(dst)};
	}

	void Patch::setBus(const uint32_t _i, const uint8_t _source, const int _amount, const uint8_t _dest)
	{
		const size_t b = 104 + _i * 3;
		data[b] = _source;
		data[b + 1] = static_cast<uint8_t>(static_cast<int8_t>(_amount));
		data[b + 2] = _dest;
	}

	namespace
	{
		constexpr ParamInfo g_params[] =
		{
			{ 0,   9, 0, 63}, { 1,  86, -63, 63}, { 2,  25, 0, 3}, { 3,  11, 0, 63}, { 4,  87, -63, 63},
			{ 5,  10, 0, 63}, { 6,  13, 0, 3}, { 7,  12, 0, 3}, { 8,  21, 0, 1}, { 9,  22, 0, 1},
			{10,  14, 0, 63}, {11,  88, -63, 63}, {12,  19, -31, 31}, {13,  16, 0, 63}, {14,  89, -63, 63},
			{15,  15, 0, 63}, {16,  18, 0, 7}, {17,  17, 0, 3}, {18,  23, 0, 3}, {19,  24, 0, 1},
			{20,  20, 0, 63}, {21,  26, 0, 127}, {22,  90, -63, 63}, {23,  91, -63, 63}, {24,  27, 0, 63},
			{25,  28, 0, 3}, {26,  29, 0, 3}, {27,  31, 0, 63}, {28,  92, -63, 63}, {29,  93, -63, 63},
			{30,  30, 0, 63}, {31, 100, -63, 63}, {32, 101, -63, 63}, {33,  76, 1, 20}, {34,  77, 0, 63},
			{35,  78, 0, 63}, {36,  79, 0, 63}, {37,  80, 0, 63}, {38,  81, 0, 63},
			{40,  82, 0, 63}, {41,  83, 0, 3}, {42,  84, 0, 63}, {43,  85, 0, 3}, {44,  32, 0, 63},
			{45,  99, -63, 63}, {46,  33, 0, 3}, {47,  34, 0, 1}, {48,   8, 0, 3},
			{50,  50, 0, 63}, {51,  51, 0, 63}, {52,  52, 0, 63}, {53,  53, 0, 63}, {54,  54, 0, 63},
			{55,  55, 0, 63}, {56,  94, -63, 63}, {57,  49, 0, 7}, {58,  57, 0, 3}, {59,  56, 0, 3},
			{60,  59, 0, 63}, {61,  60, 0, 63}, {62,  61, 0, 63}, {63,  62, 0, 63}, {64,  63, 0, 63},
			{65,  64, 0, 63}, {66,  95, -63, 63}, {67,  58, 0, 7}, {68,  66, 0, 3}, {69,  65, 0, 3},
			{70,  68, 0, 63}, {71,  69, 0, 63}, {72,  70, 0, 63}, {73,  71, 0, 63}, {74,  72, 0, 63},
			{75,  73, 0, 63}, {76,  96, -63, 63}, {77,  67, 0, 7}, {78,  75, 0, 3}, {79,  74, 0, 3},
			{80,  35, 0, 63}, {81, 102, -63, 63}, {82,  38, 0, 6}, {83,  39, 0, 31}, {84,  41, 0, 63},
			{85,  97, -63, 63}, {86,  36, 0, 3}, {87,  37, 0, 1}, {88,  40, 0, 20},
			{90,  42, 0, 63}, {91, 103, -63, 63}, {92,  45, 0, 6}, {93,  46, 0, 31}, {94,  48, 0, 63},
			{95,  98, -63, 63}, {96,  43, 0, 3}, {97,  44, 0, 1}, {98,  47, 0, 20},
		};
	}

	const ParamInfo* findParam(const uint8_t _number)
	{
		for(const auto& p : g_params)
		{
			if(p.number == _number)
				return &p;
		}
		return nullptr;
	}

	bool setParam(Patch& _p, const uint8_t _number, int _value)
	{
		const auto* info = findParam(_number);
		if(!info)
			return false;
		// stored as received, without range limit (sysex/edit_summary.csv, ctrl/rpn.csv)
		_value = _value < -128 ? -128 : (_value > 255 ? 255 : _value);
		_p.data[info->byte] = static_cast<uint8_t>(_value & 0xff);
		return true;
	}

	bool applyParam(Patch& _p, const uint8_t _number, const uint8_t _raw)
	{
		const int v = _number == 21 ? (_raw & 0x7f) : static_cast<int8_t>(static_cast<uint8_t>((_raw & 0x7f) | ((_raw & 0x40) << 1)));
		return setParam(_p, _number, v);
	}

	Patch makeInitPatch()
	{
		Patch p;
		p.setName("INIT");
		auto& d = p.data;
		d[8] = 0;
		d[9] = 0; d[10] = 0; d[11] = 31; d[12] = 1; d[13] = 2;
		d[14] = 0; d[15] = 0; d[16] = 31; d[17] = 1; d[18] = 0; d[19] = 0;
		d[20] = 63;
		d[23] = 2;
		d[26] = 127;
		d[31] = 63;
		for(size_t e = 0; e < 3; ++e)
		{
			d[49 + e * 9 + 4] = 63;
			d[49 + e * 9 + 6] = e == 1 ? 63 : 0;
		}
		d[76] = static_cast<uint8_t>(Source::Keyboard);
		d[77] = 0; d[78] = 15; d[79] = 31; d[80] = 47; d[81] = 63;
		d[93] = 63;
		return p;
	}

	void packNibbles(const uint8_t* _data, const size_t _size, std::vector<uint8_t>& _out)
	{
		uint32_t sum = 0;
		for(size_t i = 0; i < _size; ++i)
		{
			_out.push_back(_data[i] & 0x0f);
			_out.push_back(_data[i] >> 4);
			sum += _data[i];
		}
		_out.push_back(static_cast<uint8_t>(sum & 0x7f));
	}

	bool unpackNibbles(const uint8_t* _nibbles, const size_t _size, uint8_t* _out, const uint8_t _checksum)
	{
		uint32_t sum = 0;
		for(size_t i = 0; i < _size; ++i)
		{
			_out[i] = static_cast<uint8_t>((_nibbles[i * 2] & 0x0f) | ((_nibbles[i * 2 + 1] & 0x0f) << 4));
			sum += _out[i];
		}
		return (sum & 0x7f) == (_checksum & 0x7f);
	}

	std::vector<uint8_t> encodePatch(const Patch& _p, const uint8_t _opcode, const uint8_t _number)
	{
		std::vector<uint8_t> s = {0xf0, 0x10, 0x06, _opcode, static_cast<uint8_t>(_number & 0x7f)};
		packNibbles(_p.data.data(), Patch::Size, s);
		s.push_back(0xf7);
		return s;
	}

	std::vector<uint8_t> encodePatchReply(const Patch& _p, const uint8_t _bank, const uint8_t _number)
	{
		std::vector<uint8_t> s;
		encodePatchReply(_p, _bank, _number, s);
		return s;
	}

	void encodePatchReply(const Patch& _p, const uint8_t _bank, const uint8_t _number, std::vector<uint8_t>& _out)
	{
		Patch p = _p;
		const uint8_t name[Patch::NameLength] = {'B', 'N', 'K', static_cast<uint8_t>('0' + _bank % 10), ':', ' ',
			static_cast<uint8_t>('0' + (_number / 10) % 10), static_cast<uint8_t>('0' + _number % 10)};
		std::copy(std::begin(name), std::end(name), p.data.begin());
		const uint8_t head[] = {0xf0, 0x10, 0x06, 0x01, static_cast<uint8_t>(_number & 0x7f)};
		_out.insert(_out.end(), std::begin(head), std::end(head));
		packNibbles(p.data.data(), Patch::Size, _out);
		_out.push_back(0xf7);
	}
}
