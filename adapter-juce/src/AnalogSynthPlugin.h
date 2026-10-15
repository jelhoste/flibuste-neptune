#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "HostProbe.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "ScaledRoot.h"
#include "PresetBar.h"
#include "PresetHost.h"
#include "PresetManager.h"
#include "SynthView.h"
#include "neptune/core/AnalogSynth.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Preset.h"
#include "neptune/core/PresetLibrary.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Theme.h"
#include "neptune/core/Undo.h"

// « AnalogSynth » (nom provisoire) : le synthé soustractif de Neptune publié comme plugin CLAP instrument indépendant.
// Même moteur (neptune::core::analogsynth), même vue (SynthView), plus le système de presets (Preset.h) : banques 0..127 pilotées
// par MIDI Bank Select / Program Change, grille concert, bibliothèque utilisateur sur disque, presets livrés en lecture seule.
class AnalogSynthProcessor : public juce::AudioProcessor,
                             public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                             public ParamHost,
                             public PresetHost,
                             private juce::AsyncUpdater
{
public:
    AnalogSynthProcessor();
    ~AnalogSynthProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AnalogSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- ParamHost ----
    neptune::core::ParameterSet& parameters() override { return engine_.params(); }
    void uiBeginEdit (neptune::core::ParamId) override;
    void uiSetNormalised (neptune::core::ParamId, double) override;
    void uiEndEdit (neptune::core::ParamId) override;
    void uiResetToDefault (neptune::core::ParamId) override;
    bool uiUndo() override;
    bool uiRedo() override;
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const override { return modal_[t == neptune::core::uistate::Terminal::Touch ? 1 : 0]; }
    void setModalSize (neptune::core::uistate::Terminal t, neptune::core::uistate::Size s) override { modal_[t == neptune::core::uistate::Terminal::Touch ? 1 : 0] = s; }

    // ---- patch du synthé (description non automatisable) ----
    neptune::core::SynthDescription synthDescription() const { return engine_.description(); }
    void uiEditSynth (neptune::core::SynthDescription, std::string name, std::string mergeKey = {});
    void uiBeginGesture() { undo_.beginGesture(); }
    void uiEndGesture() { undo_.endGesture(); }
    neptune::core::UndoManager& undoManager() { return undo_; }

    // ---- notes de l'interface ----
    void uiNoteOn (int key, float velocity) { engine_.postNoteOn (key, velocity); }
    void uiNoteOff (int key) { engine_.postNoteOff (key); }
    void uiPanic() { engine_.postPanic(); }

    // ---- presets (thread de message) : PresetHost ----
    neptune::core::preset::Library& library() override { return *library_; }
    std::string currentPresetId() const override { return currentPresetId_; }
    const neptune::core::preset::Preset* currentPreset() const override { return currentPresetId_.empty() ? nullptr : library_->find (currentPresetId_); }
    std::uint32_t presetRevision() const override { return presetRevision_; }
    void touchPresets() override { ++presetRevision_; }
    bool loadPreset (const std::string& id) override;
    bool stepPreset (int direction) override;
    bool isModified() const override;
    bool canSaveCurrent() const override;
    bool saveCurrent (std::string* error = nullptr) override;
    bool overwritePreset (const std::string& id, std::string* error = nullptr) override;
    bool saveCurrentAs (const std::string& name, const std::string& type, neptune::core::preset::Preset* out = nullptr,
                        std::string* error = nullptr) override;
    bool saveConfig (const neptune::core::preset::LibraryConfig&) override;
    const neptune::core::preset::LibraryConfig& presetConfig() const override { return library_->config(); }

    neptune::core::analogsynth::AnalogSynthEngine& engine() { return engine_; }
    const neptune::core::settings::GlobalSettings& settings() const { return settings_; }

private:
    static BusesProperties makeBuses();
    void handleAsyncUpdate() override; // Program Change MIDI : appliqué sur le thread de message
    void notifyHostOfParameterChanges (const std::vector<float>& before);
    std::vector<float> snapshotNormalised() const;

    neptune::core::analogsynth::AnalogSynthEngine engine_;
    HostProbe probe_;
    neptune::core::UndoManager undo_;
    neptune::core::settings::GlobalSettings settings_;
    neptune::core::uistate::Size modal_[2] {};
    std::unique_ptr<neptune::core::preset::Library> library_;
    std::string currentPresetId_;
    std::uint32_t presetRevision_ = 1;

    std::atomic<int> midiBank_ { 0 };           // dernier CC 0 reçu (Bank Select MSB)
    std::atomic<int> pendingBank_ { -1 }, pendingProgram_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogSynthProcessor)
};

class AnalogSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AnalogSynthEditor (AnalogSynthProcessor&);
    ~AnalogSynthEditor() override { stopTimer(); }

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void refreshPresetBar();
    void openManager (bool saveAs = false);
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const;
    void paintContent (juce::Graphics&);

    ScaledRoot root_;
    AnalogSynthProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    neptune::core::theme::KnobStyle knobStyle_;

    std::unique_ptr<SynthView> synthView_;
    juce::OwnedArray<NeptuneKnob> knobs_;
    juce::TextButton undoButton_, redoButton_, panicButton_;
    std::unique_ptr<PresetBar> presetBar_;
    juce::Label brandLabel_, infoLabel_;
    std::unique_ptr<KnobModal> modal_;
    std::unique_ptr<PresetManager> manager_;

    std::uint32_t lastUndoRevision_ = 0;
    int lastVoices_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogSynthEditor)
};
