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
// Le préréglage se reconnaît à la forme seule : plancher, plafond et lissage sont des réglages à part.
VelocityCurve shapeOnly (VelocityCurve c)
{
    c.enabled = true;
    c.floor = 0.0;
    c.ceiling = 1.0;
    c.smooth = true;
    return c;
}
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

    enabledToggle_.setButtonText (text ("vel.enabled"));
    enabledToggle_.setToggleState (curve_.enabled, juce::dontSendNotification);
    enabledToggle_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    enabledToggle_.setVisible (false);
    enabledToggle_.onClick = [this]
    {
        if (updating_) return;
        if (onGestureBegin) onGestureBegin();
        curve_.enabled = enabledToggle_.getToggleState();
        changed();
        if (onGestureEnd) onGestureEnd();
    };
    addChildComponent (enabledToggle_);

    smoothToggle_.setButtonText (text ("vel.smooth"));
    smoothToggle_.setToggleState (curve_.smooth, juce::dontSendNotification);
    smoothToggle_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    smoothToggle_.onClick = [this]
    {
        if (updating_) return;
        if (onGestureBegin) onGestureBegin();
        curve_.smooth = smoothToggle_.getToggleState();
        changed();
        if (onGestureEnd) onGestureEnd();
    };
    addAndMakeVisible (smoothToggle_);

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
        const auto keep = curve_;
        curve_ = presetCurve (static_cast<VelocityPreset> (id - 1));
        curve_.floor = keep.floor; curve_.ceiling = keep.ceiling; curve_.smooth = keep.smooth; curve_.enabled = keep.enabled;
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

void VelocityPanel::setEnabledToggleVisible (bool visible)
{
    enabledShown_ = visible;
    enabledToggle_.setVisible (visible);
    resized();
}

void VelocityPanel::setFadeControlsVisible (bool visible)
{
    fadeControls_ = visible;
    widthLabel_.setVisible (visible);
    widthSlider_.setVisible (visible);
    linearToggle_.setVisible (visible);
    resized();
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
    smoothToggle_.setToggleState (curve_.smooth, juce::dontSendNotification);
    smoothToggle_.setEnabled (! curve_.points.empty());
    enabledToggle_.setToggleState (curve_.enabled, juce::dontSendNotification);
    int match = 0;
    for (int i = 0; i < numVelocityPresets; ++i)
        if (shapeOnly (curve_) == presetCurve (static_cast<VelocityPreset> (i))) match = i + 1;
    presets_.setSelectedId (match, juce::dontSendNotification);
    updating_ = false;
    repaint();
    if (onChange) onChange (fadeWidth_, fadeLinear_, curve_);
}

void VelocityPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    if (enabledShown_) enabledToggle_.setBounds (r.removeFromTop (28));
    if (fadeControls_)
    {
        widthLabel_.setBounds (r.removeFromTop (22));
        widthSlider_.setBounds (r.removeFromTop (30));
        linearToggle_.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
    }
    auto row = r.removeFromTop (30);
    shapeLabel_.setBounds (row.removeFromLeft (110));
    presets_.setBounds (row.removeFromRight (140).reduced (2));
    shapeSlider_.setBounds (row);
    smoothToggle_.setBounds (r.removeFromTop (26));
    readout_.setBounds (r.removeFromBottom (52));
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

    // plancher et plafond : repères en pointillés et poignées carrées sur les bords gauche et droit
    {
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (colour (Token::TextDim2));
        for (const double y : { curve_.floor, curve_.ceiling })
        {
            if (y <= 0.0 || y >= 1.0) continue;
            const auto a = toScreen (0.0, y), b = toScreen (1.0, y);
            g.drawDashedLine (juce::Line<float> (a, b), dashes, 2, 1.0f);
        }
        g.setColour (colour (Token::Handle));
        for (const auto& hnd : { toScreen (0.0, curve_.floor), toScreen (1.0, curve_.ceiling) })
        {
            const juce::Rectangle<float> box (hnd.x - 7.0f, hnd.y - 7.0f, 14.0f, 14.0f);
            g.fillRect (box);
            g.setColour (colour (Token::Text));
            g.drawRect (box, 1.5f);
            g.setColour (colour (Token::Handle));
        }
    }
    for (const auto& pt : curve_.points)
    {
        const auto s = toScreen (pt.first, toOutput (pt.second));
        g.setColour (colour (Token::KnobHot));
        g.fillEllipse (s.x - kPointRadius, s.y - kPointRadius, kPointRadius * 2, kPointRadius * 2);
        g.setColour (colour (Token::Text));
        g.drawEllipse (s.x - kPointRadius, s.y - kPointRadius, kPointRadius * 2, kPointRadius * 2, 1.5f);
    }
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (text ("vel.axis_in"), gr.toNearestInt().translated (0, 4).withTop (static_cast<int> (gr.getBottom())).withHeight (14), juce::Justification::centred);
}

