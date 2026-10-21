#pragma once
// Interface d'AnalogSynth (synthé soustractif) au style NOVA, sur le socle commun nova::EditorBase : pages OSCILLATEURS (3 oscillateurs, sous-oscillateur, bruit),
// FILTRES (routage, deux filtres avec leur enveloppe), ENVELOPPES / VOIX (volume, pitch, polyphonie), MATRICE (grille de presets de la banque pour le concert),
// SETTINGS ; clavier à l'écran en bandeau masquable. La bibliothèque de presets (banques, programmes MIDI, gestionnaire, corbeille) est conservée : la pastille
// de preset de l'en-tête ouvre un menu NOVA et le gestionnaire existant se superpose à la fenêtre.
// L'éditeur ne connaît le synthé que par `SynthLink` (description du patch, notes) : le même éditeur sert au plugin indépendant et à une piste de Neptune.
#include <functional>
#include <memory>
#include <vector>
#include "EnvelopeEditor.h"
#include "NovaEditor.h"
#include "NovaPresetUi.h"
#include "NovaWidgets.h"
#include "SynthKeyboard.h"
#include "neptune/core/Synth.h"

struct SynthLink
{
    std::function<neptune::core::SynthDescription()> description;
    std::function<void (neptune::core::SynthDescription)> edit;   // annulable (clé de fusion « synth »)
    std::function<void()> beginGesture, endGesture, panic;
    std::function<void (int key, float velocity)> noteOn;
    std::function<void (int key)> noteOff;
    std::function<std::uint64_t (int word)> heldKeys;             // touches tenues (bits 0..63 puis 64..127)
    int track = 0;                                                // piste dont on règle les paramètres automatisables
    std::vector<neptune::core::ParamId> outputs;                  // potards de la colonne de gauche (volume, pan, gain, accordage)
};

class NovaSynthEditor : public nova::EditorBase
{
public:
    NovaSynthEditor (ParamHost&, PresetHost&, SynthLink, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&, Options);
    ~NovaSynthEditor() override;
    bool keyPressed (const juce::KeyPress&) override;
    void showPresetMenu (juce::Component* anchor);
    void setPageForTest (int p) { setPage (p); }
    void setKeyboardShownForTest (bool shown) { keyboardShown_ = shown; applyKeyboard(); resized(); }

private:
    class WaveTile;
    using Desc = neptune::core::SynthDescription;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics&) override;
    void pageChanged() override;
    void applyKeyboard();
    void buildLeftColumn();
    void buildOscPage();
    void buildFilterPage();
    void buildEnvPage();
    void buildMatrixPage();
    void buildKeyboard();

    // ---- fabriques : chaque commande enregistre sa mise à jour (page -1 : toujours)
    void edit (const std::function<void (Desc&)>&);
    void commit (const std::function<void (Desc&)>& change);   // changement discret : un geste complet
    void gestureBegin();
    void gestureEnd();
    nova::ValueSlider* slider (int page, juce::String title, double lo, double hi, double step, juce::String suffix, std::function<double (const Desc&)> get,
                               std::function<void (Desc&, double)> set, double def, std::function<bool (const Desc&)> enabled = {});
    nova::NumberPill* number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                              std::function<bool (const Desc&)> enabled = {});
    nova::ChoicePill* choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                              std::function<bool (const Desc&)> enabled = {});
    nova::Pill* toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set, std::function<bool (const Desc&)> enabled = {});
    nova::KnobCard* knob (int page, neptune::core::ParamId id, juce::String title, bool compact);
    juce::String paramTitle (neptune::core::ParamId) const;
    EnvelopeEditor* envelope (int page, std::function<neptune::core::EnvelopeDescription (const Desc&)> get, std::function<void (Desc&, const neptune::core::EnvelopeDescription&)> set,
                              std::function<bool (const Desc&)> enabled = {});
    void onUpdate (int page, std::function<void()> f);

    ParamHost& hostRef_;
    PresetHost& presets_;
    SynthLink link_;
    Desc desc_;
    bool dragging_ = false;
    bool keyboardShown_ = true;
    juce::String statusText_;
    std::unique_ptr<nova::PresetUi> ui_;
    std::vector<std::unique_ptr<nova::ChoicePill>> choices_;
    std::vector<std::vector<std::function<void()>>> updaters_;   // par page (index page + 1 : -1 est l'index 0)

    // colonne de gauche
    nova::Panel *outPanel_ = nullptr, *navPanel_ = nullptr;
    std::vector<nova::KnobCard*> outKnobs_;
    nova::Pill* kbPill_ = nullptr;

    // page OSC
    struct OscUi
    {
        nova::Panel* panel = nullptr;
        WaveTile* wave = nullptr;
        nova::NumberPill *octave = nullptr, *semitone = nullptr, *unison = nullptr;
        std::vector<nova::KnobCard*> knobs;
        nova::ChoicePill* curve = nullptr;
        nova::ValueSlider *spread = nullptr, *phase = nullptr, *pan = nullptr;
        nova::Pill *retrigger = nullptr, *sync = nullptr, *mute = nullptr;
    };
    OscUi osc_[neptune::core::numOscillators];
    struct SourceUi
    {
        nova::Panel* panel = nullptr;
        nova::KnobCard* level = nullptr;
        nova::ChoicePill *wave = nullptr, *octave = nullptr;
        nova::ValueSlider* pan = nullptr;
        nova::Pill* mute = nullptr;
    };
    SourceUi sub_, noise_;

    // page FILTRES
    nova::Panel* routePanel_ = nullptr;
    nova::ChoicePill* routing_ = nullptr;
    nova::ValueSlider* balance_ = nullptr;
    struct FilterUi
    {
        nova::Panel* panel = nullptr;
        nova::Pill* on = nullptr;
        nova::ChoicePill *model = nullptr, *mode = nullptr, *slope = nullptr;
        nova::KnobCard *cutoff = nullptr, *resonance = nullptr;
        nova::ValueSlider *key = nullptr, *velocity = nullptr, *depth = nullptr, *drive = nullptr;
        EnvelopeEditor* env = nullptr;
    };
    FilterUi filter_[2];

    // page ENVELOPPES / VOIX
    nova::Panel *ampPanel_ = nullptr, *pitchPanel_ = nullptr, *voicesPanel_ = nullptr;
    EnvelopeEditor *ampEnv_ = nullptr, *pitchEnv_ = nullptr;
    nova::KnobCard* pitchDepth_ = nullptr;
    nova::ChoicePill *polyMode_ = nullptr, *steal_ = nullptr;
    nova::ValueSlider *voices_ = nullptr, *glide_ = nullptr;

    // page MATRICE
    nova::Panel* matrixPanel_ = nullptr;
    nova::PresetTiles* tiles_ = nullptr;
    nova::Pill *colsPill_ = nullptr, *rowsPill_ = nullptr, *bankPrev_ = nullptr, *bankNext_ = nullptr, *bankPill_ = nullptr, *fieldPill_ = nullptr;

    // clavier
    nova::Panel* keyboardPanel_ = nullptr;
    SynthKeyboard* keyboard_ = nullptr;
    nova::Pill *octDown_ = nullptr, *octUp_ = nullptr, *holdPill_ = nullptr, *panicPill_ = nullptr;
};
