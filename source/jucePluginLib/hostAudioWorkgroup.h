#pragma once

#include <juce_events/juce_events.h>

namespace pluginLib
{
	// macOS: hands the IO workgroup of the host's audio device to the emulation threads, see dsp56k::AudioWorkgroup.
	// Only AU can ask the host for it, but the host runs its device in our process and CoreAudio tells which one
	class HostAudioWorkgroup : juce::Timer
	{
	public:
		HostAudioWorkgroup();
		~HostAudioWorkgroup() override;

	private:
		void timerCallback() override;
	};
}
