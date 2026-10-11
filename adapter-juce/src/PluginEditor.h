#pragma once
#include "PluginProcessor.h"

// Fenêtre de sonde : paramètres en direct, et ce que l'hôte envoie (transport, notes CLAP, expressions, modulation).
class NeptuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeptuneEditor (NeptuneProcessor&);
    ~NeptuneEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    NeptuneProcessor& processor_;
    juce::Label latencyLabel_;
    juce::Label recoveryLabel_;
    juce::TextButton restoreButton_ { "Restore" }, discardButton_ { "Discard" };
    bool recoveryShown_ = false;
    void updateRecoveryBanner();
    juce::OwnedArray<juce::TextButton> latencyButtons_; // test de la latence modifiable à l'exécution
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
