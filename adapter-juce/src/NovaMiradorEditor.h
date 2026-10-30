#pragma once
// Interface de Mirador (mesure et visualisation) au style NOVA, sur le socle commun nova::EditorBase : pages SONIE (valeurs EBU R128, historique, cible),
// NIVEAUX (crête, maintien, RMS, écrêtage, facteur de crête), STÉRÉO (vectorscope, corrélation, équilibre, largeur), SPECTRE et SETTINGS.
// L'éditeur ne fait que lire le moteur (MiradorEngine) ; le spectre est calculé ici, sur le fil d'interface, par SpectrumAnalyzer (celui de l'égaliseur).
#include "NovaEditor.h"
#include "neptune/core/Mirador.h"

class NovaMiradorEditor : public nova::EditorBase
{
public:
    NovaMiradorEditor (ParamHost&, neptune::core::mirador::MiradorEngine&, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                       const neptune::core::i18n::Translator&, Options);
    ~NovaMiradorEditor() override;
    void setPageForTest (int p) { setPage (p); }

private:
    class LoudView;
    class LevelsView;
    class StereoView;
    class SpectrumView;

    void layout() override;
    void tick() override;
    void pageChanged() override;
    void buildCommon();
    void buildLoudPage();
    void buildLevelsPage();
    void buildStereoPage();
    void buildSpectrumPage();

    neptune::core::mirador::MiradorEngine& engine_;

    // en commun (toutes les pages)
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr;
    nova::VMeter* peakMeter_ = nullptr;
    nova::Pill* resetPill_ = nullptr;

    // une page = un cadre et une vue
    nova::Panel *loudPanel_ = nullptr, *levelsPanel_ = nullptr, *stereoPanel_ = nullptr, *spectrumPanel_ = nullptr;
    LoudView* loud_ = nullptr;
    LevelsView* levels_ = nullptr;
    StereoView* stereo_ = nullptr;
    SpectrumView* spectrum_ = nullptr;
    nova::Pill *targetPill_ = nullptr, *customPill_ = nullptr, *tolerancePill_ = nullptr, *historyPill_ = nullptr, *resetPlayPill_ = nullptr, *resetLoudPill_ = nullptr;
    nova::Pill *holdPill_ = nullptr, *fallPill_ = nullptr, *rmsPill_ = nullptr, *resetPeaksPill_ = nullptr;
};
