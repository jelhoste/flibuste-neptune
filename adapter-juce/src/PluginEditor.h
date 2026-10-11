#pragma once
#include "PluginProcessor.h"

class NeptuneEditor : public juce::AudioProcessorEditor
{
public:
    explicit NeptuneEditor (NeptuneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
