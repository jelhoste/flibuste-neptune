#include "ChokePanel.h"
#include "neptune/core/Instrument.h"

using namespace neptune::core;
using neptune::core::theme::Token;

ChokePanel::ChokePanel (const theme::Palette& palette, const i18n::Translator& tr, int cuts, int cutBy)
    : palette_ (palette), tr_ (tr)
{
    auto fill = [this] (juce::ComboBox& c)
    {
        c.addItem (text ("choke.none"), 1);
        for (int g = 1; g <= maxChokeGroup; ++g) c.addItem (juce::String (g), g + 1);
        c.onChange = [this]
        {
            if (updating_) return;
            if (onChange) onChange (cuts_.getSelectedId() - 1, cutBy_.getSelectedId() - 1);
        };
        addAndMakeVisible (c);
    };
    fill (cuts_);
    fill (cutBy_);
    cutsLabel_.setText (text ("choke.cuts"), juce::dontSendNotification);
    cutByLabel_.setText (text ("choke.cut_by"), juce::dontSendNotification);
    hint_.setText (text ("choke.hint"), juce::dontSendNotification);
    for (auto* l : { &cutsLabel_, &cutByLabel_ }) { l->setColour (juce::Label::textColourId, colour (Token::Text)); addAndMakeVisible (*l); }
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint_);
    setValues (cuts, cutBy);
    setWantsKeyboardFocus (true);
    setSize (420, 190);
}

juce::Colour ChokePanel::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String ChokePanel::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void ChokePanel::setValues (int cuts, int cutBy)
{
    updating_ = true;
    cuts_.setSelectedId (juce::jlimit (0, maxChokeGroup, cuts) + 1, juce::dontSendNotification);
    cutBy_.setSelectedId (juce::jlimit (0, maxChokeGroup, cutBy) + 1, juce::dontSendNotification);
    updating_ = false;
}

void ChokePanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row1 = r.removeFromTop (34);
    cutsLabel_.setBounds (row1.removeFromLeft (220));
    cuts_.setBounds (row1.reduced (2));
    r.removeFromTop (4);
    auto row2 = r.removeFromTop (34);
    cutByLabel_.setBounds (row2.removeFromLeft (220));
    cutBy_.setBounds (row2.reduced (2));
    hint_.setBounds (r);
}

void ChokePanel::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgCard)); }

bool ChokePanel::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! (m.isCommandDown() || m.isCtrlDown())) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z')
    {
        if (m.isShiftDown()) { if (onRedo) onRedo(); } else if (onUndo) onUndo();
        return true;
    }
    if (code == 'Y' || code == 'y')
    {
        if (onRedo) onRedo();
        return true;
    }
    return false;
}
