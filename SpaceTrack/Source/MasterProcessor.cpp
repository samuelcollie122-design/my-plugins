#include "MasterProcessor.h"
#include "MasterEditor.h"
#include "SharedRegistry.h"

void SpaceMasterProcessor::publishRoom()
{
    if (auto* reg = getSharedRegistry())
    {
        reg->roomSize.store (roomSize);
        reg->roomAmount.store (roomAmount);
        reg->roomBeat.store (juce::Time::currentTimeMillis());
    }
}

juce::AudioProcessorEditor* SpaceMasterProcessor::createEditor()
{
    return new SpaceMasterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceMasterProcessor();
}
