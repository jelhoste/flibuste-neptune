#include "NeptuneKnob.h"
#include <cmath>

using neptune::core::theme::Token;
namespace ui = neptune::core::ui;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
// La maquette (canvas) mesure les angles depuis l'est, dans le sens horaire ; JUCE depuis le nord.
constexpr float kA0 = 135.0f * kPi / 180.0f;
constexpr float kA1 = 405.0f * kPi / 180.0f;
float toJuce (float canvasAngle) { return canvasAngle + juce::MathConstants<float>::halfPi; }

void strokeArc (juce::Graphics& g, float cx, float cy, float radius, float from, float to, float width, bool rounded)
{
    if (std::abs (to - from) < 0.001f) return;
    juce::Path p;
    p.addCentredArc (cx, cy, radius, radius, 0.0f, toJuce (std::min (from, to)), toJuce (std::max (from, to)), true);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved,
                                           rounded ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
}
} // namespace

NeptuneKnob::NeptuneKnob (NeptuneProcessor& p, neptune::core::ParamId id, const neptune::core::theme::Palette& palette,
                          neptune::core::theme::KnobStyle style, OpenModal openModal)
    : processor_ (p), id_ (id), index_ (p.parameters().indexOf (id)), palette_ (palette), style_ (style), openModal_ (std::move (openModal))
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);

    const auto& set = processor_.parameters();
    jassert (index_ != neptune::core::ParameterSet::npos);
    if (index_ != neptune::core::ParameterSet::npos)
    {
        using Kind = neptune::core::Kind;
        const auto& spec = set.spec (index_);
        if (spec.kind != Kind::Continuous) interaction_.setSteps (static_cast<int> (spec.max - spec.min));
        interaction_.setValue (set.getNormalizedAt (index_));
        lastVersion_ = set.versionAt (index_);
    }

    ui::KnobCallbacks cb;
    cb.beginGesture = [this] { processor_.uiBeginEdit (id_); };
    cb.setNormalised = [this] (double n) { processor_.uiSetNormalised (id_, n); repaint(); };
    cb.endGesture = [this] { processor_.uiEndEdit (id_); };
    cb.resetToDefault = [this]
    {
        processor_.uiResetToDefault (id_);
        interaction_.setValue (processor_.parameters().getNormalized (id_));
        repaint();
    };
    cb.openModal = [this] { if (openModal_) openModal_ (id_, terminal()); };
    interaction_.setCallbacks (std::move (cb));

    startTimerHz (30);
}

juce::Colour NeptuneKnob::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void NeptuneKnob::timerCallback()
{
    interaction_.tick (nowMs());
    if (index_ == neptune::core::ParameterSet::npos) return;
    // Valeur changée ailleurs (hôte, annulation, modale) : on suit, sauf pendant notre propre geste.
    const auto& set = processor_.parameters();
    const auto v = set.versionAt (index_);
    if (v != lastVersion_)
    {
        lastVersion_ = v;
        if (! interaction_.gestureOpen()) interaction_.setValue (set.getNormalizedAt (index_));
        repaint();
    }
}

void NeptuneKnob::mouseDown (const juce::MouseEvent& e)
{
    lastWasTouch_ = e.source.isTouch();
    const auto button = e.mods.isPopupMenu() ? ui::MouseButton::Right : ui::MouseButton::Left;
    interaction_.pointerDown (button, lastWasTouch_ ? ui::PointerKind::Touch : ui::PointerKind::Mouse, e.mods.isShiftDown(),
                              e.position.x, e.position.y, nowMs());
}

void NeptuneKnob::mouseDrag (const juce::MouseEvent& e)
{
    interaction_.pointerMove (e.mods.isShiftDown(), e.position.x, e.position.y, nowMs());
}

void NeptuneKnob::mouseUp (const juce::MouseEvent&)
{
    interaction_.pointerUp (nowMs());
    if (index_ != neptune::core::ParameterSet::npos) interaction_.setValue (processor_.parameters().getNormalizedAt (index_));
    repaint();
}

void NeptuneKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
    if (w.isReversed) d = -d;
    if (d == 0.0f) return;
    interaction_.wheel (d > 0.0f ? 1.0 : -1.0, e.mods.isShiftDown(), nowMs());
}

