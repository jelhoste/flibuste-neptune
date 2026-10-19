#pragma once
// Interface de l'égaliseur au style NOVA (maquette design/nova/ui/nova_equalizer_ui.html) : page EQUALIZER (vu-mètres, courbe de réponse interactive,
// bandeau de bandes, barre d'outils et potards de la bande sélectionnée) et page DYNAMICS (détection de la bande dynamique, absente de la maquette).
// Les 16 bandes de ZL sont des emplacements fixes : une bande « existe » quand son état n'est pas Off. Même code pour le plugin séparé et l'insert.
#include "NovaEditor.h"
#include "neptune/core/EqualizerModule.h"

class NovaEqualizerEditor : public nova::EditorBase
{
public:
    NovaEqualizerEditor (ParamHost&, const neptune::core::EqualizerModule*, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                         const neptune::core::i18n::Translator&, Options);
    ~NovaEqualizerEditor() override;
    void setPageForTest (int p) { setPage (p); }
    void selectBandForTest (int b) { select (b); }

private:
    class CurveView;
    class BandStrip;
    class BandLabel;

    void layout() override;
    void tick() override;
    void pageChanged() override { assignKnobs(); }
    std::vector<juce::String> presetNames() const override;
    std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int) const override;
    std::vector<neptune::core::ParamId> presetParamIds() const override;
    void buildMain();
    void buildDynamics();
    void buildBottomBar();

    // ---- bandes
    bool active (int band) const;
    int firstActive() const;
    int addBand (double freq, double gain);                // retourne l'emplacement créé, ou -1 si les 16 sont pris
    void removeBand (int band);
    void select (int band);
    void showBandMenu (int band);
    void showChoiceMenu (juce::Component* target, neptune::core::ParamId, std::vector<juce::String> labels);
    void bindSelected();
    void assignKnobs();
    juce::String typeLabel (int type) const;

    const neptune::core::EqualizerModule* module_;
    int selected_ = -1;
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr, *curvePanel_ = nullptr, *detPanel_ = nullptr, *toolbarPanel_ = nullptr;
    nova::VMeter *inMeter_ = nullptr, *outMeter_ = nullptr;
    CurveView* curve_ = nullptr;
    BandStrip* strip_ = nullptr;
    BandLabel* label_ = nullptr;
    nova::Pill *typePill_ = nullptr, *onPill_ = nullptr, *dynPill_ = nullptr, *stereoPill_ = nullptr, *deletePill_ = nullptr;
    std::vector<nova::KnobCard*> knobs_, detKnobs_;
    nova::Pill *detTypePill_ = nullptr, *detSlopePill_ = nullptr, *detLinkPill_ = nullptr;
    nova::Pill *dynBypassPill_ = nullptr;
    nova::Pill *phasePill_ = nullptr, *extPill_ = nullptr, *outPill_ = nullptr, *scalePill_ = nullptr, *minPhasePill_ = nullptr;
    juce::String metaText_, detTitle_;
    int lastType_ = -2, lastDyn_ = -2, lastSel_ = -2;
};
