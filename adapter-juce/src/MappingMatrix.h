#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>
#include "neptune/core/I18n.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/MappingEdit.h"
#include "neptune/core/Theme.h"

// Matrice notes (horizontal) × vélocité (vertical) des zones de l'instrument (palier 1, jalon 5).
//  - un rectangle par « place » (mêmes notes/vélocités/Groupe/déclenchement) ; ×N = alternatives round-robin ;
//  - glisser le rectangle : déplace (la note racine reste ; Maj enfoncé pendant le glissé : elle suit) ;
//  - Ctrl/Cmd + glisser : duplique puis déplace la copie ; glisser un BORD : étire (toute la sélection, du même écart) ;
//  - clic : sélectionne (Ctrl : bascule, Maj : ajoute) ; glisser dans le vide : rectangle de sélection ;
//  - les chevauchements sont permis et signalés (hachures) ; Suppr : supprime la sélection ;
//  - molette : zoom sur les notes ; barre : défilement. Zones de saisie larges (tactile).
// La logique des modifications est dans core/MappingEdit.h : chaque geste est recalculé depuis la description du début du
// geste, et se termine en UNE entrée d'annulation (onGestureBegin/End + clé de fusion côté éditeur).
class MappingMatrix : public juce::Component, private juce::ScrollBar::Listener
{
public:
    MappingMatrix (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);

    void setDescription (const neptune::core::InstrumentDescription&); // sans notification ; ignoré pendant un glissé
    void setSelection (const std::vector<int>& zones);                 // indices de zones (comme la liste)
    void duplicateSelection();                                         // copies sur place, sélectionnées

    std::function<void (const std::vector<int>&)> onSelect;
    std::function<void (neptune::core::InstrumentDescription, const char* name)> onEdit;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void()> onDeleteRequested;
    std::function<void (int key, float velocity)> onNoteOn;  // clavier sous la matrice : bas de la touche = vélocité forte, haut = faible
    std::function<void (int key)> onNoteOff;
    std::function<void (juce::Component& anchor)> onOpenVelocity; // réponse de vélocité / fondu entre couches
    std::function<void (juce::Component& anchor)> onOpenPolyphony; // limite et vol de voix, mono / legato

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Op { None, Move, Resize, Band };
    struct Hit { int place = -1; bool edge = false; neptune::core::mapping::Edge which = neptune::core::mapping::Edge::KeyLow; bool both = false;
                 neptune::core::mapping::Edge second = neptune::core::mapping::Edge::VelLow; };

    void scrollBarMoved (juce::ScrollBar*, double newStart) override;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;

    void rebuildPlaces();
    void rebuildViewChoices();
    neptune::core::mapping::View currentView() const;
    juce::Rectangle<float> plotArea() const;
    float keyX (double key) const;           // bord gauche de la note
    float velY (double level) const;         // level 0..127 (127 en haut)
    juce::Rectangle<float> placeRect (const neptune::core::mapping::Place&) const;
    Hit hitTest (juce::Point<float>) const;
    std::vector<int> zonesOfSelectedPlaces() const;
    bool placeSelected (const neptune::core::mapping::Place&) const;
    void selectPlaces (const std::vector<int>& placeIndices, bool add);
    void zoom (double factor, double aroundKey);
    void clampView();
    void updateScrollBar();

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::InstrumentDescription description_;
    std::vector<neptune::core::mapping::Place> places_;
    std::vector<neptune::core::LayerFade> fades_;   // par zone de la description (fondu entre couches)
    std::vector<int> selected_;                 // indices de zones
    neptune::core::mapping::View view_;
    std::vector<neptune::core::mapping::View> viewChoices_;
    std::string viewSignature_;

    juce::ComboBox viewBox_;
    juce::TextButton zoomIn_, zoomOut_, zoomFit_, duplicate_, velocity_, polyphony_;
    juce::Label hint_;
    juce::ScrollBar scroll_ { false };
    double keyStart_ = 0.0, keySpan_ = 128.0;

    // Geste en cours
    Op op_ = Op::None;
    Hit down_;
    juce::Point<float> downPos_, bandEnd_;
    neptune::core::InstrumentDescription base_;     // description au début du geste
    std::vector<int> editZones_;                    // zones modifiées par le geste
    bool keyboardDrag_ = false;
    bool duplicating_ = false, started_ = false, extendOnUp_ = false, toggleOnUp_ = false;
    int hoverPlace_ = -1;
    int pressedKey_ = -1;     // touche du clavier enfoncée à la souris / au doigt
    float pressedVelocity_ = 0.0f;
    juce::Rectangle<float> keyboardArea() const;
    bool playKeyboard (juce::Point<float>, bool starting);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MappingMatrix)
};
