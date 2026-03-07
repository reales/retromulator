#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "synthLib/romLoader.h"

namespace dx7Emu
{

class RomData
{
public:
	RomData() = default;
	RomData(const std::string& _filename, std::vector<uint8_t> _data)
		: m_filename(_filename), m_data(std::move(_data)) {}

	bool isValid() const { return m_data.size() == 16384; }
	const std::string& getFilename() const { return m_filename; }
	const std::vector<uint8_t>& getData() const { return m_data; }

private:
	std::string m_filename;
	std::vector<uint8_t> m_data;
};

class RomLoader : synthLib::RomLoader
{
public:
	static RomData findROM();
};

} // namespace dx7Emu
