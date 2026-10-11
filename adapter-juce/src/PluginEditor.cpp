#include "PluginEditor.h"

NeptuneEditor::NeptuneEditor (NeptuneProcessor& p) : juce::AudioProcessorEditor (p)
{
    setSize (1160, 780);
    setResizable (true, true);
}

void NeptuneEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff12100e));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f));
    g.drawText ("Neptune", getLocalBounds(), juce::Justification::centred);
}
