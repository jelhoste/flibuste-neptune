#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include "neptune/core/DragValue.h"
#include "neptune/core/Theme.h"

// Valeur numérique en « pill » : on la fait glisser horizontalement à la souris ou au doigt (toute la largeur = toute l'étendue, Maj = réglage fin,
// molette = un pas), et un double-clic / double-tap ouvre un champ texte pour la saisir au clavier. La pill se remplit comme un curseur (à partir de
// zéro quand l'étendue est de part et d'autre de zéro). Règle de l'interface : un champ numérique est toujours une pill glissable + saisie au double-clic.
// La logique (échelle, pas, texte) est dans core/DragValue.h. Un geste de glissé (ou une saisie) est encadré par onGestureBegin / onGestureEnd : le
// destinataire en fait une seule entrée d'annulation.
class DragValuePill : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragValuePill()
    {
        editor_.setFont (juce::FontOptions (16.0f));
        editor_.setInputRestrictions (12, "-+0123456789.,");
        editor_.setJustification (juce::Justification::centred);
        editor_.onReturnKey = [this] { finishEdit (true); };
        editor_.onEscapeKey = [this] { finishEdit (false); };
        editor_.onFocusLost = [this] { finishEdit (true); };
        addChildComponent (editor_);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    // La palette doit survivre à la pill (celle de la vue qui la contient).
    void setPalette (const neptune::core::theme::Palette& palette)
    {
        palette_ = &palette;
        using neptune::core::theme::Token;
        editor_.setColour (juce::TextEditor::backgroundColourId, colour (Token::BgApp));
        editor_.setColour (juce::TextEditor::textColourId, colour (Token::Text));
        editor_.setColour (juce::TextEditor::outlineColourId, colour (Token::Accent));
        editor_.setColour (juce::TextEditor::focusedOutlineColourId, colour (Token::Accent));
        editor_.setColour (juce::CaretComponent::caretColourId, colour (Token::Accent));
        repaint();
    }

    // `suffix` : l'unité affichée après le nombre (« Hz », « % »), sans l'espace.
    void setRange (const neptune::core::dragvalue::Range& range, juce::String suffix = {})
    {
        range_ = range;
        suffix_ = std::move (suffix);
        value_ = neptune::core::dragvalue::clamp (range_, value_);
        repaint();
    }

    void setValue (double value)   // sans rappel : la valeur vient d'ailleurs (rechargement, annulation)
    {
        const double v = neptune::core::dragvalue::clamp (range_, value);
        if (v == value_) return;
        value_ = v;
        repaint();
    }
    double value() const noexcept { return value_; }

    std::function<void (double)> onValue;      // à chaque changement de valeur (glissé, molette, saisie)
    std::function<void()> onGestureBegin, onGestureEnd;

    void paint (juce::Graphics& g) override
    {
        using neptune::core::theme::Token;
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = juce::jmin (10.0f, r.getHeight() * 0.5f);
        g.setColour (colour (Token::Pb));
        g.fillRoundedRectangle (r, radius);
        {
            // le remplissage : de zéro (ou du minimum) jusqu'à la valeur
            namespace dv = neptune::core::dragvalue;
            juce::Path shape;
            shape.addRoundedRectangle (r, radius);
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (shape);
            const double n = dv::toNormalised (range_, value_);
            const double zero = (range_.min < 0.0 && range_.max > 0.0) ? dv::toNormalised (range_, 0.0) : 0.0;
            const float a = r.getX() + r.getWidth() * static_cast<float> (juce::jmin (n, zero));
            const float b = r.getX() + r.getWidth() * static_cast<float> (juce::jmax (n, zero));
            g.setColour (colour (Token::Accent).withAlpha (isEnabled() ? 0.35f : 0.15f));
            g.fillRect (juce::Rectangle<float> (a, r.getY(), juce::jmax (b - a, 1.0f), r.getHeight()));
        }
        g.setColour (dragging_ ? colour (Token::Accent) : colour (Token::Border));
        g.drawRoundedRectangle (r, radius, dragging_ ? 2.0f : 1.0f);
        if (editing_) return;
        g.setColour (isEnabled() ? colour (Token::Text) : colour (Token::TextDim));
        g.setFont (juce::FontOptions (juce::jlimit (12.0f, 18.0f, r.getHeight() * 0.55f)));
        g.drawText (displayText(), getLocalBounds().reduced (4, 0), juce::Justification::centred, true);
    }

    void resized() override { editor_.setBounds (getLocalBounds().reduced (1)); }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (editing_) return;
        startNormalised_ = neptune::core::dragvalue::toNormalised (range_, value_);
        dragging_ = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (editing_ || ! isEnabled()) return;
        const int dx = e.getDistanceFromDragStartX();
        if (! dragging_)
        {
            if (std::abs (dx) < (e.source.isTouch() ? 8 : 3)) return;   // en deçà : ce n'est pas un glissé (un clic ou un double-clic)
            dragging_ = true;
            if (onGestureBegin) onGestureBegin();
            repaint();
        }
        apply (neptune::core::dragvalue::dragged (range_, startNormalised_, dx, static_cast<double> (getWidth()), e.mods.isShiftDown()));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging_) return;
        dragging_ = false;
        if (onGestureEnd) onGestureEnd();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { if (isEnabled()) beginEdit(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (editing_ || ! isEnabled() || wheel.deltaY == 0.0f) return;
        const int notches = wheel.deltaY > 0.0f ? 1 : -1;
        if (onGestureBegin) onGestureBegin();
        apply (neptune::core::dragvalue::wheel (range_, value_, notches, e.mods.isShiftDown()));
        if (onGestureEnd) onGestureEnd();
    }

private:
    juce::String displayText() const
    {
        const auto number = juce::String::fromUTF8 (neptune::core::dragvalue::format (range_, value_).c_str());
        return suffix_.isEmpty() ? number : number + " " + suffix_;
    }

    juce::Colour colour (neptune::core::theme::Token t) const
    {
        if (palette_ == nullptr) return juce::Colours::grey;
        const auto& c = palette_->color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }

    void apply (double v)
    {
        v = neptune::core::dragvalue::clamp (range_, v);
        if (v == value_) return;
        value_ = v;
        repaint();
        if (onValue) onValue (v);
    }

    void beginEdit()
    {
        editing_ = true;
        editor_.setText (juce::String::fromUTF8 (neptune::core::dragvalue::format (range_, value_).c_str()), false);
        editor_.setVisible (true);
        editor_.toFront (false);
        editor_.grabKeyboardFocus();
        editor_.selectAll();
        repaint();
    }

    void finishEdit (bool commit)
    {
        if (! editing_) return;
        editing_ = false;
        double typed = 0.0;
        const bool ok = commit && neptune::core::dragvalue::parse (range_, editor_.getText().toStdString(), typed);
        editor_.setVisible (false);
        if (ok && neptune::core::dragvalue::clamp (range_, typed) != value_)
        {
            if (onGestureBegin) onGestureBegin();
            apply (typed);
            if (onGestureEnd) onGestureEnd();
        }
        repaint();
    }

    const neptune::core::theme::Palette* palette_ = nullptr;
    neptune::core::dragvalue::Range range_;
    juce::String suffix_;
    double value_ = 0.0;
    double startNormalised_ = 0.0;
    bool dragging_ = false, editing_ = false;
    juce::TextEditor editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragValuePill)
};