bool NeptuneKnob::keyPressed (const juce::KeyPress& k)
{
    // Ctrl/Cmd + lettre : raccourcis de l'éditeur (annuler/rétablir), pas pour le potard.
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;

    const bool shift = k.getModifiers().isShiftDown();
    ui::Key key;
    if (k.isKeyCode (juce::KeyPress::upKey)) key = ui::Key::Up;
    else if (k.isKeyCode (juce::KeyPress::downKey)) key = ui::Key::Down;
    else if (k.isKeyCode (juce::KeyPress::leftKey)) key = ui::Key::Left;
    else if (k.isKeyCode (juce::KeyPress::rightKey)) key = ui::Key::Right;
    else if (k.isKeyCode (juce::KeyPress::pageUpKey)) key = ui::Key::PageUp;
    else if (k.isKeyCode (juce::KeyPress::pageDownKey)) key = ui::Key::PageDown;
    else if (k.isKeyCode (juce::KeyPress::homeKey)) key = ui::Key::Home;
    else if (k.isKeyCode (juce::KeyPress::endKey)) key = ui::Key::End;
    else if (k.isKeyCode (juce::KeyPress::returnKey)) key = ui::Key::Enter;
    else if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey)) key = ui::Key::Delete;
    else return false;

    lastWasTouch_ = false;
    return interaction_.key (key, shift);
}

void NeptuneKnob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float S = std::min (b.getWidth(), b.getHeight());
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float radius = S * 0.4f;
    const float n = juce::jlimit (0.0f, 1.0f, index_ == neptune::core::ParameterSet::npos
                                                  ? 0.0f
                                                  : static_cast<float> (processor_.parameters().getNormalizedAt (index_)));
    const float angle = kA0 + n * (kA1 - kA0);

    if (style_ == neptune::core::theme::KnobStyle::Pot)
    {
        // Potentiomètre physique avec pointeur (nova-knob-styles.js : drawPot)
        juce::ColourGradient grad (colour (Token::KnobPotFace1), cx - 3.0f, cy - 4.0f, colour (Token::KnobPotFace3),
                                   cx - 3.0f + radius + 5.0f, cy - 4.0f, true);
        grad.addColour (0.55, colour (Token::KnobPotFace2));
        g.setGradientFill (grad);
        g.fillEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (colour (Token::KnobPotRing));
        g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        g.setColour (colour (Token::Accent));
        strokeArc (g, cx, cy, radius - 5.0f, kA0, angle, 2.4f, true);

        juce::Path tri;
        tri.addTriangle (radius - 7.0f, 0.0f, radius - 14.0f, -2.2f, radius - 14.0f, 2.2f);
        g.setColour (colour (Token::KnobPotPointer));
        g.fillPath (tri, juce::AffineTransform::rotation (angle).translated (cx, cy));

        g.setColour (colour (Token::KnobPotCenter));
        g.fillEllipse (cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
    }
    else
    {
        // Anneau LED avec dégradé d'intensité (nova-knob-styles.js : drawLED)
        g.setColour (colour (Token::KnobFace));
        g.fillEllipse (cx - radius * 0.72f, cy - radius * 0.72f, radius * 1.44f, radius * 1.44f);

        g.setColour (colour (Token::KnobTrack).withAlpha (0.8f));
        strokeArc (g, cx, cy, radius, kA0, kA1, 5.0f, false);

        const auto c1 = colour (Token::Accent).interpolatedWith (colour (Token::KnobHot), n);
        const auto c2 = colour (Token::Accent2).interpolatedWith (colour (Token::KnobHot2), n);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (c1.withAlpha (0.07f * static_cast<float> (i)));
            strokeArc (g, cx, cy, radius, kA0, angle, 5.0f + static_cast<float> (i) * 3.0f, true);
        }
        g.setGradientFill (juce::ColourGradient (c1, cx - radius, cy, c2, cx + radius, cy, false));
        strokeArc (g, cx, cy, radius, kA0, angle, 5.0f, true);

        const float ix = cx + std::cos (angle) * (radius - 1.0f), iy = cy + std::sin (angle) * (radius - 1.0f);
        g.setColour (colour (Token::KnobDot));
        g.fillEllipse (ix - 2.6f, iy - 2.6f, 5.2f, 5.2f);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (colour (Token::Accent));
        g.drawEllipse (b.reduced (1.0f), 1.5f);
    }
}
