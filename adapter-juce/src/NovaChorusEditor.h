#pragma once
// Interface du chorus au style NOVA (maquette design/nova/ui/nova_chorus_ui.html), sur le socle commun nova::EditorBase : page CHORUS (retard gauche / droite au
// cours du temps ou réponse en fréquence du peigne, mini-contrôles, sept potards), page TONE (filtres et caractère BBD de la voie traitée) et page SETTINGS.
// En mode TAL (deux unités I et II, comme le chorus de TAL-NoiseMaker), les deux premiers potards règlent la vitesse de chaque unité et les autres sont éteints.
// Même code pour le plugin séparé et pour l'insert de Neptune (SYNC n'est utilisable que dans le plugin séparé, qui reçoit le tempo de l'hôte).
#include "NovaEditor.h"
#include "neptune/core/ChorusModule.h"

class NovaChorusEditor : public nova::EditorBase
{
public:
    NovaChorusEditor (ParamHost&, const neptune::core::ChorusModule*, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                      const neptune::core::i18n::Translator&, Options);
    ~NovaChorusEditor() override;
    void setPageForTest (int p) { setPage (p); }
    void showResponseForTest (bool r) { showResponse_ = r; refreshNow(); }

private:
    class ModView;
    class ToneView;
    struct Zone;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics&) override;
    std::vector<juce::String> presetNames() const override;
    std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int) const override;
    std::vector<neptune::core::ParamId> presetParamIds() const override;
    void buildChorusPage();
    void buildTonePage();
    void buildSettingsExtras();
    void rebindForMode();

    const neptune::core::ChorusModule* module_;
    int boundMode_ = -1;
    bool showResponse_ = false;

    // page CHORUS
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr, *viewPanel_ = nullptr;
    nova::VMeter *inMeter_ = nullptr, *outMeter_ = nullptr;
    nova::Pill *inTrimPill_ = nullptr, *viewPill_ = nullptr;
    ModView* view_ = nullptr;
    nova::Pill *modePill_ = nullptr, *syncPill_ = nullptr, *divPill_ = nullptr, *shapePill_ = nullptr, *voicesPill_ = nullptr, *unitIPill_ = nullptr, *unitIIPill_ = nullptr;
    struct Column { nova::Panel* card; nova::KnobCard* knob; };
    std::vector<Column> columns_;          // RATE, DEPTH, DELAY, WIDTH, FEEDBACK, MIX, OUTPUT

    // page TONE
    nova::Panel* tonePanel_ = nullptr;
    ToneView* toneView_ = nullptr;
    std::vector<std::unique_ptr<Zone>> zones_;

    // page SETTINGS (en plus des sections communes)
    nova::Text* modSection_ = nullptr;
    nova::Pill *interpPill_ = nullptr, *lfoStartPill_ = nullptr;

    juce::String tempoText_;
};
