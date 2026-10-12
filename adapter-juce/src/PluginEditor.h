#pragma once
#include "PluginProcessor.h"
#include "EnvelopeEditor.h"
#include "ImportWizard.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ZoneTable.h"
#include <map>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Fenêtre de sonde : paramètres en direct, et ce que l'hôte envoie (transport, notes CLAP, expressions, modulation).
class NeptuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeptuneEditor (NeptuneProcessor&);
    ~NeptuneEditor() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override; // Ctrl/Cmd+Z : annuler ; Ctrl/Cmd+Maj+Z ou Ctrl+Y : rétablir

private:
    void timerCallback() override;

    NeptuneProcessor& processor_;
    // Langue, thème et contraste viennent de settings.json, lus une fois au démarrage de l'instance.
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (std::string_view key) const { return juce::String::fromUTF8 (tr_.tr (key).data(), static_cast<int> (tr_.tr (key).size())); }
    juce::Label latencyLabel_;
    juce::Label recoveryLabel_;
    juce::Label profileLabel_;
    juce::TextButton profileSwitchButton_, profileLockButton_;
    void updateProfileControls();

    // Potards génériques (palier 0) et leur modale.
    juce::OwnedArray<NeptuneKnob> knobs_;
    std::unique_ptr<KnobModal> modal_;
    juce::TextButton undoButton_, redoButton_;

    // Sampler (palier 1) : chargement d'un sample et réglages de la zone (le premier jalon n'a qu'une zone).
    juce::TextButton loadButton_, modeButton_;
    juce::Label statusLabel_, rootLabel_;
    juce::Slider rootSlider_ { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    std::unique_ptr<EnvelopeEditor> envelopeEditor_;
    juce::TextButton viewButton_; // bascule entre la vue Sampler et la vue Sonde (texte de diagnostic de l'hôte)
    bool showProbe_ = false;
    void updateViewVisibility();
    std::unique_ptr<juce::FileChooser> chooser_, importChooser_;

    // Zones multiples (jalon 3) : liste, import par gabarit de nom, suppression ; les réglages agissent sur la sélection.
    std::unique_ptr<ZoneTable> zoneTable_;
    std::unique_ptr<ImportWizard> wizard_;
    juce::TextButton importButton_, removeButton_;
    juce::Label selectionLabel_;
    std::uint32_t lastRevision_ = 0xFFFFFFFFu;
    neptune::core::InstrumentDescription cachedDescription_;
    void startImport();
    void openWizard (std::vector<std::string> paths);
    void closeWizard();
    void applyImport (const ImportWizard::Result&);
    void removeSelectedZones();

    // Tables chargées en mémoire : alternatives round-robin, relâchements, un Groupe à la fois (vidage réversible).
    juce::Label loadTitle_;
    juce::ToggleButton rrToggle_, releaseToggle_, streamToggle_;
    juce::Label preloadLabel_;
    juce::ComboBox preloadBox_; // Ko de PCM gardés en mémoire par sample en mode streaming
    juce::OwnedArray<juce::ToggleButton> groupToggles_;
    juce::Component groupsHolder_;
    juce::Viewport groupsViewport_;
    neptune::core::memory::FileInfoCache fileInfo_; // en-têtes des fichiers : mémoire exacte, un fichier n'est ouvert qu'une fois
    std::string groupsSignature_;
    void rebuildLoadPanel();
    bool updatingControls_ = false;
    void chooseSample();
    void editZone (const std::function<void (neptune::core::ZoneDescription&)>&);
    void updateSamplerControls();
    juce::String statusText() const;
    juce::String baseStatusText() const;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void updateUndoButtons();
    juce::TextButton restoreButton_, discardButton_;
    bool recoveryShown_ = false;
    void updateRecoveryBanner();
    juce::OwnedArray<juce::TextButton> latencyButtons_; // test de la latence modifiable à l'exécution
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeptuneEditor)
};
