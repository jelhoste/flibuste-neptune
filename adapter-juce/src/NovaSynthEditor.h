#pragma once
// Interface d'AnalogSynth (synthé soustractif) au style NOVA, sur le socle commun des instruments (NovaInstrumentBase) : pages OSCILLATEURS (3 oscillateurs,
// sous-oscillateur, bruit), FILTRES (routage, deux filtres avec leur enveloppe), ENVELOPPES / VOIX (volume, pitch, polyphonie), puis MATRICE (grille de presets de la
// banque pour le concert) et SETTINGS ; clavier à l'écran en bandeau masquable. La bibliothèque de presets (banques, programmes MIDI, gestionnaire, corbeille) est
// conservée : la pastille de preset de l'en-tête ouvre un menu NOVA et le gestionnaire existant se superpose à la fenêtre.
// L'éditeur ne connaît le synthé que par `SynthLink` (description du patch, notes) : le même éditeur sert au plugin indépendant et à une piste de Neptune.
#include "EnvelopeEditor.h"
#include "ModulationHost.h"
#include "NovaInstrumentBase.h"
#include "neptune/core/Synth.h"

struct SynthLink : InstrumentLink
{
    std::function<neptune::core::SynthDescription()> description;
    std::function<void (neptune::core::SynthDescription)> edit;   // annulable (clé de fusion « synth »)
    ModulationHost* modulation = nullptr;                         // plugin seul : la matrice de modulation (page MOD) ; nul dans Neptune, qui a son onglet MOD
};

class ModView;

class NovaSynthEditor : public NovaInstrumentBase
{
public:
    NovaSynthEditor (ParamHost&, PresetHost&, SynthLink, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&, Options);
    ~NovaSynthEditor() override;

private:
    class WaveTile;
    using Desc = neptune::core::SynthDescription;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics& g) override { paintStatus (g); }
    void reloadDescription() override { desc_ = describe_(); }
    void buildOscPage();
    void buildFilterPage();
    void buildEnvPage();
    void buildModPage();
    ModulationHost* modulation_ = nullptr;   // page MOD (plugin seul) : l'hôte de la matrice
    ModView* modView_ = nullptr;

    // ---- fabriques typées sur la description du patch
    void edit (const std::function<void (Desc&)>&);
    void commit (const std::function<void (Desc&)>& change);   // changement discret : un geste complet
    nova::ValueSlider* slider (int page, juce::String title, double lo, double hi, double step, juce::String suffix, std::function<double (const Desc&)> get,
                               std::function<void (Desc&, double)> set, double def, std::function<bool (const Desc&)> enabled = {});
    nova::NumberPill* number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                              std::function<bool (const Desc&)> enabled = {});
    nova::ChoicePill* choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                              std::function<bool (const Desc&)> enabled = {});
    nova::Pill* toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set, std::function<bool (const Desc&)> enabled = {});
    EnvelopeEditor* envelope (int page, std::function<neptune::core::EnvelopeDescription (const Desc&)> get, std::function<void (Desc&, const neptune::core::EnvelopeDescription&)> set,
                              std::function<bool (const Desc&)> enabled = {});

    std::function<Desc()> describe_;
    std::function<void (Desc)> apply_;
    Desc desc_;

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
    nova::NumberPill* bendRange_ = nullptr;   // plage du pitch bend (plugin seul : Neptune n'envoie pas de pitch bend au synthé)
    nova::ChoicePill *polyMode_ = nullptr, *steal_ = nullptr;
    nova::ValueSlider *voices_ = nullptr, *glide_ = nullptr;
};
