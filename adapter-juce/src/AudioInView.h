#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Vue d'une piste « Entrée audio » : pas d'instrument. Elle dit d'où vient le signal : l'entrée audio de l'hôte (interrupteur) et les envois
// des autres pistes, des bus ne pouvant pas lui en envoyer. Le trim est le potard de gain de la piste (rangée de potards du bas).
class AudioInView : public juce::Component
{
public:
    AudioInView (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);

    void setTrack (int track) { track_ = track; refresh(); }
    void refresh();                                  // relit l'interrupteur et la liste des sources
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token) const;

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    int track_ = 0;
    bool updating_ = false;
    juce::ToggleButton hostInput_;
    juce::Label hint_, sources_;
    juce::String shownSources_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioInView)
};
