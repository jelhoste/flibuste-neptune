#include "ZoneTable.h"

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
    table_.setMultipleSelectionEnabled (true);
    table_.setRowHeight (24);
    table_.setColour (juce::ListBox::backgroundColourId, colour (Token::BgCard));
    table_.setColour (juce::ListBox::outlineColourId, colour (Token::Border));
    table_.setOutlineThickness (1);
    addAndMakeVisible (table_);
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

void ZoneTable::paintRowBackground (juce::Graphics& g, int, int, int, bool selected)
{
    if (selected) g.fillAll (colour (Token::Nav1));
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
