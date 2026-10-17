#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "neptune/core/Control.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Gestion des assignations de la couche de contrôle (fenêtre flottante) : une ligne par assignation {source, cible, plage, courbe,
// inversion, mode}. La source peut être apprise (bouger un contrôleur MIDI), une macro ou une adresse OSC ; la cible est un paramètre
// (menu hiérarchique construit sur les adresses OSC) ou une action.
class ControlPanel : public juce::Component, private juce::Timer
{
public:
    ControlPanel (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    ~ControlPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    static constexpr int panelWidth = 900, panelHeight = 540;

private:
    struct Row;
    class Content;
    void timerCallback() override;
    void rebuild();                                   // reconstruit les lignes depuis la configuration
    void edit (std::size_t index, const std::function<void (neptune::core::control::Binding&)>&);
    void addAssignment();
    void remove (std::size_t index);
    void startLearning (int index);                   // -1 : aucune
    void showSourceMenu (std::size_t index, juce::Component& anchor);
    void showTargetMenu (std::size_t index, juce::Component& anchor);
    std::size_t count() const { return config_.bindings.size() + (hasPending_ ? 1 : 0); }
    const neptune::core::control::Binding* at (std::size_t) const;   // assignations réelles puis, si elle existe, celle en attente de cible
    juce::String targetLabel (const neptune::core::control::Binding&) const;
    juce::Colour colour (neptune::core::theme::Token) const;
    juce::String text (const char* key) const;

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::control::Config config_;           // copie de la configuration du moteur
    neptune::core::control::Binding pending_;         // nouvelle assignation : jamais enregistrée tant que sa cible manque
    bool hasPending_ = false;
    juce::TextButton newButton_;
    juce::Label emptyLabel_;
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    int learning_ = -1;
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanel)
};