int VelocityPanel::handleAt (juce::Point<float> p) const
{
    const float d1 = toScreen (0.0, curve_.floor).getDistanceFrom (p), d2 = toScreen (1.0, curve_.ceiling).getDistanceFrom (p);
    if (d1 <= kHitRadius && d1 <= d2) return 1;
    return d2 <= kHitRadius ? 2 : 0;
}

int VelocityPanel::pointAt (juce::Point<float> p) const
{
    int best = -1;
    float bestD = kHitRadius;
    for (std::size_t i = 0; i < curve_.points.size(); ++i)
    {
        const float d = toScreen (curve_.points[i].first, toOutput (curve_.points[i].second)).getDistanceFrom (p);
        if (d <= bestD) { best = static_cast<int> (i); bestD = d; }
    }
    return best;
}

void VelocityPanel::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragPoint_ = pointAt (e.position);
    if (dragPoint_ < 0)
    {
        dragHandle_ = handleAt (e.position);
        if (dragHandle_ != 0 && onGestureBegin) onGestureBegin();
        return;
    }
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
    if (dragHandle_ != 0)
    {
        const auto g = graph();
        const double y = (g.getBottom() - e.position.y) / g.getHeight();
        if (dragHandle_ == 1) curve_.floor = std::clamp (y, 0.0, curve_.ceiling - VelocityCurve::minSpan);
        else curve_.ceiling = std::clamp (y, curve_.floor + VelocityCurve::minSpan, 1.0);
        changed();
        return;
    }
    if (dragPoint_ < 0 || dragPoint_ >= static_cast<int> (curve_.points.size())) return;
    const auto g = graph();
    const double x = (e.position.x - g.getX()) / g.getWidth();
    const double y = (g.getBottom() - e.position.y) / g.getHeight();
    curve_.points[static_cast<std::size_t> (dragPoint_)] = { std::clamp (x, 0.0, 1.0), fromOutput (y) };
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
    if ((dragPoint_ >= 0 || dragHandle_ != 0) && onGestureEnd) onGestureEnd();
    dragPoint_ = -1;
    dragHandle_ = 0;
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
        const double y = fromOutput (evaluate (curve_, x)); // le nouveau point part de la courbe actuelle : rien ne bouge d'un coup
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
    if (sliderDragging_ || dragPoint_ >= 0 || dragHandle_ != 0) return;
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
    smoothToggle_.setToggleState (curve_.smooth, juce::dontSendNotification);
    smoothToggle_.setEnabled (! curve_.points.empty());
    enabledToggle_.setToggleState (curve_.enabled, juce::dontSendNotification);
    int match = 0;
    for (int i = 0; i < numVelocityPresets; ++i)
        if (shapeOnly (curve_) == presetCurve (static_cast<VelocityPreset> (i))) match = i + 1;
    presets_.setSelectedId (match, juce::dontSendNotification);
    updating_ = false;
    repaint();
}
