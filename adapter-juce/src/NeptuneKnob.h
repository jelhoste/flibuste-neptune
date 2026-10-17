#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "ParamHost.h"
#include "neptune/core/KnobInteraction.h"
#include "neptune/core/Theme.h"
#include "neptune/core/UiState.h"

// Dessine l'image d'un potard NOVA (styles « led » et « pot ») dans `bounds`, pour la position normalisée `normalised`.
// Partagé par NeptuneKnob et par le look des curseurs rotatifs (réglages du synthé / des filtres qui ne sont pas des paramètres).
void paintNeptuneKnob (juce::Graphics&, juce::Rectangle<float> bounds, float normalised, const neptune::core::theme::Palette&,
                       neptune::core::theme::KnobStyle);

// Look des juce::Slider rotatifs : même image que NeptuneKnob.
class NeptuneSliderLook : public juce::LookAndFeel_V4
{
public:
    NeptuneSliderLook (const neptune::core::theme::Palette& palette, neptune::core::theme::KnobStyle style) : palette_ (palette), style_ (style) {}
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

private:
    neptune::core::theme::Palette palette_;
    neptune::core::theme::KnobStyle style_;
};

// Potard générique (palier 0). La logique des gestes est dans le cœur (KnobInteraction) ; ce composant ne fait que
// traduire les événements JUCE, dessiner (styles « led » et « pot » de la maquette) et brancher les actions.
class NeptuneKnob : public juce::Component, private juce::Timer
{
public:
    using OpenModal = std::function<void (neptune::core::ParamId, neptune::core::uistate::Terminal)>;

    NeptuneKnob (ParamHost&, neptune::core::ParamId, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, OpenModal);
    ~NeptuneKnob() override { stopTimer(); }

    neptune::core::ParamId paramId() const { return id_; }
    void setParamId (neptune::core::ParamId); // rebranche le potard sur un autre paramètre (piste sélectionnée)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    void showMacroMenu();
    static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
    juce::Colour colour (neptune::core::theme::Token t) const;
    neptune::core::uistate::Terminal terminal() const
    {
        return lastWasTouch_ ? neptune::core::uistate::Terminal::Touch : neptune::core::uistate::Terminal::Pointer;
    }

    ParamHost& processor_;
    neptune::core::ParamId id_;
    std::size_t index_;
    neptune::core::theme::Palette palette_;
    neptune::core::theme::KnobStyle style_;
    OpenModal openModal_;
    neptune::core::ui::KnobInteraction interaction_;
    std::uint32_t lastVersion_ = 0;
    bool lastWasTouch_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneKnob)
};
