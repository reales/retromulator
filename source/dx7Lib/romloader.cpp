#include "romloader.h"

#include "baseLib/filesystem.h"

namespace dx7Emu
{

RomData RomLoader::findROM()
{
	// DX7 firmware ROM is exactly 16384 bytes (16 KB)
	auto files = synthLib::RomLoader::findFiles(".bin", 16384, 16384);

	for(const auto& file : files)
	{
		std::vector<uint8_t> data;
		if(!baseLib::filesystem::readFile(data, file))
			continue;

		if(data.size() != 16384)
			continue;

		// Basic validation: check for known DX7 v1.8 ROM signature
		// The reset vector at 0x3FFE-0x3FFF should point to a valid address
		// For v1.8 firmware, the ROM starts execution around 0xC000
		uint16_t resetVector = (static_cast<uint16_t>(data[0x3FFE]) << 8) | data[0x3FFF];
		if(resetVector < 0xC000 || resetVector > 0xFFFF)
			continue;

		return RomData(file, std::move(data));
	}

	return {};
}

} // namespace dx7Emu
