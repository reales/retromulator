#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <set>

#include "baseLib/filesystem.h"

namespace synthLib
{
	class RomLoader
	{
	public:
		static std::vector<std::string> findFiles(const std::string& _extension, size_t _minSize, size_t _maxSize);
		static std::vector<std::string> findFiles(const std::string& _path, const std::string& _extension, size_t _minSize, size_t _maxSize);

		// As findFiles, but each search path marked recursive is descended into, and each result
		// carries the size that was looked up on the way. Use where ROMs are identified by content
		// rather than by name, so a user can drop a whole collection into the ROM folder unsorted.
		static std::vector<baseLib::filesystem::FoundFile> findFilesRecursive(const std::string& _extension, size_t _minSize, size_t _maxSize);

		// _recursive marks a path as one the caller owns and wants descended into.
		static void addSearchPath(const std::string& _path, bool _recursive = false);
	};
}
