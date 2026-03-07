#include "romloader.h"

#include <cstdio>

#include "baseLib/filesystem.h"

namespace mqLib
{
	ROM RomLoader::findROM()
	{
		const auto midiFiles = findFiles(".mid", 300 * 1024, 400 * 1024);

		for (const auto& midiFile : midiFiles)
		{
			ROM rom(midiFile);
			if(rom.isValid())
			{
				// Write the assembled ROM as microq.bin next to the source file and delete the .mid.
				const auto dir = baseLib::filesystem::validatePath(baseLib::filesystem::getPath(midiFile));
				const auto outPath = dir + "micro_q.bin";
				if(baseLib::filesystem::writeFile(outPath, rom.getData()))
					std::remove(midiFile.c_str());
				return rom;
			}
		}

		const auto binFiles = findFiles(".bin", 512 * 1024, 512 * 1024);

		for (const auto& binFile : binFiles)
		{
			ROM rom(binFile);
			if(rom.isValid())
				return rom;
		}
		return {};
	}
}
