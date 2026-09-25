#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace matrixLib
{
	struct RomSet
	{
		std::string firmwareFile;
		std::vector<uint8_t> firmware;      // 32 KB system EPROM
		std::string patchRomFile;
		std::vector<uint8_t> patchRom;      // 64 KB patch EPROM, banks 2-9

		bool isValid() const { return firmware.size() == 0x8000 && patchRom.size() == 0x10000; }
	};

	class RomLoader
	{
	public:
		static RomSet findROM();

		static bool isFirmware(const std::vector<uint8_t>& _data);
		static bool isPatchRom(const std::vector<uint8_t>& _data);
	};
}
