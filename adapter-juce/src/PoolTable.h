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
        for (const char* k : { "pool.col_file", "pool.detail_path", "pool.col_length", "pool.col_channels", "pool.col_rate", "pool.col_pitch", "pool.col_contents", "pool.detail_used", "pool.detail_empty" })
            labels_.add (name (k));
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
        detail_.setMultiLine (true, true);
        detail_.setReadOnly (true);
        detail_.setCaretVisible (false);
        detail_.setScrollbarsShown (true);
        detail_.setFont (juce::Font (juce::FontOptions (12.5f)));
        detail_.setColour (juce::TextEditor::backgroundColourId, colour (neptune::core::theme::Token::BgCard));
        detail_.setColour (juce::TextEditor::textColourId, colour (neptune::core::theme::Token::Text));
        detail_.setColour (juce::TextEditor::outlineColourId, colour (neptune::core::theme::Token::Border));
        detail_.setColour (juce::TextEditor::focusedOutlineColourId, colour (neptune::core::theme::Token::Border));
        addAndMakeVisible (detail_);
        updateDetail();
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
        updateDetail();
    }
    // Ligne dont le fichier est en cours d'écoute (-1 : aucune) : son bouton devient un carré
    void setPlayingRow (int row)
    {
        if (row == playing_) return;
        playing_ = row;
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
    std::function<void (int row)> onDoubleClick;   // double-clic sur une ligne : écouter le fichier
    std::function<void (int row)> onPlay;          // clic sur le bouton de lecture d'une ligne
    std::function<void()> onSelectionChanged;

    void resized() override
    {
        auto b = getLocalBounds();
        detail_.setBounds (b.removeFromBottom (std::min (112, b.getHeight() / 3)));
        table_.setBounds (b);
    }

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
        if (column == 1)
        {
            // bouton de lecture : triangle, ou carré pendant l'écoute
            const auto box = juce::Rectangle<float> (4.0f, (static_cast<float> (height) - 12.0f) * 0.5f, 12.0f, 12.0f);
            g.setColour (colour (row == playing_ ? Token::Accent : Token::TextDim));
            if (row == playing_) g.fillRect (box.reduced (1.0f));
            else
            {
                juce::Path tri;
                tri.addTriangle (box.getX() + 1.0f, box.getY(), box.getX() + 1.0f, box.getBottom(), box.getRight(), box.getCentreY());
                g.fillPath (tri);
            }
            g.setColour (colour (! r.ok ? Token::Accent : r.used > 0 || column == 1 ? Token::Text : Token::TextDim));
            g.drawText (text, kPlayWidth + 2, 0, width - kPlayWidth - 4, height, juce::Justification::centredLeft, true);
            return;
        }
        g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
    }
    void selectedRowsChanged (int) override { updateDetail(); if (onSelectionChanged) onSelectionChanged(); }
    void cellDoubleClicked (int row, int, const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick (row); }
    void cellClicked (int row, int column, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onContextMenu) onContextMenu (row); return; }
        if (column != 1 || row < 0 || row >= static_cast<int> (rows_.size()) || ! onPlay) return;
        // abscisse dans la colonne du nom (1 pixel de contour, défilement horizontal éventuel)
        const int left = table_.getHeader().getColumnPosition (table_.getHeader().getIndexOfColumnId (1, true)).getX();
        const int viewX = table_.getViewport() != nullptr ? table_.getViewport()->getViewPositionX() : 0;
        const int x = e.getEventRelativeTo (&table_).x - 1 - (left - viewX);
        if (x >= 0 && x < kPlayWidth) onPlay (row);
    }
    // Tout ce qu'on sait du premier fichier sélectionné, en pleine longueur
    void updateDetail()
    {
        const auto sel = table_.getSelectedRows();
        if (sel.size() == 0 || sel[0] >= static_cast<int> (rows_.size())) { detail_.setText (labels_[8], false); return; }
        const auto& r = rows_[static_cast<std::size_t> (sel[0])];
        juce::String t;
        t << labels_[0] << ":  " << r.name << "\n" << labels_[1] << ":  " << juce::String::fromUTF8 (r.path.c_str()) << "\n";
        if (r.ok)
            t << labels_[2] << ":  " << r.length << "    " << labels_[3] << ":  " << r.channels << "    " << labels_[4] << ":  " << r.rate
              << "    " << labels_[5] << ":  " << (r.pitch.isEmpty() ? juce::String ("-") : r.pitch) << "\n";
        t << labels_[6] << ":  " << (r.contents.isEmpty() ? juce::String ("-") : r.contents) << "\n" << labels_[7] << ":  " << r.used;
        if (sel.size() > 1) t << "\n(+" << (sel.size() - 1) << ")";
        detail_.setText (t, false);
    }
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
    static constexpr int kPlayWidth = 20;
    juce::TableListBox table_ { "pool", this };
    juce::TextEditor detail_;
    juce::StringArray labels_;   // noms des champs du panneau de détail
    int playing_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoolTable)
};
