#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include "neptune/core/Theme.h"

// Molette verticale du clavier à l'écran : « hauteur » (pitch bend, revient au milieu quand on la lâche) ou « modulation » (reste où on la laisse).
// La valeur va de -1 à +1 (hauteur) ou de 0 à 1 (modulation) ; un double-clic la remet au repos. Souris et tactile.
class KeyboardWheel : public juce::Component
{
public:
    KeyboardWheel (const neptune::core::theme::Palette& palette, bool springBack, juce::String label)
        : palette_ (palette), spring_ (springBack), label_ (std::move (label)) {}

    std::function<void (float)> onChange;   // position : -1..+1 (retour au centre) ou 0..1

    float value() const noexcept { return value_; }
    void setValue (float v, bool notify)
    {
        const float lo = spring_ ? -1.0f : 0.0f;
        v = std::clamp (v, lo, 1.0f);
        if (v == value_) return;
        value_ = v;
        repaint();
        if (notify && onChange) onChange (value_);
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = colour (neptune::core::theme::Token::Accent);
        const auto text = colour (neptune::core::theme::Token::TextDim);
        auto area = getLocalBounds().toFloat().reduced (2.0f);
        auto labelArea = area.removeFromBottom (14.0f);
        g.setColour (text);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (label_, labelArea.toNearestInt(), juce::Justification::centred);
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (area, 5.0f);
        g.setColour (juce::Colour (0xff555555));
        g.drawRoundedRectangle (area, 5.0f, 1.0f);
        if (spring_)   // repère du centre
        {
            g.setColour (juce::Colour (0xff555555));
            g.drawLine (area.getX() + 3.0f, area.getCentreY(), area.getRight() - 3.0f, area.getCentreY(), 1.0f);
        }
        const float t = spring_ ? (value_ + 1.0f) * 0.5f : value_;   // 0 en bas, 1 en haut
        const float capH = 14.0f;
        const float y = area.getBottom() - capH - t * (area.getHeight() - capH);
        const auto cap = juce::Rectangle<float> (area.getX() + 2.0f, y, area.getWidth() - 4.0f, capH);
        g.setColour (isMouseButtonDown() ? accent : accent.withAlpha (0.75f));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        for (int i = 1; i < 4; ++i) g.drawLine (cap.getX() + 3.0f, cap.getY() + i * capH / 4.0f, cap.getRight() - 3.0f, cap.getY() + i * capH / 4.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override { if (spring_) setValue (0.0f, true); else repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { setValue (0.0f, true); }

private:
    void drag (const juce::MouseEvent& e)
    {
        auto area = getLocalBounds().toFloat().reduced (2.0f);
        area.removeFromBottom (14.0f);
        const float capH = 14.0f;
        const float t = 1.0f - std::clamp ((e.position.y - capH * 0.5f - area.getY()) / std::max (1.0f, area.getHeight() - capH), 0.0f, 1.0f);   // 1 en haut
        setValue (spring_ ? t * 2.0f - 1.0f : t, true);
    }
    juce::Colour colour (neptune::core::theme::Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }

    neptune::core::theme::Palette palette_;
    bool spring_;
    juce::String label_;
    float value_ = 0.0f;
};
