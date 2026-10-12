#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/Envelope.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Éditeur graphique de l'enveloppe (palier 1) : la courbe est dessinée avec la formule du temps réel ; on déplace les temps
// (glisser un nœud horizontalement), le niveau de sustain (nœud de fin du decay, verticalement) et les formes (poignée au
// milieu de chaque courbe, verticalement). Poignée : double-clic = forme linéaire ; appui long ou clic droit = bascule dB.
// Une ligne de boutons active/désactive chaque segment ; un menu propose les réglages prédéfinis (percussion...).
class EnvelopeEditor : public juce::Component, private juce::Timer
{
public:
    EnvelopeEditor (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    ~EnvelopeEditor() override { stopTimer(); }

    std::function<void (const neptune::core::EnvelopeDescription&)> onChange; // à chaque modification par l'utilisateur
    void setEnvelope (const neptune::core::EnvelopeDescription&);            // sans notification

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Seg { Delay, Attack, Hold, Decay, Release, None };
    enum class Part { Node, Shape, None };
    struct Hit { Seg seg = Seg::None; Part part = Part::None; };

    struct Geometry; // positions de chaque segment dans la zone de courbe
    Geometry layout() const;
    neptune::core::EnvelopeSegment& segment (neptune::core::EnvelopeDescription&, Seg) const;
    Hit hitTest (juce::Point<float>) const;
    juce::Colour colour (neptune::core::theme::Token t) const;
    void changed();
    void timerCallback() override; // appui long
    void syncControls();

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::EnvelopeDescription env_;
    juce::TextButton toggles_[5];
    juce::ComboBox presets_;
    bool updating_ = false;

    Hit drag_;
    juce::Point<float> downPos_;
    double downMs_ = 0.0;
    juce::uint32 downTime_ = 0;
    bool longPressFired_ = false, moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};
