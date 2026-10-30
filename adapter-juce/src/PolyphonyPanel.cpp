#include "PolyphonyPanel.h"

using namespace neptune::core;
using neptune::core::theme::Token;

PolyphonyPanel::PolyphonyPanel (const theme::Palette& palette, const i18n::Translator& tr, const Settings& s)
    : palette_ (palette), tr_ (tr), s_ (s)
{
    auto label = [this] (juce::Label& l, const char* key)
    {
        l.setText (text (key), juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colour (Token::Text));
        addAndMakeVisible (l);
    };
    label (modeLabel_, "poly.mode");
    label (limitLabel_, "poly.limit");
    label (stealLabel_, "poly.steal");
    label (sameKeyLabel_, "poly.same_key");
    label (fadeLabel_, "poly.cut_fade");
    label (glideLabel_, "poly.glide");
    label (bendLabel_, "poly.bend_range");

    mode_.addItem (text ("poly.mode_poly"), 1);
    mode_.addItem (text ("poly.mode_mono"), 2);
    mode_.addItem (text ("poly.mode_legato"), 3);
    steal_.addItem (text ("poly.steal_oldest"), 1);
    steal_.addItem (text ("poly.steal_quietest"), 2);
    steal_.addItem (text ("poly.steal_keep_low"), 3);
    steal_.addItem (text ("poly.steal_keep_high"), 4);
    sameKey_.addItem (text ("poly.same_stack"), 1);
    sameKey_.addItem (text ("poly.same_cut"), 2);
    for (auto* c : { &mode_, &steal_, &sameKey_ })
    {
        c->onChange = [this]
        {
            if (updating_) return;
            if (onGestureBegin) onGestureBegin();
            s_.mode = static_cast<PolyphonyMode> (mode_.getSelectedId() - 1);
            s_.steal = static_cast<VoiceStealMode> (steal_.getSelectedId() - 1);
            s_.sameKey = static_cast<SameKeyMode> (sameKey_.getSelectedId() - 1);
            changed();
            if (onGestureEnd) onGestureEnd();
        };
        addAndMakeVisible (*c);
    }

    limit_.setRange (1, InstrumentDescription::maxVoicesLimit, 1);
    {
        const int presets[3] = { 64, 128, 256 };
        for (int i = 0; i < 3; ++i)
        {
            limitPreset_[i].setButtonText (juce::String (presets[i]));
            limitPreset_[i].onClick = [this, v = presets[i]]
            {
                if (onGestureBegin) onGestureBegin();
                s_.maxVoices = v;
                changed();
                if (onGestureEnd) onGestureEnd();
            };
            addAndMakeVisible (limitPreset_[i]);
        }
    }
    fade_.setRange (1, InstrumentDescription::maxCutFadeMs, 1);
    fade_.setTextValueSuffix (" ms");
    glide_.setRange (0, InstrumentDescription::maxGlideMs, 10);
    glide_.setTextValueSuffix (" ms");
    bend_.setRange (0, InstrumentDescription::maxBendRange, 1);
    bend_.setTextValueSuffix (" st");
    for (auto* sl : { &limit_, &fade_, &glide_, &bend_ })
    {
        sl->onDragStart = [this] { dragging_ = true; if (onGestureBegin) onGestureBegin(); };
        sl->onDragEnd = [this] { dragging_ = false; if (onGestureEnd) onGestureEnd(); };
        sl->onValueChange = [this]
        {
            if (updating_) return;
            s_.maxVoices = static_cast<int> (limit_.getValue());
            s_.cutFadeMs = static_cast<int> (fade_.getValue());
            s_.glideMs = static_cast<int> (glide_.getValue());
            s_.bendRange = static_cast<int> (bend_.getValue());
            changed();
        };
        addAndMakeVisible (*sl);
    }

    chokeSame_.setButtonText (text ("poly.choke_same"));
    chokeSame_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    chokeSame_.onClick = [this]
    {
        if (updating_) return;
        if (onGestureBegin) onGestureBegin();
        s_.chokeSameZone = chokeSame_.getToggleState();
        changed();
        if (onGestureEnd) onGestureEnd();
    };
    addAndMakeVisible (chokeSame_);

    hint_.setText (text ("poly.hint"), juce::dontSendNotification);
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint_);

    setWantsKeyboardFocus (true);
    refreshControls();
    setSize (500, 372);
}

juce::Colour PolyphonyPanel::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String PolyphonyPanel::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void PolyphonyPanel::refreshControls()
{
    updating_ = true;
    mode_.setSelectedId (static_cast<int> (s_.mode) + 1, juce::dontSendNotification);
    steal_.setSelectedId (static_cast<int> (s_.steal) + 1, juce::dontSendNotification);
    sameKey_.setSelectedId (static_cast<int> (s_.sameKey) + 1, juce::dontSendNotification);
    limit_.setValue (s_.maxVoices, juce::dontSendNotification);
    fade_.setValue (s_.cutFadeMs, juce::dontSendNotification);
    glide_.setValue (s_.glideMs, juce::dontSendNotification);
    bend_.setValue (s_.bendRange, juce::dontSendNotification);
    chokeSame_.setToggleState (s_.chokeSameZone, juce::dontSendNotification);
    updating_ = false;
    const bool poly = s_.mode == PolyphonyMode::Poly;
    limit_.setEnabled (poly);
    for (auto& b : limitPreset_) b.setEnabled (poly);
    steal_.setEnabled (poly);
    sameKey_.setEnabled (poly);
    glide_.setEnabled (s_.mode == PolyphonyMode::Legato);
    limitLabel_.setEnabled (poly);
    stealLabel_.setEnabled (poly);
    sameKeyLabel_.setEnabled (poly);
    glideLabel_.setEnabled (s_.mode == PolyphonyMode::Legato);
}

void PolyphonyPanel::changed()
{
    refreshControls();
    if (onChange) onChange (s_);
}

void PolyphonyPanel::setValues (const Settings& s)
{
    if (dragging_ || s == s_) return;
    s_ = s;
    refreshControls();
}

void PolyphonyPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto row = [&r] (juce::Label& l, juce::Component& c)
    {
        auto line = r.removeFromTop (34);
        l.setBounds (line.removeFromLeft (170));
        c.setBounds (line.reduced (2));
        r.removeFromTop (4);
    };
    row (modeLabel_, mode_);
    {
        auto line = r.removeFromTop (34);
        limitLabel_.setBounds (line.removeFromLeft (170));
        for (int i = 2; i >= 0; --i) limitPreset_[i].setBounds (line.removeFromRight (46).reduced (2));
        limit_.setBounds (line.reduced (2));
        r.removeFromTop (4);
    }
    row (stealLabel_, steal_);
    row (sameKeyLabel_, sameKey_);
    row (fadeLabel_, fade_);
    row (glideLabel_, glide_);
    row (bendLabel_, bend_);
    chokeSame_.setBounds (r.removeFromTop (30));
    hint_.setBounds (r);   // ce qui reste (la ligne de plage du bend a pris sa place)
}

void PolyphonyPanel::paint (juce::Graphics& g) { g.fillAll (colour (Token::BgCard)); }

bool PolyphonyPanel::keyPressed (const juce::KeyPress& k)
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
