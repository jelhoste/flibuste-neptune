#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include "neptune/core/Theme.h"

// Clavier à l'écran (souris et tactile, plusieurs doigts) : joue la piste sélectionnée. « Maintien » (ou Maj+clic) : un clic
// sur une touche la laisse sonner, un second clic l'arrête. Sert à tester sans clavier MIDI, et à tenir des notes pendant qu'on
// règle le mixeur.
class SynthKeyboard : public juce::Component
{
public:
    explicit SynthKeyboard (const neptune::core::theme::Palette&);

    std::function<void (int key, float velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;

    void setHold (bool on) { hold_ = on; }
    void shiftOctave (int octaves);
    int centreKey() const noexcept { return centreKey_; }
    // Touches tenues par des notes MIDI reçues (bits 0..63 et 64..127) : affichées en plus des touches enfoncées à l'écran.
    void setHeldKeys (std::uint64_t lo, std::uint64_t hi) { if (lo != heldLo_ || hi != heldHi_) { heldLo_ = lo; heldHi_ = hi; repaint(); } }
    void releaseAll(); // relâche toutes les touches tenues (bouton « tout arrêter », changement de piste)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr float whiteKeyWidth = 26.0f; // largeur fixe d'une touche blanche : agrandir la fenêtre ajoute des touches
    // Le do central (do 4, note 60) est au milieu ; la fenêtre plus large ajoute des touches de chaque côté.
    static constexpr int defaultCentreKey = 60;

private:
    bool visible (int key) const;
    bool heldByMidi (int key) const noexcept;
    int keyAt (juce::Point<int>) const;
    juce::Rectangle<float> keyRect (int key) const;
    bool isBlack (int key) const noexcept;
    void press (int source, int key, bool latch);
    void release (int source);
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    int centreKey_ = defaultCentreKey; // touche blanche placée au milieu
    std::uint64_t heldLo_ = 0, heldHi_ = 0;
    bool hold_ = false;
    std::map<int, int> sourceKey_;   // doigt / souris -> touche pressée
    std::set<int> latched_;          // touches maintenues
    std::set<int> down_;             // touches enfoncées (affichage)
};
