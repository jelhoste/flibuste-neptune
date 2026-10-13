#include "ZoneTable.h"
#include <algorithm>

using neptune::core::theme::Token;

ZoneTable::ZoneTable (const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr)
    : palette_ (palette), tr_ (tr)
{
    auto name = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    auto& h = table_.getHeader();
    h.addColumn (name ("import.col_file"), 1, 170, 60, 600, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
    h.addColumn (name ("import.col_note"), 2, 50, 36, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("import.col_keys"), 3, 70, 40, 140, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("import.col_velocity"), 4, 70, 40, 140, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("import.col_rr"), 5, 34, 24, 80, juce::TableHeaderComponent::notSortable);
    h.addColumn ("Mode", 6, 64, 40, 120, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("trig.label"), 7, 104, 80, 160, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("choke.cuts_short"), 8, 62, 50, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("choke.cut_by_short"), 9, 74, 50, 120, juce::TableHeaderComponent::notSortable);
    h.addColumn (juce::String::fromUTF8 ("\xe2\x96\xb6"), 10, 36, 30, 60, juce::TableHeaderComponent::notSortable);
    table_.setMultipleSelectionEnabled (true);
    table_.setRowHeight (24);
    table_.setColour (juce::ListBox::backgroundColourId, colour (Token::BgCard));
    table_.setColour (juce::ListBox::outlineColourId, colour (Token::Border));
    table_.setOutlineThickness (1);
    addAndMakeVisible (table_);
}

// Cellule à liste déroulante (déclenchement, groupes de choke) : modifie la ligne, ou toute la sélection si la ligne en fait partie.
class ZoneTable::ComboCell : public juce::Component
{
public:
    ComboCell (ZoneTable& owner, int column) : owner_ (owner), column_ (column)
    {
        if (column == 7)
        {
            auto name = [&owner] (const char* key) { return juce::String::fromUTF8 (std::string (owner.tr_.tr (key)).c_str()); };
            box_.addItem (name ("trig.attack"), 1);
            box_.addItem (name ("trig.release"), 2);
            box_.addItem (name ("trig.first"), 3);
            box_.addItem (name ("trig.legato"), 4);
        }
        else
        {
            box_.addItem (juce::String::fromUTF8 (std::string (owner.tr_.tr ("choke.none")).c_str()), 1);
            for (int g = 1; g <= neptune::core::maxChokeGroup; ++g) box_.addItem (juce::String (g), g + 1);
        }
        box_.onChange = [this]
        {
            if (updating_ || box_.getSelectedId() < 1 || ! owner_.onCellEdited) return;
            owner_.onCellEdited (row_, column_, column_ == 7 ? box_.getSelectedId() : box_.getSelectedId() - 1);
        };
        addAndMakeVisible (box_);
    }
    void resized() override { box_.setBounds (getLocalBounds().reduced (1)); }
    void update (int row, const neptune::core::ZoneDescription& z)
    {
        row_ = row;
        updating_ = true;
        int id = 1;
        if (column_ == 7)
            id = z.onRelease ? 2 : z.trigger == neptune::core::TriggerCondition::First ? 3 : z.trigger == neptune::core::TriggerCondition::Legato ? 4 : 1;
        else
            id = (column_ == 8 ? z.chokeCuts : z.chokeCutBy) + 1;
        box_.setSelectedId (id, juce::dontSendNotification);
        updating_ = false;
    }

private:
    ZoneTable& owner_;
    int column_;
    int row_ = 0;
    bool updating_ = false;
    juce::ComboBox box_;
};

// Petit bouton d'écoute : appui = joue la zone à sa note racine, relâchement = Note Off (une boucle se relâche) ; Maj+clic = stop.
class ZoneTable::PlayCell : public juce::Component
{
public:
    explicit PlayCell (ZoneTable& owner) : owner_ (owner) {}
    void update (int row) { row_ = row; }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (8.0f, 5.0f);
        g.setColour (down_ ? owner_.colour (Token::KnobHot) : owner_.colour (Token::Accent));
        juce::Path p;
        p.addTriangle (r.getX() + 2.0f, r.getY(), r.getX() + 2.0f, r.getBottom(), r.getRight(), r.getCentreY());
        g.fillPath (p);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        down_ = true;
        shift_ = e.mods.isShiftDown();
        if (owner_.onPlay) owner_.onPlay (row_, true, shift_);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (down_ && ! shift_ && owner_.onPlay) owner_.onPlay (row_, false, false);
        down_ = false;
        repaint();
    }

private:
    ZoneTable& owner_;
    int row_ = 0;
    bool down_ = false, shift_ = false;
};

