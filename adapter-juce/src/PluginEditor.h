#pragma once
#include "PluginProcessor.h"

// Fenêtre de sonde : affiche quelques paramètres en direct (automation de l'hôte, MIDI learn, etc.).
class NeptuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeptuneEditor (NeptuneProcessor&);
    ~NeptuneEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    void timerCallback() override { repaint(); }

    NeptuneProcessor& processor_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
