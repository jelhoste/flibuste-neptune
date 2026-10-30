#pragma once
// Interface de FM6Op (module FM de type DX7, moteur msfa) au style NOVA, sur le socle commun des instruments (NovaInstrumentBase) : pages OPÉRATEURS (diagramme
// d'algorithme interactif, réglages de l'opérateur choisi, enveloppe à 4 vitesses / 4 niveaux, mise à l'échelle du clavier), GLOBAL (LFO, enveloppe de pitch, voix,
// accordage, contrôleurs, macros automatisables), puis MATRICE (grille de presets) et SETTINGS ; clavier à l'écran masquable ; import / export sysex DX7.
// L'éditeur ne connaît le module que par `FmLink` (description du patch, notes) : le même éditeur sert au plugin indépendant et à une piste de Neptune.
#include "FmSysexIO.h"
#include "ModulationHost.h"
#include "NovaInstrumentBase.h"
#include "neptune/core/FmVoices.h"

struct FmLink : InstrumentLink
{
    std::function<neptune::core::fm::FmDescription()> description;
    std::function<void (neptune::core::fm::FmDescription, std::string name, std::string mergeKey)> edit;
    ModulationHost* modulation = nullptr;   // plugin seul : la matrice de modulation (page MOD) ; nul dans Neptune, qui a son onglet MOD
};

class ModView;

class NovaFmEditor : public NovaInstrumentBase
{
public:
    NovaFmEditor (ParamHost&, PresetHost&, FmLink, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&, Options);
    ~NovaFmEditor() override;

private:
    class AlgoView;
    class EnvView;
    class ScaleView;
    using Desc = neptune::core::fm::FmDescription;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics& g) override { paintStatus (g); }
    void reloadDescription() override { desc_ = describe_(); }
    void buildOperatorPage();
    void buildGlobalPage();
    void buildModPage();
    ModulationHost* modulation_ = nullptr;   // page MOD (plugin seul) : l'hôte de la matrice
    ModView* modView_ = nullptr;

    void edit (const std::function<void (Desc&)>&, const char* name = "FM", const char* mergeKey = "fm");
    nova::ValueSlider* slider (int page, juce::String title, double lo, double hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set, int def,
                               std::function<bool (const Desc&)> enabled = {}, std::function<juce::String (double)> format = {});
    nova::NumberPill* number (int page, juce::String label, int lo, int hi, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set,
                              std::function<juce::String (int)> text = {});
    nova::ChoicePill* choice (int page, juce::String label, std::vector<juce::String> items, std::function<int (const Desc&)> get, std::function<void (Desc&, int)> set);
    nova::Pill* toggle (int page, juce::String text, std::function<bool (const Desc&)> get, std::function<void (Desc&, bool)> set);
    nova::ValueSlider* opSlider (juce::String title, int field, int hi, int def, std::function<juce::String (double)> format = {});

    std::function<Desc()> describe_;
    std::function<void (Desc, std::string, std::string)> apply_;
    Desc desc_;
    int selectedOp_ = 5;   // OP1
    std::unique_ptr<FmSysexIO> sysex_;
    std::vector<neptune::core::ParamId> macroIds_;

    // page OPERATEURS
    nova::Panel *algoPanel_ = nullptr, *opPanel_ = nullptr;
    AlgoView* algo_ = nullptr;
    nova::Pill *algoPrev_ = nullptr, *algoNext_ = nullptr, *keySync_ = nullptr, *importPill_ = nullptr, *exportVoicePill_ = nullptr, *exportBankPill_ = nullptr, *opOn_ = nullptr;
    nova::NumberPill* algoNumber_ = nullptr;
    nova::ValueSlider* feedback_ = nullptr;
    juce::TextEditor* name_ = nullptr;
    nova::Text *nameText_ = nullptr, *freqText_ = nullptr, *scaleText_ = nullptr;
    nova::ChoicePill *mode_ = nullptr, *leftCurve_ = nullptr, *rightCurve_ = nullptr;
    std::vector<nova::ValueSlider*> opSliders_;   // coarse, fine, detune, niveau, vélocité, rate scaling, AMS
    std::vector<nova::ValueSlider*> scaleSliders_; // rupture, profondeur gauche, droite
    EnvView* opEg_ = nullptr;
    ScaleView* scale_ = nullptr;

    // page GLOBAL
    nova::Panel *lfoPanel_ = nullptr, *pitchPanel_ = nullptr, *voicesPanel_ = nullptr, *tunePanel_ = nullptr, *macrosPanel_ = nullptr;
    std::vector<nova::ValueSlider*> lfoSliders_, voiceSliders_, tuneSliders_;
    nova::ChoicePill *lfoWave_ = nullptr, *polyMode_ = nullptr, *steal_ = nullptr;
    nova::Pill* lfoSync_ = nullptr;
    EnvView* pitchEg_ = nullptr;
    std::vector<nova::KnobCard*> macros_;
};
