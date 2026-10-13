#include "VoicesView.h"

using neptune::core::theme::Token;

namespace
{
constexpr int kSummaryH = 26;
}

VoicesView::VoicesView (const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr)
    : palette_ (palette), tr_ (tr)
{
    auto name = [this] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
    auto& h = table_.getHeader();
    h.addColumn (name ("voices.col_sample"), 1, 190, 80, 600, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_key"), 2, 70, 40, 120, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_velocity"), 3, 44, 30, 80, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_zone"), 4, 46, 30, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("trig.label"), 5, 112, 60, 180, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("import.col_group"), 6, 80, 40, 200, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("choke.label"), 7, 60, 40, 120, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_state"), 8, 100, 60, 200, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_layer"), 9, 56, 40, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_level"), 10, 56, 40, 100, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_position"), 11, 80, 40, 200, juce::TableHeaderComponent::notSortable);
    h.addColumn (name ("voices.col_source"), 12, 60, 40, 120, juce::TableHeaderComponent::notSortable);
    table_.setRowHeight (22);
    table_.setColour (juce::ListBox::backgroundColourId, colour (Token::BgCard));
    table_.setColour (juce::ListBox::outlineColourId, colour (Token::Border));
    table_.setOutlineThickness (1);
    addAndMakeVisible (table_);
}

juce::Colour VoicesView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void VoicesView::setRows (std::vector<Row> rows, const juce::String& summary)
{
    rows_ = std::move (rows);
    summary_ = summary;
    table_.updateContent();
    table_.repaint();
    repaint();
}

void VoicesView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (kSummaryH);
    table_.setBounds (r);
}

void VoicesView::paint (juce::Graphics& g)
{
    g.setColour (colour (Token::Accent));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (summary_, 4, 0, getWidth() - 8, kSummaryH, juce::Justification::centredLeft, true);
}

void VoicesView::paintRowBackground (juce::Graphics& g, int row, int, int, bool)
{
    if (row < 0 || row >= static_cast<int> (rows_.size())) return;
    const auto& r = rows_[static_cast<std::size_t> (row)];
    if (r.finished) return;
    if (r.fading) g.fillAll (colour (Token::KnobHot).withAlpha (0.16f));
    else if (r.releasing) g.fillAll (colour (Token::Accent2).withAlpha (0.16f));
    else if (r.preview) g.fillAll (colour (Token::Pink).withAlpha (0.14f));
    else g.fillAll (colour (Token::Nav1).withAlpha (0.35f));
}

void VoicesView::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (rows_.size())) return;
    const auto& r = rows_[static_cast<std::size_t> (row)];
    g.setColour (r.finished ? colour (Token::TextDim2) : colour (Token::Text));
    g.setFont (juce::FontOptions (13.0f));
    juce::String t;
    switch (column)
    {
        case 1: t = r.sample; break;
        case 2: t = r.key; break;
        case 3: t = r.velocity; break;
        case 4: t = r.zone; break;
        case 5: t = r.trigger; break;
        case 6: t = r.group; break;
        case 7: t = r.choke; break;
        case 8: t = r.state; break;
        case 9: t = r.layerGain; break;
        case 10: t = r.level; break;
        case 12: t = r.source; break;
        case 11:
        {
            const auto bar = juce::Rectangle<float> (4.0f, 6.0f, static_cast<float> (width) - 10.0f, static_cast<float> (height) - 12.0f);
            g.setColour (colour (Token::Border));
            g.drawRect (bar, 1.0f);
            g.setColour (r.finished ? colour (Token::TextDim2) : colour (Token::Accent));
            g.fillRect (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, r.progress)));
            return;
        }
        default: break;
    }
    g.drawText (t, 4, 0, width - 6, height, juce::Justification::centredLeft, true);
}
