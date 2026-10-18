#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/ShaperModule.h"
#include "neptune/core/ShaperPresets.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Theme.h"

// Interface du shaper de transitoires (maquette design/nova/ui/nova_transient_shaper_ui.html) : trois pages (Shaper, Sidechain, Settings),
// gabarit logique 1160 x 780 réduit pour tenir dans la fenêtre. Ne connaît que ParamHost et le module : la même interface sert l'insert
// de Neptune et le plugin CLAP « La Flèche ».
class ShaperEditor : public juce::Component, private juce::Timer
{
public:
    struct Options
    {
        bool brand = false;           // en-tête « LA FLÈCHE / TRANSIENT SHAPER · FLIBUSTE SUITE » (plugin séparé)
        bool bypass = false;          // bouton de bypass propre (plugin séparé)
        bool externalKey = false;     // entrée sidechain de l'hôte disponible (plugin séparé) ; faux dans un insert de Neptune
        std::function<double()> sampleRate;
    };

    ShaperEditor (ParamHost&, const neptune::core::ShaperModule*, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&,
                  neptune::core::theme::KnobStyle, Options);
    ~ShaperEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int designWidth = 1160, designHeight = 780;

private:
    class Surface;
    class ParamBar;
    class StateButton;
    class LevelMeter;
    class DetectionView;
    class EqView;
    friend class DetectionView;
    friend class EqView;

    void timerCallback() override;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String tx (const char* key) const;
    double value (neptune::core::ParamId id) const { return host_.parameters().get (id); }
    void setValue (neptune::core::ParamId id, double plain);              // un geste complet (début, valeur, fin)
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void setPage (int page);
    void refreshStates();
    void buildHeader();
    void buildShaperPage();
    void buildSidechainPage();
    void buildSettingsPage();
    ParamBar* addBar (juce::Component& parent, std::vector<juce::Component*>& page, neptune::core::ParamId, const char* labelKey, bool horizontal = false);
    StateButton* addState (juce::Component& parent, std::vector<juce::Component*>& page, neptune::core::ParamId, const char* labelKey,
                           std::vector<const char*> choices = {});
    void showPresetMenu();
    void toggleAB();
    std::map<neptune::core::ParamId, double> snapshot() const;
    void restore (const std::map<neptune::core::ParamId, double>&);

    ParamHost& host_;
    const neptune::core::ShaperModule* module_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    Options options_;

    std::unique_ptr<Surface> surface_;
    std::unique_ptr<KnobModal> modal_;
    int page_ = 0;
    std::array<std::vector<juce::Component*>, 3> pages_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<ParamBar*> bars_;
    std::vector<StateButton*> states_;
    std::vector<std::pair<NeptuneKnob*, neptune::core::ParamId>> knobs_;
    std::array<juce::TextButton, 3> navButtons_;
    juce::TextButton presetButton_, abButton_, bypassButton_, viewButton_;
    juce::Label latencyLabel_;
    std::unique_ptr<LevelMeter> inMeter_, gainMeter_, outMeter_;
    std::unique_ptr<DetectionView> detection_;
    std::unique_ptr<EqView> eq_;
    juce::Label settingsInfo_;
    // Diagnostic de développement : identifiant de construction et mesures de fluidité de l'interface (affichés en bas à gauche)
    struct Diag
    {
        double windowStart = 0.0, lastTick = 0.0, sumInterval = 0.0, maxInterval = 0.0, maxWork = 0.0, sumPaint = 0.0, maxPaint = 0.0;
        int ticks = 0, paints = 0, vblanks = 0, maxHeadJump = 0;
        std::uint32_t lastHead = 0;
    } diag_;
    juce::Label diagLabel_;
    int presetIndex_ = 0;
    int tick_ = 0;
    std::uint32_t eqVersions_ = 0, responseVersions_ = 0xFFFFFFFFu;
    bool slotB_ = false;
    std::map<neptune::core::ParamId, double> otherSlot_;
    juce::Rectangle<int> headerArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperEditor)
};
