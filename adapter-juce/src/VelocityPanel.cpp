#include "VelocityPanel.h"
#include <algorithm>
#include <cmath>
#include "neptune/core/Instrument.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr float kPointRadius = 7.0f;
constexpr float kHitRadius = 16.0f; // >= 32 px de diamètre (tactile)
const char* const kPresetKeys[numVelocityPresets] = { "vel.preset_linear", "vel.preset_soft", "vel.preset_very_soft", "vel.preset_hard", "vel.preset_very_hard" };
}

VelocityPanel::VelocityPanel (const theme::Palette& palette, const i18n::Translator& tr, int fadeWidth, bool fadeLinear,
                              const VelocityCurve& curve)
    : palette_ (palette), tr_ (tr), fadeWidth_ (fadeWidth), fadeLinear_ (fadeLinear), curve_ (sanitised (curve))
{
    widthLabel_.setText (text ("vel.fade_width"), juce::dontSendNotification);
    widthLabel_.setColour (juce::Label::textColourId, colour (Token::Text));
    addAndMakeVisible (widthLabel_);
    widthSlider_.setRange (0, InstrumentDescription::maxVelocityFadeWidth, 1);
    widthSlider_.setValue (fadeWidth_, juce::dontSendNotification);
    widthSlider_.onDragStart = [this] { sliderDragging_ = true; if (onGestureBegin) onGestureBegin(); };
    widthSlider_.onDragEnd = [this] { sliderDragging_ = false; if (onGestureEnd) onGestureEnd(); };
    widthSlider_.onValueChange = [this] { if (updating_) return; fadeWidth_ = static_cast<int> (widthSlider_.getValue()); changed(); };
    addAndMakeVisible (widthSlider_);

    linearToggle_.setButtonText (text ("vel.fade_linear"));
    linearToggle_.setToggleState (fadeLinear_, juce::dontSendNotification);
    linearToggle_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    linearToggle_.onClick = [this] { fadeLinear_ = linearToggle_.getToggleState(); changed(); };
    addAndMakeVisible (linearToggle_);

    shapeLabel_.setText (text ("vel.shape"), juce::dontSendNotification);
    shapeLabel_.setColour (juce::Label::textColourId, colour (Token::Text));
    addAndMakeVisible (shapeLabel_);
    shapeSlider_.setRange (-1.0, 1.0, 0.01);
    shapeSlider_.setValue (curve_.shape, juce::dontSendNotification);
    shapeSlider_.setDoubleClickReturnValue (true, 0.0);
    shapeSlider_.onDragStart = [this] { sliderDragging_ = true; if (onGestureBegin) onGestureBegin(); };
    shapeSlider_.onDragEnd = [this] { sliderDragging_ = false; if (onGestureEnd) onGestureEnd(); };
    shapeSlider_.onValueChange = [this]
    {
        if (updating_) return;
        curve_.shape = shapeSlider_.getValue();
        curve_.points.clear(); // la forme remplace les points
        changed();
    };
    addAndMakeVisible (shapeSlider_);

    presets_.setTextWhenNothingSelected (text ("vel.presets"));
    for (int i = 0; i < numVelocityPresets; ++i) presets_.addItem (text (kPresetKeys[i]), i + 1);
    presets_.onChange = [this]
    {
        if (updating_) return;
        const int id = presets_.getSelectedId();
        if (id < 1) return;
        if (onGestureBegin) onGestureBegin();
        curve_ = presetCurve (static_cast<VelocityPreset> (id - 1));
        changed();
        if (onGestureEnd) onGestureEnd();
    };
    addAndMakeVisible (presets_);

    readout_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    readout_.setText (text ("vel.hint"), juce::dontSendNotification);
    readout_.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (readout_);
    setWantsKeyboardFocus (true);
    setSize (400, 500);
}

juce::Colour VelocityPanel::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String VelocityPanel::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void VelocityPanel::changed()
{
    curve_ = sanitised (curve_);
    updating_ = true;
    shapeSlider_.setValue (curve_.shape, juce::dontSendNotification);
    shapeSlider_.setEnabled (curve_.points.empty());
    int match = 0;
    for (int i = 0; i < numVelocityPresets; ++i)
        if (curve_ == presetCurve (static_cast<VelocityPreset> (i))) match = i + 1;
    presets_.setSelectedId (match, juce::dontSendNotification);
    updating_ = false;
    repaint();
    if (onChange) onChange (fadeWidth_, fadeLinear_, curve_);
}

void VelocityPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    widthLabel_.setBounds (r.removeFromTop (22));
    widthSlider_.setBounds (r.removeFromTop (30));
    linearToggle_.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto row = r.removeFromTop (30);
    shapeLabel_.setBounds (row.removeFromLeft (110));
    presets_.setBounds (row.removeFromRight (140).reduced (2));
    shapeSlider_.setBounds (row);
    readout_.setBounds (r.removeFromBottom (36));
    r.removeFromBottom (4);
    graphArea_ = r.reduced (24, 6).toFloat();
}

juce::Rectangle<float> VelocityPanel::graph() const
{
    const float side = std::min (graphArea_.getWidth(), graphArea_.getHeight());
    return juce::Rectangle<float> (graphArea_.getX() + (graphArea_.getWidth() - side) / 2.0f, graphArea_.getY(), side, side);
}

juce::Point<float> VelocityPanel::toScreen (double x, double y) const
{
    const auto g = graph();
    return { g.getX() + static_cast<float> (x) * g.getWidth(), g.getBottom() - static_cast<float> (y) * g.getHeight() };
}

void VelocityPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgCard));
    const auto gr = graph();
    g.setColour (colour (Token::BgApp));
    g.fillRect (gr);
    g.setColour (colour (Token::Grid));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine (static_cast<int> (gr.getX() + gr.getWidth() * i / 4.0f), gr.getY(), gr.getBottom());
        g.drawHorizontalLine (static_cast<int> (gr.getY() + gr.getHeight() * i / 4.0f), gr.getX(), gr.getRight());
    }
    g.setColour (colour (Token::Border));
    g.drawRect (gr, 1.0f);
    // diagonale = réponse linéaire
    g.setColour (colour (Token::TextDim).withAlpha (0.5f));
    g.drawLine (gr.getX(), gr.getBottom(), gr.getRight(), gr.getY(), 1.0f);

    juce::Path p;
    for (int i = 0; i <= 127; ++i)
    {
        const double x = i / 127.0;
        const auto s = toScreen (x, evaluate (curve_, x));
        if (i == 0) p.startNewSubPath (s); else p.lineTo (s);
    }
    g.setColour (colour (Token::Accent));
    g.strokePath (p, juce::PathStrokeType (2.0f));

    for (const auto& pt : curve_.points)
    {
        const auto s = toScreen (pt.first, pt.second);
        g.setColour (colour (Token::KnobHot));
        g.fillEllipse (s.x - kPointRadius, s.y - kPointRadius, kPointRadius * 2, kPointRadius * 2);
        g.setColour (colour (Token::Text));
        g.drawEllipse (s.x - kPointRadius, s.y - kPointRadius, kPointRadius * 2, kPointRadius * 2, 1.5f);
    }
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (text ("vel.axis_in"), gr.toNearestInt().translated (0, 4).withTop (static_cast<int> (gr.getBottom())).withHeight (14), juce::Justification::centred);
}

int VelocityPanel::pointAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = kHitRadius;
    for (std::size_t i = 0; i < curve_.points.size(); ++i)
    {
        const float d = toScreen (curve_.points[i].first, curve_.points[i].second).getDistanceFrom (p);
        if (d <= bestD) { best = static_cast<int> (i); bestD = d; }
    }
    return best;
}

void VelocityPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragPoint_ = pointAt (e.position);
    if (dragPoint_ < 0) return;
    if (e.mods.isPopupMenu()) // clic droit : supprime le point
    {
        if (onGestureBegin) onGestureBegin();
        curve_.points.erase (curve_.points.begin() + dragPoint_);
        dragPoint_ = -1;
        changed();
        if (onGestureEnd) onGestureEnd();
        return;
    }
    if (onGestureBegin) onGestureBegin();
}

void VelocityPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPoint_ < 0 || dragPoint_ >= static_cast<int> (curve_.points.size())) return;
    const auto g = graph();
    const double x = (e.position.x - g.getX()) / g.getWidth();
    const double y = (g.getBottom() - e.position.y) / g.getHeight();
    curve_.points[static_cast<std::size_t> (dragPoint_)] = { std::clamp (x, 0.0, 1.0), std::clamp (y, 0.0, 1.0) };
    // garde l'ordre pendant le glissé : pas de croisement avec les voisins (la normalisation les espacerait de toute façon)
    auto& pts = curve_.points;
    const std::size_t i = static_cast<std::size_t> (dragPoint_);
    const double lo = i > 0 ? pts[i - 1].first + VelocityCurve::minPointGap : VelocityCurve::minPointGap;
    const double hi = i + 1 < pts.size() ? pts[i + 1].first - VelocityCurve::minPointGap : 1.0 - VelocityCurve::minPointGap;
    pts[i].first = std::clamp (pts[i].first, lo, std::max (lo, hi));
    changed();
}

void VelocityPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragPoint_ >= 0 && onGestureEnd) onGestureEnd();
    dragPoint_ = -1;
}

void VelocityPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto g = graph();
    if (! g.contains (e.position)) return;
    if (onGestureBegin) onGestureBegin();
    const int hit = pointAt (e.position);
    if (hit >= 0)
        curve_.points.erase (curve_.points.begin() + hit);
    else if (curve_.points.size() < VelocityCurve::maxPoints)
    {
        const double x = (e.position.x - g.getX()) / g.getWidth();
        const double y = evaluate (curve_, x); // le nouveau point part de la courbe actuelle : rien ne bouge d'un coup
        curve_.points.push_back ({ x, y });
    }
    changed();
    if (onGestureEnd) onGestureEnd();
}

bool VelocityPanel::keyPressed (const juce::KeyPress& k)
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

void VelocityPanel::setValues (int fadeWidth, bool fadeLinear, const VelocityCurve& curve)
{
    if (sliderDragging_ || dragPoint_ >= 0) return;
    const auto c = sanitised (curve);
    if (fadeWidth == fadeWidth_ && fadeLinear == fadeLinear_ && c == curve_) return;
    fadeWidth_ = fadeWidth;
    fadeLinear_ = fadeLinear;
    curve_ = c;
    updating_ = true;
    widthSlider_.setValue (fadeWidth_, juce::dontSendNotification);
    linearToggle_.setToggleState (fadeLinear_, juce::dontSendNotification);
    shapeSlider_.setValue (curve_.shape, juce::dontSendNotification);
    shapeSlider_.setEnabled (curve_.points.empty());
    int match = 0;
    for (int i = 0; i < numVelocityPresets; ++i)
        if (curve_ == presetCurve (static_cast<VelocityPreset> (i))) match = i + 1;
    presets_.setSelectedId (match, juce::dontSendNotification);
    updating_ = false;
    repaint();
}
