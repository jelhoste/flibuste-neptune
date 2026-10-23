#pragma once
// Interface de Convolution (chargeur de réponses impulsionnelles à latence nulle) au style NOVA, sur le socle commun nova::EditorBase :
// page IR (forme d'onde et spectre de l'IR avec marqueurs de début / fin, choix du fichier, retouches de l'IR), page TONE (pré-délai, largeur, mix, sortie, EQ 3 bandes,
// passe-haut et passe-bas), page LEVELS (crête, RMS, LUFS en entrée et en sortie) et page SETTINGS. Même code pour le plugin séparé et pour l'insert de Neptune.
#include <memory>

#include "NovaEditor.h"
#include "neptune/core/ConvolutionModule.h"

class NovaConvolutionEditor : public nova::EditorBase, public juce::FileDragAndDropTarget
{
public:
    NovaConvolutionEditor (ParamHost&, neptune::core::ConvolutionModule*, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                           const neptune::core::i18n::Translator&, Options);
    ~NovaConvolutionEditor() override;
    void setPageForTest (int p) { setPage (p); }

private:
    class IrView;
    class LevelsView;
    class EqView;

    void layout() override;
    void tick() override;
    std::vector<juce::String> presetNames() const override;
    std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int) const override;
    std::vector<neptune::core::ParamId> presetParamIds() const override;
    void buildIrPage();
    void buildTonePage();
    void buildLevelsPage();
    void chooseFile();
    void setIrFile (const std::string& path);   // annulable
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    juce::String statusText() const;

    neptune::core::ConvolutionModule* module_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String shownFile_, shownStatus_;

    // colonne de gauche (toutes les pages)
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr;
    nova::VMeter *inMeter_ = nullptr, *outMeter_ = nullptr;
    nova::Pill* inTrimPill_ = nullptr;

    // page IR
    nova::Panel *irPanel_ = nullptr, *optionsPanel_ = nullptr;
    nova::Pill *loadPill_ = nullptr, *clearPill_ = nullptr, *wavePill_ = nullptr, *spectrumPill_ = nullptr;
    nova::Text *fileText_ = nullptr, *statusText_ = nullptr;
    IrView* irView_ = nullptr;
    std::vector<nova::KnobCard*> irKnobs_;                 // étirement, début, fin, attaque, décroissance
    nova::Pill *modePill_ = nullptr, *trimPill_ = nullptr, *lenPill_ = nullptr, *revPill_ = nullptr, *normPill_ = nullptr;

    // page TONE
    nova::Panel *voicePanel_ = nullptr, *eqPanel_ = nullptr;
    std::vector<nova::KnobCard*> voiceKnobs_;              // pré-délai, largeur, mix, sortie
    nova::Pill* eqPill_ = nullptr;
    struct EqColumn { nova::Pill *on, *type, *slope; };
    std::vector<EqColumn> eqColumns_;                      // 3 bandes, passe-haut, passe-bas
    EqView* eqView_ = nullptr;                             // courbe de réponse avec une poignée par bande et par filtre

    // page LEVELS
    nova::Panel* levelsPanel_ = nullptr;
    LevelsView* levelsView_ = nullptr;
    nova::Pill* resetPill_ = nullptr;
    std::vector<nova::KnobCard*> levelKnobs_;              // fenêtre RMS, retombée de la crête
};
