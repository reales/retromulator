#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "endian.h"

namespace mc68k
{
	namespace memoryOps
	{
		template<typename, typename = void> struct HasReadImm8  : std::false_type {};
		template<typename, typename = void> struct HasReadImm16 : std::false_type {};
		template<typename, typename = void> struct HasReadImm32 : std::false_type {};
		template<typename, typename, typename = void> struct HasReadImmT  : std::false_type {};

		template<typename, typename = void> struct HasRead8  : std::false_type {};
		template<typename, typename = void> struct HasRead16 : std::false_type {};
		template<typename, typename = void> struct HasRead32 : std::false_type {};
		template<typename, typename, typename = void> struct HasReadT  : std::false_type {};

		template<typename T> struct HasReadImm8 <T, std::void_t<decltype(std::declval<T>().readImm8 (0))>> : std::true_type {};
		template<typename T> struct HasReadImm16<T, std::void_t<decltype(std::declval<T>().readImm16(0))>> : std::true_type {};
		template<typename T> struct HasReadImm32<T, std::void_t<decltype(std::declval<T>().readImm32(0))>> : std::true_type {};
		template<typename TClass, typename TData> struct HasReadImmT<TClass, TData, std::void_t<decltype(std::declval<TClass>().template readImm<TData>(0))>> : std::true_type {};

		template<typename T> struct HasRead8 <T, std::void_t<decltype(std::declval<T>().read8 (0))>> : std::true_type {};
		template<typename T> struct HasRead16<T, std::void_t<decltype(std::declval<T>().read16(0))>> : std::true_type {};
		template<typename T> struct HasRead32<T, std::void_t<decltype(std::declval<T>().read32(0))>> : std::true_type {};
		template<typename TClass, typename TData> struct HasReadT<TClass, TData, std::void_t<decltype(std::declval<TClass>().template read<TData>(0))>> : std::true_type {};

		template<typename TClass, bool Immediate> uint8_t read8(TClass& _c, const uint32_t _addr)
		{
			static_assert(!Immediate, "immediate 8 bit reads don't exist");
			if constexpr (Immediate && HasReadImm8<TClass>::value)
				return _c.readImm8(_addr);
			else
				return _c.read8(_addr);
		}

		template<typename TClass, bool Immediate> uint16_t read16(TClass& _c, const uint32_t _addr)
		{
			if constexpr (Immediate && HasReadImm16<TClass>::value)
				return _c.readImm16(_addr);
			return _c.read16(_addr);
		}

		template<typename TClass, bool Immediate> uint32_t defaultReadImm32(TClass& _c, const uint32_t _addr)
		{
			uint32_t res = static_cast<uint32_t>(read16<TClass,Immediate>(_c,_addr)) << 16;
			res |= read16<TClass,Immediate>(_c, _addr + 2);
			return res;
		}

		template<typename TClass, bool Immediate> uint32_t read32(TClass& _c, const uint32_t _addr)
		{
			if constexpr (Immediate)
			{
				if constexpr (HasReadImm32<TClass>::value)
					return _c.readImm32(_addr);
				else
					return defaultReadImm32<TClass, Immediate>(_c, _addr);
			}
			else
			{
				if constexpr (HasRead32<TClass>::value)
					return _c.read32(_addr);
				else
					return defaultReadImm32<TClass, Immediate>(_c, _addr);
			}
		}

		template<typename TClass, typename TData, bool Immediate> TData read(TClass& _c, const uint32_t _addr)
		{
			if constexpr (Immediate && HasReadImmT<TClass, TData>::value)		return _c.template readImm<TData>(0);
			else if constexpr (!Immediate && HasReadT<TClass, TData>::value)	return _c.template read<TData>(_addr);
			else if constexpr (sizeof(TData) == 1)								return read8<TClass, Immediate>(_c, _addr);
			else if constexpr (sizeof(TData) == 2)								return read16<TClass, Immediate>(_c, _addr);
			else if constexpr (sizeof(TData) == 4)								return read32<TClass, Immediate>(_c, _addr);
			else
			{
				static_assert(sizeof(TData) == 1 || sizeof(TData) == 2 || sizeof(TData) == 4, "invalid size");
				return 0;
			}
		}

		inline uint16_t readU16(const uint8_t* _buf, const size_t _offset)
		{
			const auto* ptr = &_buf[_offset];

			const auto v16 = *reinterpret_cast<const uint16_t*>(ptr);

			return endianSwap16IfLittle(v16);
		}

		inline uint16_t readU16(const std::vector<uint8_t>& _buf, size_t _offset)
		{
			return readU16(_buf.data(), _offset);
		}
		
		inline uint32_t readU32(const uint8_t* _buf, const size_t _offset)
		{
			const auto* ptr = &_buf[_offset];

			const auto v32 = *reinterpret_cast<const uint32_t*>(ptr);

			return endianSwap32IfLittle(v32);
		}

		inline uint32_t readU32(const std::vector<uint8_t>& _buf, const size_t _offset)
		{
			return readU32(_buf.data(), _offset);
		}

		// ______________________________
		// write

		template<typename, typename = void> struct HasWrite32 : std::false_type {};
		template<typename T> struct HasWrite32<T, std::void_t<decltype(std::declval<T>().write32(0,0))>> : std::true_type {};

		template<typename TClass> void defaultWrite32(TClass& _c, uint32_t _addr, const uint32_t _val)
		{
			_c.write16(_addr, _val >> 16);
			_c.write16(_addr + 2, _val & 0xffff);
		}

		template<typename TClass, typename TData> void write(TClass& _c, const uint32_t _addr, const TData _val)
		{
			if constexpr (sizeof(TData) == 1)
			{
				_c.write8(_addr, static_cast<uint8_t>(_val));
			}
			else if constexpr (sizeof(TData) == 2)
			{
				_c.write16(_addr, static_cast<uint16_t>(_val));
			}
			else if constexpr (sizeof(TData) == 4)
			{
				if constexpr (HasWrite32<TClass>::value)
					_c.write32(_addr, static_cast<uint32_t>(_val));
				else
					defaultWrite32(_c, _addr, static_cast<uint32_t>(_val));
			}
			else
			{
				static_assert(sizeof(TData) == 1 || sizeof(TData) == 2 || sizeof(TData) == 4, "invalid size");
			}
		}

		inline void writeU16(uint8_t* _buf, const size_t _offset, uint16_t _value)
		{
			auto* p8 = &_buf[_offset];
			auto* p16 = reinterpret_cast<uint16_t*>(p8);

			_value = endianSwap16IfLittle(_value);

			*p16 = _value;
		}

		inline void writeU16(std::vector<uint8_t>& _buf, const size_t _offset, const uint16_t _value)
		{
			writeU16(_buf.data(), _offset, _value);
		}
	}
}
