#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"
#include "neptune/core/VelocityResponse.h"

// Réponse de vélocité de l'instrument (fenêtre flottante) : largeur et type du fondu entre couches, et courbe qui remappe la
// vélocité reçue. La courbe se règle par un paramètre de forme + préréglages, et/ou par des POINTS (double-clic dans le
// graphe : ajoute ; glisser : déplace ; double-clic sur un point ou clic droit : supprime). Dès qu'il y a des points, la
// courbe passe par eux (interpolation monotone) ; choisir une forme ou un préréglage les efface.
class VelocityPanel : public juce::Component
{
public:
    VelocityPanel (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                   int fadeWidth, bool fadeLinear, const neptune::core::VelocityCurve&);

    std::function<void (int width, bool linear, const neptune::core::VelocityCurve&)> onChange;
    std::function<void()> onGestureBegin, onGestureEnd;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    juce::Rectangle<float> graph() const;
    juce::Point<float> toScreen (double x, double y) const;
    int pointAt (juce::Point<float>) const;
    void changed();

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    int fadeWidth_ = 0;
    bool fadeLinear_ = false;
    neptune::core::VelocityCurve curve_;

    juce::Label widthLabel_, shapeLabel_, readout_;
    juce::Slider widthSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider shapeSlider_ { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ToggleButton linearToggle_;
    juce::ComboBox presets_;
    bool updating_ = false;
    int dragPoint_ = -1;
    juce::Rectangle<float> graphArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelocityPanel)
};
