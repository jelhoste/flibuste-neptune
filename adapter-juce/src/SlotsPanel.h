#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Theme.h"

// Emplacements assignables du Sampler (fenêtre flottante) : 16 lignes {cible, paramètre, valeur}. La cible est un Groupe, ou la
// zone sélectionnée dans la liste ; la valeur est le paramètre automatisable de l'emplacement (relative à la zone, neutre à 0).
class SlotsPanel : public juce::Component, private juce::Timer
{
public:
    SlotsPanel (ParamHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, int track);
    ~SlotsPanel() override;

    std::function<neptune::core::InstrumentDescription()> getDescription;
    std::function<std::vector<int>()> getSelectedZones;           // lignes sélectionnées dans la liste des zones
    std::function<void (neptune::core::InstrumentDescription)> onEdit;

    void refresh();                                               // relit la description (cibles, paramètres)
    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int panelWidth = 780, panelHeight = 560;

private:
    struct Row;
    void timerCallback() override;
    void assign (int slot, int targetId, int paramId);
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    juce::String zoneLabel (const neptune::core::InstrumentDescription&, int zone) const;

    ParamHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    int track_;
    juce::OwnedArray<Row> rows_;
    juce::Label hint_;
    std::string signature_;
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotsPanel)
};
