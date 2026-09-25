#include "romloader.h"

#include "synthLib/romLoader.h"
#include "baseLib/filesystem.h"

namespace matrixLib
{
	bool RomLoader::isFirmware(const std::vector<uint8_t>& _data)
	{
		if(_data.size() != 0x8000)
			return false;

		// every Matrix-1000 release starts at the reset vector with CLR $1D80 (bank latch)
		const uint16_t reset = static_cast<uint16_t>((_data[0x7ffe] << 8) | _data[0x7fff]);
		if(reset < 0x8000 || reset > 0xfff0)
			return false;
		const auto* p = &_data[reset - 0x8000];
		return p[0] == 0x7f && p[1] == 0x1d && p[2] == 0x80;
	}

	bool RomLoader::isPatchRom(const std::vector<uint8_t>& _data)
	{
		if(_data.size() != 0x10000)
			return false;

		// eight 8 KB pages, each holding 100 records of 80 bytes; the unused tail of every
		// page is left at the programmer's fill values
		for(uint32_t bank = 0; bank < 8; ++bank)
		{
			const auto* page = &_data[bank * 0x2000];
			bool hasData = false;
			for(uint32_t i = 0; i < 8000 && !hasData; ++i)
				hasData = page[i] != 0xff;
			if(!hasData)
				return false;
			for(uint32_t i = 8000; i < 0x2000; ++i)
			{
				if(page[i] != 0x00 && page[i] != 0x01 && page[i] != 0xff)
					return false;
			}
		}
		return true;
	}

	RomSet RomLoader::findROM()
	{
		RomSet set;

		for(const auto& file : synthLib::RomLoader::findFiles(".bin", 0x8000, 0x10000))
		{
			std::vector<uint8_t> data;
			if(!baseLib::filesystem::readFile(data, file))
				continue;

			if(set.firmware.empty() && isFirmware(data))
			{
				set.firmwareFile = file;
				set.firmware = std::move(data);
			}
			else if(set.patchRom.empty() && isPatchRom(data))
			{
				set.patchRomFile = file;
				set.patchRom = std::move(data);
			}

			if(set.isValid())
				break;
		}
		return set;
	}
}
