#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "NovaKit.h"
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Theme.h"

// Emplacements assignables du Sampler (fenêtre superposée, comme celle des modulateurs) : 16 lignes {cible, paramètre, valeur}. La cible est un Groupe, ou la
// zone sélectionnée dans la liste ; la valeur est le paramètre automatisable de l'emplacement (relative à la zone, neutre à 0).
class SlotsPanel : public juce::Component, private juce::Timer
{
public:
    SlotsPanel (ParamHost&, const nova::Skin&, const neptune::core::i18n::Translator&, int track);
    ~SlotsPanel() override;

    std::function<neptune::core::InstrumentDescription()> getDescription;
    std::function<std::vector<int>()> getSelectedZones;           // lignes sélectionnées dans la liste des zones
    std::function<void (neptune::core::InstrumentDescription)> onEdit;
    std::function<void()> onClose;

    void refresh();                                               // relit la description (cibles, paramètres)
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override {}   // fenêtre modale : un clic dans le vide ne traverse pas
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Row;
    void timerCallback() override;
    void assign (int slot, int targetId, int paramId);
    juce::Colour colour (neptune::core::theme::Token t) const { return skin_.colour (t); }
    juce::Rectangle<int> panelBounds() const { return getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 880), std::min (getHeight() - 30, 620)); }
    juce::String text (const char* key) const;
    juce::String zoneLabel (const neptune::core::InstrumentDescription&, int zone) const;

    ParamHost& host_;
    const nova::Skin& skin_;
    nova::LookAndFeel look_;
    const neptune::core::i18n::Translator& tr_;
    int track_;
    juce::OwnedArray<Row> rows_;
    juce::Label hint_, title_;
    juce::TextButton closeButton_;
    std::string signature_;
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotsPanel)
};
