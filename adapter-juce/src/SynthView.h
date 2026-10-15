#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "EnvelopeEditor.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Synth.h"
#include "neptune/core/Theme.h"
#include "neptune/core/WaveCatalog.h"

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
    int firstKey() const noexcept { return firstKey_; }
    void releaseAll(); // relâche toutes les touches tenues (bouton « tout arrêter », changement de piste)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr float whiteKeyWidth = 26.0f; // largeur fixe d'une touche blanche : agrandir la fenêtre ajoute des touches
    static constexpr int minWhiteKeys = 7;

private:
    int endKey() const;          // première touche hors de la zone visible (selon la largeur)
    int keyAt (juce::Point<int>) const;
    juce::Rectangle<float> keyRect (int key) const;
    bool isBlack (int key) const noexcept;
    void press (int source, int key, bool latch);
    void release (int source);
    juce::Colour colour (neptune::core::theme::Token t) const;

    neptune::core::theme::Palette palette_;
    int firstKey_ = 48;
    bool hold_ = false;
    std::map<int, int> sourceKey_;   // doigt / souris -> touche pressée
    std::set<int> latched_;          // touches maintenues
    std::set<int> down_;             // touches enfoncées (affichage)
};

// Bouton de forme d'onde : dessine la forme et son nom ; un clic ouvre l'explorateur.
class WaveButton : public juce::Button
{
public:
    WaveButton (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    void setWave (int wave);
    int wave() const noexcept { return wave_; }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    int wave_ = neptune::core::kWaveSaw;
    bool haveData_ = false;
    std::vector<float> preview_;
};

// Vue du synthé soustractif (spec-modules §2) : trois pages (oscillateurs et mixeur, filtres, enveloppes et voix) et le clavier.
// Les valeurs automatisables sont des potards liés aux paramètres de la piste ; le reste est la description du patch, annulable.
class SynthView : public juce::Component, private juce::Timer
{
public:
    SynthView (ParamHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, neptune::core::theme::KnobStyle);
    ~SynthView() override;

    // Taille de conception : en dessous, la vue entière est réduite proportionnellement (au lieu de superposer ses éléments).
    static constexpr int designWidth = 1100, designHeight = 570;
    // Place la vue dans `area` ; si `area` est plus petite que la taille de conception, tout est réduit d'un même facteur.
    void placeScaled (juce::Rectangle<int> area);

    std::function<void (const neptune::core::SynthDescription&)> onChange; // à chaque modification par l'utilisateur
    std::function<void()> onGestureBegin, onGestureEnd;                    // entourent un glissé : une seule entrée d'annulation
    std::function<void (int key, float velocity)> onNoteOn;
    std::function<void (int key)> onNoteOff;

    void setDescription (const neptune::core::SynthDescription&); // sans notification ; ignoré pendant un geste
    void setTrack (int track);                                    // rebranche les potards sur les paramètres de cette piste
    void releaseKeys() { keyboard_.releaseAll(); }

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    struct OscPanel;
    struct Knob
    {
        std::unique_ptr<NeptuneKnob> knob;
        std::function<neptune::core::ParamId (int track)> idFor;
        const char* labelKey = "";
        int page = 0;
    };

    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    NeptuneKnob* addKnob (int page, const char* labelKey, std::function<neptune::core::ParamId (int)> idFor);
    void setPage (int page);
    void refreshControls();
    void emit();
    void gesture (const std::function<void()>& change); // un geste complet : début, changement, notification, fin
    void openExplorer (int osc);
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();

    ParamHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    neptune::core::SynthDescription d_;
    int track_ = 0;
    int page_ = 0;
    bool updating_ = false, dragging_ = false;
    std::vector<std::uint32_t> shownVersions_; // versions des paramètres au dernier dessin (les valeurs sous les potards suivent l'automation)

    std::vector<Knob> knobs_;
    std::vector<std::pair<int, juce::Component*>> pageControls_; // (page, composant) pour la visibilité
    juce::OwnedArray<juce::TextButton> tabButtons_;
    std::unique_ptr<KnobModal> modal_;

    // composants nommés
    std::unique_ptr<OscPanel> panels_[neptune::core::numOscillators];
    struct Extras;
    std::unique_ptr<Extras> x_;

    SynthKeyboard keyboard_;
    juce::TextButton octDown_, octUp_;
    juce::ToggleButton hold_;
    juce::Label keysHint_;

    friend struct OscPanel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthView)
};