juce::Component* ZoneTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (row < 0 || row >= static_cast<int> (zones_.size()) || column < 7)
    {
        delete existing;
        return nullptr;
    }
    if (column == 10)
    {
        auto* cell = dynamic_cast<PlayCell*> (existing);
        if (cell == nullptr) { delete existing; cell = new PlayCell (*this); }
        cell->update (row);
        return cell;
    }
    auto* cell = dynamic_cast<ComboCell*> (existing);
    if (cell == nullptr) { delete existing; cell = new ComboCell (*this, column); }
    cell->update (row, zones_[static_cast<std::size_t> (row)]);
    return cell;
}

juce::Colour ZoneTable::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void ZoneTable::setZones (const std::vector<neptune::core::ZoneDescription>& zones)
{
    const auto selection = table_.getSelectedRows();
    zones_ = zones;
    table_.updateContent();
    if (selection.size() > 0)
    {
        juce::SparseSet<int> keep;
        for (int i = 0; i < selection.size(); ++i)
            if (selection[i] < static_cast<int> (zones_.size())) keep.addRange ({ selection[i], selection[i] + 1 });
        table_.setSelectedRows (keep, juce::dontSendNotification);
    }
    table_.repaint();
}

std::vector<int> ZoneTable::selectedRows() const
{
    std::vector<int> r;
    const auto sel = table_.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i) r.push_back (sel[i]);
    return r;
}

void ZoneTable::setSelectedRows (const std::vector<int>& rows, bool notify)
{
    juce::SparseSet<int> set;
    for (const int r : rows)
        if (r >= 0 && r < static_cast<int> (zones_.size())) set.addRange ({ r, r + 1 });
    table_.setSelectedRows (set, notify ? juce::sendNotificationAsync : juce::dontSendNotification);
    if (! rows.empty() && ! set.isEmpty()) table_.scrollToEnsureRowIsOnscreen (set[0]);
}

void ZoneTable::setPlayingRows (const std::vector<int>& rows)
{
    if (rows == playing_) return;
    playing_ = rows;
    table_.repaint();
}

void ZoneTable::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected) g.fillAll (colour (Token::Nav1));
    if (std::binary_search (playing_.begin(), playing_.end(), row)) g.fillAll (colour (Token::KnobHot).withAlpha (0.28f)); // sonne
}

void ZoneTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (zones_.size())) return;
    const auto& z = zones_[static_cast<std::size_t> (row)];
    juce::String text;
    switch (column)
    {
        case 1: text = juce::File (juce::String::fromUTF8 (z.samplePath.c_str())).getFileName(); break;
        case 2: text = juce::String (z.rootNote); break;
        case 3: text = z.keyLow == z.keyHigh ? juce::String (z.keyLow) : juce::String (z.keyLow) + "-" + juce::String (z.keyHigh); break;
        case 4: text = z.velLow == z.velHigh ? juce::String (z.velLow) : juce::String (z.velLow) + "-" + juce::String (z.velHigh); break;
        case 5: text = z.roundRobin > 0 ? juce::String (z.roundRobin) : juce::String(); break;
        case 6:
            text = z.mode == neptune::core::PlayMode::Loop ? "Loop" : "One-shot";
            if (z.onRelease) text << " (rel)";
            break;
        default: break;
    }
    g.setColour (colour (Token::Text));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
}
