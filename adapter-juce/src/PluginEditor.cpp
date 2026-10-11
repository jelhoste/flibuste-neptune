#include "PluginEditor.h"
#include "neptune/core/ProbePool.h"

NeptuneEditor::NeptuneEditor (NeptuneProcessor& p) : juce::AudioProcessorEditor (p), processor_ (p)
{
    setSize (1160, 780);
    setResizable (true, true);
    startTimerHz (30);
}

void NeptuneEditor::paint (juce::Graphics& g)
{
    using namespace neptune::core;
    g.fillAll (juce::Colour (0xff12100e));

    auto area = getLocalBounds().reduced (32);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText ("Neptune", area.removeFromTop (40), juce::Justification::centredLeft);

    auto& ps = processor_.parameters();
    g.setColour (juce::Colour (0xff9a9088));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (juce::String (static_cast<int> (ps.size())) + " parameters exposed to the host ("
                    + juce::String (processor_.probeTracks()) + " probe tracks)",
                area.removeFromTop (24), juce::Justification::centredLeft);
    area.removeFromTop (12);

    struct Row { const char* label; ParamId id; };
    const Row rows[] = {
        { "Master Volume",       probe::masterVolume },
        { "Master Pan",          probe::masterPan },
        { "Master Mute",         probe::masterMute },
        { "FM Algorithm (1-32)", probe::algorithm },
        { "Track 1 Volume",      probe::trackParam (0, probe::kVolume) },
        { "Track 1 Pan",         probe::trackParam (0, probe::kPan) },
        { "Track 1 Mute",        probe::trackParam (0, probe::kMute) },
        { "Track 1 Solo",        probe::trackParam (0, probe::kSolo) },
        { "Track 1 Type",        probe::trackParam (0, probe::kType) },
        { "Track 1 Insert 1 Bypass", probe::trackParam (0, probe::kInsertBypass) },
        { "Track 1 Send 1 Level",    probe::trackParam (0, probe::kSendLevel) },
    };

    g.setFont (juce::FontOptions (16.0f));
    for (const auto& r : rows)
    {
        auto line = area.removeFromTop (28);
        g.setColour (juce::Colour (0xff9a9088));
        g.drawText (r.label, line.removeFromLeft (260), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xffe8823f));
        g.drawText (juce::String::fromUTF8 (ps.toText (r.id).c_str()), line.removeFromLeft (160), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff6f665f));
        g.drawText ("changes: " + juce::String (static_cast<int> (ps.version (r.id))), line, juce::Justification::centredLeft);
    }
}
