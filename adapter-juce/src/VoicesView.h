#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Moniteur des voix (débogage) : ce qui sonne à cet instant, avec la zone, la touche, la vélocité et l'état. Les notes déjà
// finies restent affichées en grisé quelques instants (les notes courtes passeraient sinon inaperçues).
class VoicesView : public juce::Component, private juce::TableListBoxModel
{
public:
    struct Row
    {
        juce::String sample, key, velocity, zone, trigger, group, choke, state, layerGain, level, source;
        float progress = 0.0f;
        bool finished = false;  // terminée depuis peu (grisée)
        bool fading = false, releasing = false, preview = false;
    };

    VoicesView (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);

    void setRows (std::vector<Row>, const juce::String& summary);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override { return static_cast<int> (rows_.size()); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::vector<Row> rows_;
    juce::String summary_;
    juce::TableListBox table_ { "voices", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicesView)
};
