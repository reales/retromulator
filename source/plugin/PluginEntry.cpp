#include "HeadlessProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new retromulator::HeadlessProcessor();
}
