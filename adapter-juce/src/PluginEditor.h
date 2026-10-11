#pragma once
#include "PluginProcessor.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

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
    // Langue, thème et contraste viennent de settings.json, lus une fois au démarrage de l'instance.
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (std::string_view key) const { return juce::String::fromUTF8 (tr_.tr (key).data(), static_cast<int> (tr_.tr (key).size())); }
    juce::Label latencyLabel_;
    juce::Label recoveryLabel_;
    juce::Label profileLabel_;
    juce::TextButton profileSwitchButton_, profileLockButton_;
    void updateProfileControls();
    juce::TextButton restoreButton_, discardButton_;
    bool recoveryShown_ = false;
    void updateRecoveryBanner();
    juce::OwnedArray<juce::TextButton> latencyButtons_; // test de la latence modifiable à l'exécution
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
