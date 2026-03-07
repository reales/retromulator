#include "xtRomLoader.h"

#include <cassert>
#include <map>
#include <algorithm>
#include <cstring>
#include <cstdio>

#include "baseLib/filesystem.h"

#include "wLib/wRom.h"

namespace xt
{
	constexpr uint32_t g_1Kb = 1024;

	constexpr uint32_t g_romSizeFull = 256 * g_1Kb;

	constexpr uint32_t g_romSizeHalf = 128 * g_1Kb;

	constexpr uint32_t g_midiSizeMin = 166 * g_1Kb;
	constexpr uint32_t g_midiSizeMax = 171 * g_1Kb;

	Rom RomLoader::findROM()
	{
		std::vector<File> allFiles;

		{
			// full roms are 256k in size
			auto filesFull = findFiles(".bin", g_romSizeFull, g_romSizeFull);

			for (auto& file : filesFull)
			{
				if(detectFileType(file) && detectVersion(file))
					allFiles.push_back(std::move(file));
			}
		}

		{
			// half roms are 128k in size, we need exactly two files to combine them
			// allow one extra leading byte (some IC dumps have a spurious 0x20 prefix byte)
		auto filesHalf = findFiles(".bin", g_romSizeHalf, g_romSizeHalf + 1);

			for(uint32_t i=0; i<static_cast<uint32_t>(filesHalf.size());)
			{
				auto& file = filesHalf[i];
				if(!detectFileType(file) || (file.type != FileType::HalfRomA && file.type != FileType::HalfRomB))
					filesHalf.erase(filesHalf.begin() + i);
				else
					++i;
			}

			if(filesHalf.size() > 2)
			{
				// remove possible duplicates
				for(size_t i=0; i<filesHalf.size(); ++i)
				{
					for(size_t j=i+1; j<filesHalf.size(); ++j)
					{
						if(filesHalf[i].data == filesHalf[j].data)
						{
							filesHalf.erase(filesHalf.begin() + static_cast<ptrdiff_t>(j));
							--j;
						}
					}
				}
			}

			if(filesHalf.size() == 2)
			{
				File& a = filesHalf.front();
				File& b = filesHalf.back();

				if(a.type == FileType::HalfRomB && b.type == FileType::HalfRomA)
					std::swap(a,b);

				if(a.type == FileType::HalfRomA && b.type == FileType::HalfRomB)
				{
					File result;
					result.data.reserve(g_romSizeFull);

					for(size_t i=0; i<g_romSizeHalf; ++i)
					{
						result.data.push_back(a.data[i]);
						result.data.push_back(b.data[i]);
					}

					assert(result.data[0] == 0xc0 && result.data[1] == 0xde);

					result.name = "MWXT_FW.bin";
					result.type = FileType::FullRom;

					if(detectVersion(result))
					{
						// Write combined ROM next to the source files and remove the two halves
						if(!a.path.empty() && !b.path.empty())
						{
							const auto dir = baseLib::filesystem::validatePath(baseLib::filesystem::getPath(a.path));
							const auto outPath = dir + result.name;
							if(baseLib::filesystem::writeFile(outPath, result.data.data(), result.data.size()))
							{
								std::remove(a.path.c_str());
								std::remove(b.path.c_str());
							}
						}
						allFiles.push_back(std::move(result));
					}
				}
			}
		}

		{
			// midi file OS update contain about half of the rom, wavetables are missing so an OS update cannot
			// be used on its own, but it can be used to upgrade an older rom by replacing the upper half with
			// the content of the midi OS update
			auto filesMidi = findFiles(".mid", g_midiSizeMin, g_midiSizeMax);

			for (auto& file : filesMidi)
			{
				std::vector<uint8_t> data;
				if(!wLib::ROM::loadFromMidiData(data, file.data))
					continue;
				if(!removeBootloader(data))
					continue;
				file.data = data;
				if(detectFileType(file) && detectVersion(file))
					allFiles.emplace_back(std::move(file));
			}
		}

		if(allFiles.empty())
			return Rom::invalid();

		std::map<FileType, std::vector<File>> romsByType;
		for (auto& file : allFiles)
		{
			romsByType[file.type].push_back(file);
		}

		auto& fullRoms = romsByType[FileType::FullRom];

		if(fullRoms.empty())
			return Rom::invalid();

		File best;

		// use the highest version that we have
		for (auto& fullRom : fullRoms)
		{
			if(fullRom.version > best.version)
				best = std::move(fullRom);
		}

		// apply OS update if we have any and the version of that upgrade is higher
		auto& midiUpgrades = romsByType[FileType::MidiUpdate];

		File bestMidi;
		for (auto& midi : midiUpgrades)
		{
			if(midi.version > bestMidi.version && midi.version > best.version)
				bestMidi = std::move(midi);
		}

		if(bestMidi.version)
		{
			assert(bestMidi.data.size() <= best.data.size());
			::memcpy(best.data.data(), bestMidi.data.data(), bestMidi.data.size());
			best.version = bestMidi.version;
			best.name += "_upgraded_" + bestMidi.name;
		}

		return {best.name, best.data};
	}

	std::vector<RomLoader::File> RomLoader::findFiles(const std::string& _extension, const size_t _sizeMin, const size_t _sizeMax)
	{
		const auto fileNames = synthLib::RomLoader::findFiles(_extension, _sizeMin, _sizeMax);

		std::vector<File> files;

		for (const auto& name : fileNames)
		{
			File f;
			if(!baseLib::filesystem::readFile(f.data, name))
				continue;

			f.name = baseLib::filesystem::getFilenameWithoutPath(name);
			f.path = name;
			files.emplace_back(std::move(f));
		}
		return files;
	}

	bool RomLoader::detectFileType(File& _file)
	{
		auto& data = _file.data;

		// Some IC dumps have a spurious leading byte; strip it so the file becomes g_romSizeHalf.
		if(data.size() == g_romSizeHalf + 1 && (data[1] == 0xc0 || data[1] == 0xde))
			data.erase(data.begin());

		switch (data.size())
		{
		case g_romSizeFull:
			// full rom starts with C0DE
			if(data[0] != 0xc0 || data[1] != 0xde)
				return false;
			_file.type = FileType::FullRom;
			return true;
		case g_romSizeHalf:
			// rom half starts with either C0 or DE
			if(data[0] == 0xc0 && data[1] == 0x00)
				_file.type = FileType::HalfRomA;
			else if(data[0] == 0xde && data[1] == 0x00)
				_file.type = FileType::HalfRomB;
			else
				return false;
			return true;
		default:
			// OS update starts with C0DE too
			if(data[0] != 0xc0 || data[1] != 0xde)
				return false;
			_file.type = FileType::MidiUpdate;
			return true;
		}
	}

	bool RomLoader::removeBootloader(std::vector<uint8_t>& _data)
	{
		if(_data.size() < 2)
			return false;

		const std::vector<uint8_t> pattern{0xc0, 0xde, 0x00, 0x00};

		const auto it = std::search(_data.begin(), _data.end(), pattern.begin(), pattern.end());

		if(it == _data.end())
			return false;

		if(it != _data.begin())
			_data.erase(_data.begin(), it);

		return true;
	}

	bool RomLoader::detectVersion(File& _file)
	{
		_file.version = 0;

		const auto& data = _file.data;
		if(data.size() < 0x33)
			return false;
		if(data[0x30] != '.')
			return false;

		_file.version = (data[0x2f] - '0') * 100 + (data[0x31] - '0') * 10 + (data[0x32] - '0');

		return _file.version > 200;
	}
}
