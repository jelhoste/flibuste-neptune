#pragma once
// Interface du delay à huit lignes au style NOVA (maquette design/nova/ui/nova_delay_ui.html), sur le socle commun nova::EditorBase : page DELAY (timeline des
// huit lignes avec barres déplaçables, vue FILTERS, sélecteur de ligne, potards de la ligne choisie), page TAPS (réglages secondaires de la ligne choisie ; la
// matrice des huit lignes viendra ensuite), page GLOBAL (routage, feedback global, ping-pong, saturation, largeur, ducking, freeze) et page SETTINGS.
// Même code pour le plugin séparé et pour l'insert de Neptune (SYNC n'est utilisable que dans le plugin séparé, qui reçoit le tempo de l'hôte).
#include "NovaEditor.h"
#include "neptune/core/DelayModule.h"

class NovaDelayEditor : public nova::EditorBase
{
public:
    NovaDelayEditor (ParamHost&, const neptune::core::DelayModule*, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle,
                     const neptune::core::i18n::Translator&, Options);
    ~NovaDelayEditor() override;
    void setPageForTest (int p) { setPage (p); }
    void selectLineForTest (int n) { selected_ = n; refreshNow(); }
    void showFiltersForTest (bool f) { showFilters_ = f; refreshNow(); }

private:
    class DelayView;
    class LineRow;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics&) override;
    std::vector<juce::String> presetNames() const override;
    std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int) const override;
    std::vector<neptune::core::ParamId> presetParamIds() const override;
    void buildDelayPage();
    void buildTapsPage();
    void buildGlobalPage();
    void buildSettingsExtras();
    void rebindLineControls();
    void selectLine (int n) { selected_ = n; }

    const neptune::core::DelayModule* module_;
    int selected_ = 0, boundLine_ = -1;
    int boundSync_ = -1;
    bool showFilters_ = false;

    // page DELAY
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr, *viewPanel_ = nullptr;
    nova::VMeter *inMeter_ = nullptr, *outMeter_ = nullptr;
    nova::Pill *inTrimPill_ = nullptr, *viewPill_ = nullptr;
    DelayView* view_ = nullptr;
    LineRow* rowDelay_ = nullptr;
    struct Column { nova::Panel* card; nova::KnobCard* knob; };
    std::vector<Column> columns_;          // TIME / DIV, LEVEL, PAN, FEEDBACK, puis MIX et OUTPUT (rebranchés sur la ligne choisie pour les quatre premiers)

    // page TAPS
    nova::Panel* tapsPanel_ = nullptr;
    LineRow* rowTaps_ = nullptr;
    std::vector<nova::KnobCard*> tapsCards_;   // LOW CUT, HIGH CUT, MOD DEPTH, MOD RATE, R OFFSET
    nova::Pill* invertPill_ = nullptr;

    // page GLOBAL
    nova::Panel* globalPanel_ = nullptr;
    nova::Pill *syncPill_ = nullptr, *routingPill_ = nullptr, *freezePill_ = nullptr;
    std::vector<nova::KnobCard*> globalCards_;
    nova::Text* syncNote_ = nullptr;

    // page SETTINGS (en plus des sections communes)
    nova::Text* delaySection_ = nullptr;
    nova::Pill* limiterPill_ = nullptr;
    nova::SubSlider* glideSlider_ = nullptr;

    juce::String metaText_, tempoText_;
};
