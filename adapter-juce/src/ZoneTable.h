#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/Theme.h"

// Liste des zones de l'instrument (palier 1, jalon 3) : fichier, note racine, étendues, round-robin, mode.
// Multi-sélection ; la sélection désigne les zones que les réglages (mode, note racine, enveloppe) modifient.
class ZoneTable : public juce::Component, private juce::TableListBoxModel
{
public:
    ZoneTable (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);

    void setZones (const std::vector<neptune::core::ZoneDescription>&); // garde la sélection quand c'est possible
    std::vector<int> selectedRows() const;
    std::function<void()> onSelectionChanged;

    void resized() override { table_.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return static_cast<int> (zones_.size()); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { if (onSelectionChanged) onSelectionChanged(); }
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::vector<neptune::core::ZoneDescription> zones_;
    juce::TableListBox table_ { "zones", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneTable)
};
