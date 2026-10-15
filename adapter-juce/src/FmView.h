#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "SynthView.h"
#include "neptune/core/FmEngine.h"
#include "neptune/core/FmVoices.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Theme.h"

// Vue du module FM de type DX7 (spec-modules §17) : diagramme d'algorithme interactif, panneau de l'opérateur choisi (rapport /
// fixe, niveau, enveloppe graphique 4 vitesses / 4 niveaux, mise à l'échelle du clavier avec mini-courbe), page globale (LFO,
// enveloppe de pitch, voix, unisson, macros) et clavier à l'écran. Les réglages du patch sont annulables (description) ; les
// macros sont des potards liés aux paramètres de la piste.
class FmView : public juce::Component, private juce::Timer
{
public:
    FmView (ParamHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, neptune::core::theme::KnobStyle);
    ~FmView() override;

    // Taille de conception : en dessous, la vue entière est réduite proportionnellement ; le clavier se masque tout seul.
    static constexpr int designWidth = 1100, designHeight = 600, designHeightNoKeyboard = 464;
    void placeScaled (juce::Rectangle<int> area);

    std::function<void (const neptune::core::fm::FmDescription&)> onChange;
    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<void (int key, float velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;
    std::function<void()> onImportSysex, onExportVoice, onExportBank; // gérés par l'éditeur (fichiers)

    void setDescription (const neptune::core::fm::FmDescription&); // sans notification ; ignoré pendant un geste
    void setTrack (int track);                                      // rebranche les potards des macros
    void releaseKeys() { keyboard_.releaseAll(); }
    void setHeldKeys (std::uint64_t lo, std::uint64_t hi) { keyboard_.setHeldKeys (lo, hi); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Impl;
    void timerCallback() override;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;

    ParamHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    std::unique_ptr<NeptuneSliderLook> sliderLook_;
    SynthKeyboard keyboard_;
    juce::TextButton octDown_, octUp_, keyboardButton_;
    juce::ToggleButton hold_;
    int keyboardMode_ = -1;
    bool keyboardShown_ = true;
    juce::Rectangle<int> lastArea_;
    std::unique_ptr<Impl> impl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmView)
};
