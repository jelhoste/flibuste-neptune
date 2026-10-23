#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
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
    void setPlayingRows (const std::vector<int>& rows); // lignes dont une voix sonne (surbrillance, moniteur de voix)
    void setPreviewRow (int row) { if (row != previewRow_) { previewRow_ = row; table_.repaint(); } } // zone en cours d'écoute (-1 : aucune) : son bouton devient un carré
    int previewRow() const { return previewRow_; }
    void setSelectedRows (const std::vector<int>&, bool notify = true);
    std::function<void()> onSelectionChanged;
    // Modification d'une cellule : colonne 7 (déclenchement : 1 attaque, 2 relâchement, 3 premier, 4 legato), 8 (groupe de choke
    // coupé) et 9 (groupe de choke qui coupe) : 0 à maxChokeGroup. L'éditeur décide à quelles lignes l'appliquer.
    std::function<void (int row, int column, int value)> onCellEdited;
    // Bouton d'écoute : `down` à l'appui, relâché ensuite ; `stop` = Maj enfoncé (coupe l'écoute).
    std::function<void (int row, bool down, bool stop)> onPlay;
    // Clic droit sur une ligne (menu contextuel : assigner la zone à un emplacement, etc.).
    std::function<void (int row)> onContextMenu;

    void resized() override
    {
        auto b = getLocalBounds();
        detail_.setBounds (b.removeFromBottom (std::min (140, b.getHeight() / 3)));
        table_.setBounds (b);
    }

private:
    int getNumRows() override { return static_cast<int> (zones_.size()); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    // Glisser une ou plusieurs lignes (vers la matrice) : « neptune-zones:<indices> ». Le conteneur de glisser-déposer est l'éditeur.
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        juce::StringArray parts;
        for (int i = 0; i < rows.size(); ++i) parts.add (juce::String (rows[i]));
        return "neptune-zones:" + parts.joinIntoString (",");
    }
    void selectedRowsChanged (int) override { updateDetail(); if (onSelectionChanged) onSelectionChanged(); }
    void updateDetail();   // tout ce qu'on sait de la première zone sélectionnée, en pleine longueur
    void cellClicked (int row, int, const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onContextMenu) onContextMenu (row); }
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::vector<neptune::core::ZoneDescription> zones_;
    std::vector<int> playing_;
    int previewRow_ = -1;
    juce::TableListBox table_ { "zones", this };
    juce::TextEditor detail_;
    class ComboCell;
    class PlayCell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneTable)
};
