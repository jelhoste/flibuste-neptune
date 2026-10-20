#pragma once
// Interface de Microdécale (micro-décalage de hauteur de type H3000 MICROPITCHSHIFT) au style NOVA, sur le socle commun nova::EditorBase : page MICRO (vue
// des deux voies sur l'axe du temps, mode, huit potards : hauteur, retard et feedback de chaque voie, mix, sortie), page VOICES (niveaux et panoramiques des quatre
// sources, longueur de raccord, modulation de hauteur) et page SETTINGS. Même code pour le plugin séparé et pour l'insert de Neptune.
#include "NovaEditor.h"
#include "neptune/core/MicrodecaleModule.h"

class NovaMicrodecaleEditor : public nova::EditorBase
{
public:
    NovaMicrodecaleEditor (ParamHost&, const neptune::core::MicrodecaleModule*, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                           const neptune::core::i18n::Translator&, Options);
    ~NovaMicrodecaleEditor() override;
    void setPageForTest (int p) { setPage (p); }

private:
    class MicroView;

    void layout() override;
    void tick() override;
    std::vector<juce::String> presetNames() const override;
    std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int) const override;
    std::vector<neptune::core::ParamId> presetParamIds() const override;
    void buildMicroPage();
    void buildVoicesPage();

    const neptune::core::MicrodecaleModule* module_;

    // page MICRO
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr, *viewPanel_ = nullptr;
    nova::VMeter *inMeter_ = nullptr, *outMeter_ = nullptr;
    nova::Pill *inTrimPill_ = nullptr, *modePill_ = nullptr;
    nova::Text* note_ = nullptr;
    MicroView* view_ = nullptr;
    struct Column { nova::Panel* card; nova::KnobCard* knob; };
    std::vector<Column> columns_;   // PITCH L / R, DELAY L / R, FEEDBACK L / R, MIX, OUTPUT

    // page VOICES
    nova::Panel* voicesPanel_ = nullptr;
    std::vector<nova::KnobCard*> voiceCards_;   // niveaux (4), panoramiques (4), splice, mod, vitesse de mod
    nova::Pill* wavePill_ = nullptr;
};
