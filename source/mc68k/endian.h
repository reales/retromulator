#pragma once

#ifdef __cplusplus
#  include <cstdint>
#else
#  include <stdint.h>
#endif

#if defined(_MSC_VER)   // MSVC
#	include <intrin.h>
#	define BSWAP32(x) _byteswap_ulong(x)
#	define BSWAP16(x) _byteswap_ushort(x)
#elif defined(__GNUC__) || defined(__clang__)  // GCC / Clang
#	define BSWAP32(x) __builtin_bswap32(x)
#	define BSWAP16(x) __builtin_bswap16(x)
#else
#	define BSWAP32(x) (((x) >> 24) | (((x) >> 8) & 0x0000FF00) | (((x) << 8) & 0x00FF0000) | ((x) << 24))
#	define BSWAP16(x) ((uint16_t)((uint16_t)(x) >> 8) | (uint16_t)((uint16_t)(x) << 8))
#endif

#ifdef __cplusplus
namespace mc68k
{
	enum class HostEndian : uint8_t
	{
		Big,
		Little
	};

	static constexpr HostEndian hostEndian()
	{
		constexpr uint32_t test32 = 0x01020304;
		constexpr uint8_t test8 = static_cast<const uint8_t&>(test32);

		static_assert(test8 == 0x01 || test8 == 0x04, "unable to determine endianess");

		return test8 == 0x01 ? HostEndian::Big : HostEndian::Little;
	}

	inline uint32_t endianSwap32IfLittle(const uint32_t _nativeEndianVal)
	{
		if constexpr (hostEndian() == HostEndian::Little)
			return BSWAP32(_nativeEndianVal);
		return _nativeEndianVal;
	}

	inline uint16_t endianSwap16IfLittle(const uint16_t _nativeEndianVal)
	{
		if constexpr (hostEndian() == HostEndian::Little)
			return BSWAP16(_nativeEndianVal);
		return _nativeEndianVal;
	}
}
#endif // __cplusplus
