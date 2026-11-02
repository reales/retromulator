#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace matrixLib
{
	struct RomSet;

	// Writes "!ROM Bank 2..9.syx" from the original firmware and patch EPROM: boots the firmware
	// headless, asks it for every patch of banks 2-9 and names them. The sound engine never runs
	// the firmware; this is the only user of it
	class RomBankGenerator
	{
	public:
		static std::string fileName(uint32_t _bank);
		static bool isMissing(const std::string& _folder);

		// blocking, about half a minute of machine time; writes only the missing files, each
		// one atomically. False when the ROMs are invalid, a dump failed or _cancel was set
		static bool generate(const std::string& _folder, const RomSet& _roms, const std::atomic<bool>* _cancel = nullptr);

		// once per process, on a background thread, when files are missing and both ROMs are
		// installed. Any thread
		static void startOnce(const std::string& _folder);
		// counts finished background runs that wrote files, for views to rescan the folder
		static uint32_t completedRuns();
	};
}
