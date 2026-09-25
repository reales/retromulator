#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace matrixLib
{
	// Matrix-6/1000 single patch: 134 bytes, sent as low/high nibble pairs
	namespace patch
	{
		constexpr uint8_t IdOberheim = 0x10;
		constexpr uint8_t IdMatrix = 0x06;

		constexpr uint8_t OpSinglePatch = 0x01;          // F0 10 06 01 nn <data> cs F7, store to patch nn
		constexpr uint8_t OpRequest = 0x04;              // F0 10 06 04 type nn F7
		constexpr uint8_t OpParameter = 0x06;            // F0 10 06 06 pp vv F7, remote parameter edit
		constexpr uint8_t OpSetBank = 0x0a;              // F0 10 06 0A bb F7
		constexpr uint8_t OpEditBuffer = 0x0d;           // F0 10 06 0D 00 <data> cs F7

		constexpr size_t DataSize = 134;
		constexpr size_t SysexSize = 5 + DataSize * 2 + 2;   // 275
		constexpr size_t NameLength = 8;

		using Data = std::array<uint8_t, DataSize>;

		bool isSinglePatch(const uint8_t* _sysex, size_t _size);
		std::optional<Data> decode(const uint8_t* _sysex, size_t _size);
		inline bool isSinglePatch(const std::vector<uint8_t>& _sysex) { return isSinglePatch(_sysex.data(), _sysex.size()); }
		inline std::optional<Data> decode(const std::vector<uint8_t>& _sysex) { return decode(_sysex.data(), _sysex.size()); }
		std::vector<uint8_t> encode(const Data& _data, uint8_t _opcode, uint8_t _number);

		// names use 6 bit characters: 0x20-0x3F as is, 0x00-0x1F are 0x40-0x5F
		std::string getName(const Data& _data);
		void setName(Data& _data, const std::string& _name);
		std::string getName(const uint8_t* _sysex, size_t _size);
		template<typename T> std::string getName(const T& _sysex) { return getName(_sysex.data(), _sysex.size()); }

		// readdresses a single patch to the edit buffer, so it plays at once
		template<typename T> void toEditBuffer(T& _sysex)
		{
			if(!isSinglePatch(_sysex.data(), _sysex.size()))
				return;
			_sysex[3] = OpEditBuffer;
			_sysex[4] = 0x00;
		}

		// remote parameter (opcode 06) and where it lives in the 134 byte patch
		struct Param
		{
			uint8_t number;
			uint8_t byte;
			int8_t min;
			int8_t max;
			const char* name;
		};

		const Param* findParam(uint8_t _number);
		const std::vector<Param>& getParams();

		// value as the patch stores it (signed parameters are two's complement bytes)
		int getParamValue(const Data& _data, const Param& _param);
		std::vector<uint8_t> createParamChange(uint8_t _number, int _value);
	}
}
