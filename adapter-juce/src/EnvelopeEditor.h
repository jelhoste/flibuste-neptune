#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/Envelope.h"
#include "neptune/core/EnvelopeLayout.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Éditeur graphique de l'enveloppe (palier 1) : une seule courbe continue ; la largeur de chaque segment reflète sa durée
// (neptune::core::computeEnvelopeLayout), un segment à 0 ms n'a pas de largeur. Les durées sont écrites sous la courbe.
//
//  - nœud de fin de segment : glisser horizontalement = durée (le nœud du decay aussi verticalement = niveau de sustain) ;
//  - poignée au milieu d'une courbe : glisser verticalement = forme ; double-clic = linéaire ; appui long / clic droit = dB ;
//  - étiquette de durée : glisser horizontalement = faire varier la valeur (marche aussi pour 0 ms), toucher = saisir en
//    chiffres ;
//  - pendant un glissé l'échelle est figée : ce qui précède ne bouge pas ; au relâchement tout est recalculé.
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
    enum class Seg { Delay = 0, Attack = 1, Hold = 2, Decay = 3, Release = 4, None = 5 };
    enum class Part { Node, Shape, Chip, None };
    struct Hit { Seg seg = Seg::None; Part part = Part::None; };
    struct Chip { Seg seg; juce::Rectangle<float> rect; juce::String text; };
    struct Frame { juce::Rectangle<float> graph; float yTop, yBottom; neptune::core::EnvelopeLayout layout; std::vector<Chip> chips; };

    Frame frame() const;
    neptune::core::EnvelopeSegment& segment (neptune::core::EnvelopeDescription&, Seg) const;
    Hit hitTest (juce::Point<float>) const;
    juce::Colour colour (neptune::core::theme::Token t) const;
    void changed();
    void timerCallback() override; // appui long
    void syncControls();
    void askValue (Seg);

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::EnvelopeDescription env_;
    juce::TextButton toggles_[5];
    juce::ComboBox presets_;
    bool updating_ = false;

    Hit drag_;
    double frozenScale_ = 0.0;
    juce::Point<float> downPos_;
    double downMs_ = 0.0;
    juce::uint32 downTime_ = 0;
    bool longPressFired_ = false, moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};
