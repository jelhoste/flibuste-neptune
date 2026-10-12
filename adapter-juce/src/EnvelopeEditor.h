#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/Envelope.h"
#include "neptune/core/EnvelopeLayout.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Éditeur graphique de l'enveloppe (palier 1) : une seule courbe continue ; la largeur de chaque segment reflète sa durée
// (neptune::core::computeEnvelopeLayout), un segment à 0 ms n'a pas de largeur.
//
// Rangée du haut : un bouton d'activation par segment (Dly, Atk, Hold, Dec, Rel), et SOUS chacun la valeur de sa durée,
// toujours alignée (jamais superposée). La valeur se glisse horizontalement (marche aussi depuis 0 ms) ou se touche
// pour saisir des chiffres ; le segment correspondant s'illumine sur la courbe.
//
// Sur la courbe, on peut aussi tout régler directement :
//  - nœud de fin de segment : glisser horizontalement = durée (le nœud du decay aussi verticalement = niveau de sustain) ;
//  - chaque segment activé a TOUJOURS sa poignée, même à 0 ms : délai collé à la marge gauche (ligne de base), attaque au
//    sommet, decay sur la chute, relâchement au bout du plateau ; le hold à 0 ms a une poignée creuse sur le prolongement
//    pointillé du niveau haut ;
//  - poignée du plateau : glisser verticalement = niveau de sustain (active le decay au besoin) ;
//  - poignée au milieu d'une courbe : glisser verticalement = forme ; double-clic = linéaire ; appui long / clic droit = dB ;
//  - pendant un glissé l'échelle est figée : ce qui précède ne bouge pas ; au relâchement tout est recalculé.
// Un menu propose les réglages prédéfinis (percussion...).
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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class Seg { Delay = 0, Attack = 1, Hold = 2, Decay = 3, Release = 4, None = 5 };
    enum class Part { Node, Shape, Chip, Sustain, None };
    struct Hit { Seg seg = Seg::None; Part part = Part::None; };
    struct Chip { Seg seg; juce::Rectangle<float> rect; juce::String text; };
    struct Handle { Seg seg; Part part; juce::Point<float> pos; bool ghost; };
    struct Frame { juce::Rectangle<float> graph; float yTop, yBottom; neptune::core::EnvelopeLayout layout; std::vector<Chip> chips; };

    Frame frame() const;
    neptune::core::EnvelopeSegment& segment (neptune::core::EnvelopeDescription&, Seg) const;
    std::vector<Handle> handles (const Frame&) const;
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
    Seg highlight_ = Seg::None; // segment illuminé (valeur touchée ou survolée)

    Hit drag_;
    double frozenScale_ = 0.0;
    juce::Point<float> downPos_;
    double downMs_ = 0.0;
    juce::uint32 downTime_ = 0;
    bool longPressFired_ = false, moved_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};
