#pragma once
// Réserve de fichiers de l'instrument : les samples disponibles, placés ou non. Une ligne par fichier (nom, durée, canaux, fréquence, ce que le fichier apporte :
// boucles, note racine, marqueur de volume), et combien de zones l'utilisent. On glisse des lignes sur la matrice pour créer des zones.
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

class PoolTable : public juce::Component, private juce::TableListBoxModel
{
public:
    struct Row
    {
        std::string path;
        juce::String name, length, channels, rate, pitch, contents;
        int used = 0;
        bool ok = true;
    };

    PoolTable (const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr) : palette_ (palette)
    {
        auto name = [&tr] (const char* key) { return juce::String::fromUTF8 (std::string (tr.tr (key)).c_str()); };
        auto& h = table_.getHeader();
        const auto flags = juce::TableHeaderComponent::notSortable;
        h.addColumn (name ("pool.col_file"), 1, 170, 60, 600, flags);
        h.addColumn (name ("pool.col_length"), 2, 62, 40, 120, flags);
        h.addColumn (name ("pool.col_channels"), 3, 34, 24, 70, flags);
        h.addColumn (name ("pool.col_rate"), 4, 56, 40, 100, flags);
        h.addColumn (name ("pool.col_pitch"), 5, 64, 40, 110, flags);
        h.addColumn (name ("pool.col_contents"), 6, 150, 60, 400, flags);
        h.addColumn (name ("pool.col_used"), 7, 40, 30, 80, flags);
        table_.setMultipleSelectionEnabled (true);
        table_.setRowHeight (24);
        table_.setColour (juce::ListBox::backgroundColourId, colour (neptune::core::theme::Token::BgCard));
        table_.setColour (juce::ListBox::outlineColourId, colour (neptune::core::theme::Token::Border));
        table_.setOutlineThickness (1);
        addAndMakeVisible (table_);
    }

    void setRows (std::vector<Row> rows)
    {
        const auto selection = table_.getSelectedRows();
        rows_ = std::move (rows);
        table_.updateContent();
        juce::SparseSet<int> keep;
        for (int i = 0; i < selection.size(); ++i)
            if (selection[i] < static_cast<int> (rows_.size())) keep.addRange ({ selection[i], selection[i] + 1 });
        table_.setSelectedRows (keep, juce::dontSendNotification);
        table_.repaint();
    }
    std::vector<int> selectedRows() const
    {
        std::vector<int> r;
        const auto sel = table_.getSelectedRows();
        for (int i = 0; i < sel.size(); ++i) r.push_back (sel[i]);
        return r;
    }
    int numRows() const { return static_cast<int> (rows_.size()); }

    std::function<void (int row)> onContextMenu;   // clic droit sur une ligne
    std::function<void()> onSelectionChanged;

    void resized() override { table_.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return static_cast<int> (rows_.size()); }
    void paintRowBackground (juce::Graphics& g, int, int, int, bool selected) override
    {
        if (selected) g.fillAll (colour (neptune::core::theme::Token::Nav1));
    }
    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool) override
    {
        if (row < 0 || row >= static_cast<int> (rows_.size())) return;
        const auto& r = rows_[static_cast<std::size_t> (row)];
        juce::String text;
        switch (column)
        {
            case 1: text = r.name; break;
            case 2: text = r.length; break;
            case 3: text = r.channels; break;
            case 4: text = r.rate; break;
            case 5: text = r.pitch; break;
            case 6: text = r.contents; break;
            case 7: text = r.used > 0 ? juce::String (r.used) : juce::String(); break;
            default: break;
        }
        using neptune::core::theme::Token;
        g.setColour (colour (! r.ok ? Token::Accent : r.used > 0 || column == 1 ? Token::Text : Token::TextDim));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { if (onSelectionChanged) onSelectionChanged(); }
    void cellClicked (int row, int, const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onContextMenu) onContextMenu (row); }
    // Glisser des lignes vers la matrice : « neptune-files:<indices> ». Le conteneur de glisser-déposer est l'éditeur.
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        juce::StringArray parts;
        for (int i = 0; i < rows.size(); ++i) parts.add (juce::String (rows[i]));
        return "neptune-files:" + parts.joinIntoString (",");
    }
    juce::Colour colour (neptune::core::theme::Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }

    neptune::core::theme::Palette palette_;
    std::vector<Row> rows_;
    juce::TableListBox table_ { "pool", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoolTable)
};
