#include "tools.h"

#include "baseLib/filesystem.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace pluginLib
{
	bool Tools::isHeadless()
	{
		// returns false on a build machine without display even...
		if(juce::Desktop::getInstance().isHeadless())
			return true;

		const auto host = juce::PluginHostType::getHostPath();

		// So we use this instead. These tools cause crashes if you attempt to
		// open a message box. LV2 even opens the editor, even on a headless
		// build machine, whatever that is good for
		return host.contains("juce_vst3_helper") || host.contains("juce_lv2_helper");
	}

	std::string Tools::getPublicDataFolder(const std::string& _vendorName, const std::string& _productName)
	{
#ifdef __APPLE__
		// On macOS use ~/Library/Application Support/ to avoid triggering a TCC Documents permission prompt.
		const auto folderType = baseLib::filesystem::SpecialFolderType::PrivateAppData;
#else
		const auto folderType = baseLib::filesystem::SpecialFolderType::UserDocuments;
#endif
		return baseLib::filesystem::validatePath(baseLib::filesystem::getSpecialFolderPath(folderType) + _vendorName + '/' + _productName + '/');
	}
}
