#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Theme.h"
#include "neptune/core/UtilityModule.h"

// Interface de l'Utilitaire (spec-modules §8). Compacte et redimensionnable, une seule page : niveaux, stéréo, temps et polarité,
// vu-mètres d'entrée et de sortie, corrélateur. Ne connaît que ParamHost : la même interface sert l'insert de Neptune et le plugin
// CLAP séparé.
class UtilityEditor : public juce::Component, private juce::Timer
{
public:
    struct Options
    {
        bool brand = false;           // en-tête « MOUSSAILLON / UTILITY · FLIBUSTE SUITE » (plugin séparé)
        bool bypass = false;          // bouton de bypass propre (plugin séparé ; dans Neptune c'est celui de l'emplacement)
        std::function<double()> sampleRate; // pour afficher le délai en échantillons
    };

    UtilityEditor (ParamHost&, const neptune::core::StockModule* metersSource, const neptune::core::theme::Palette&,
                   const neptune::core::i18n::Translator&, neptune::core::theme::KnobStyle, Options);
    ~UtilityEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int preferredWidth = 760, preferredHeight = 470;

private:
    struct Toggle : juce::TextButton
    {
        neptune::core::ParamId id = 0;
        std::uint32_t seen = 0xFFFFFFFFu;
    };
    struct Choice : juce::ComboBox
    {
        neptune::core::ParamId id = 0;
        std::uint32_t seen = 0xFFFFFFFFu;
    };
    struct KnobSlot
    {
        std::unique_ptr<NeptuneKnob> knob;
        neptune::core::ParamId id = 0;
        const char* label = "";
        const char* dualLabel = nullptr; // autre libellé en mode « Double mono »
    };

    void timerCallback() override;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;
    void addKnob (neptune::core::ParamId, const char* label, const char* dualLabel = nullptr);
    Toggle* addToggle (neptune::core::ParamId, const char* label);
    Choice* addChoice (neptune::core::ParamId, std::initializer_list<const char*> items, const char* label);
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void refreshStates();
    void drawMeters (juce::Graphics&, juce::Rectangle<int>) const;
    void drawCorrelation (juce::Graphics&, juce::Rectangle<int>) const;
    double value (neptune::core::ParamId id) const { return host_.parameters().get (id); }

    ParamHost& host_;
    const neptune::core::StockModule* meters_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::theme::KnobStyle knobStyle_;
    Options options_;

    std::vector<KnobSlot> knobs_;
    std::vector<std::unique_ptr<Toggle>> toggles_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::pair<Choice*, juce::String>> choiceLabels_;
    std::unique_ptr<KnobModal> modal_;
    Toggle* bypass_ = nullptr;

    // vu-mètres : crête avec repli
    float peak_[4] = { 0, 0, 0, 0 };
    float correlation_ = 0.0f;

    juce::Rectangle<int> metersArea_, corrArea_, headerArea_;
    struct Group { juce::String title; juce::Rectangle<int> area; };
    std::vector<Group> groups_;
    juce::Label delayInfo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UtilityEditor)
};
