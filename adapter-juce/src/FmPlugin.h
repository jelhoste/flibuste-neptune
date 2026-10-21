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
#include "NovaFmEditor.h"
#include "ParamHost.h"
#include "PresetHost.h"
#include "PresetManager.h"
#include "neptune/core/FmPreset.h"
#include "neptune/core/FmSynth.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Theme.h"
#include "neptune/core/Undo.h"

// « FM6Op » (nom provisoire) : le module FM de type DX7 de Neptune en plugin CLAP instrument indépendant. Même moteur (msfa),
// interface NOVA (NovaFmEditor), mêmes presets (bibliothèque partagée avec Neptune et AnalogSynth), import / export sysex DX7.
class FmSynthProcessor : public juce::AudioProcessor,
                         public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                         public ParamHost,
                         public PresetHost,
                         private juce::AsyncUpdater
{
public:
    FmSynthProcessor();
    ~FmSynthProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FM6Op"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
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

    // ---- patch FM ----
    neptune::core::fm::FmDescription fmDescription() const { return engine_.description(); }
    void uiEditFm (neptune::core::fm::FmDescription, std::string name, std::string mergeKey = {});
    void uiBeginGesture() { undo_.beginGesture(); }
    void uiEndGesture() { undo_.endGesture(); }
    neptune::core::UndoManager& undoManager() { return undo_; }
    void uiNoteOn (int key, float velocity) { engine_.postNoteOn (key, velocity); }
    void uiNoteOff (int key) { engine_.postNoteOff (key); }
    void uiPanic() { engine_.postPanic(); }

    // ---- presets (PresetHost) ----
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
    bool acceptsPreset (const neptune::core::preset::Preset& p) const override { return p.module.empty() || p.module == neptune::core::fm::moduleId; }
    std::vector<std::string> presetKeys() const override
    {
        std::vector<std::string> keys;
        for (const auto& k : neptune::core::fm::keyedParams()) keys.push_back (k.key);
        return keys;
    }

    neptune::core::fm::FmSynthEngine& engine() { return engine_; }
    const neptune::core::settings::GlobalSettings& settings() const { return settings_; }
    void applySettings (const neptune::core::settings::GlobalSettings&);   // met à jour et enregistre les réglages globaux de la suite (thème, contraste, langue)

private:
    static BusesProperties makeBuses();
    void handleAsyncUpdate() override;
    void notifyHostOfParameterChanges (const std::vector<float>& before);
    std::vector<float> snapshotNormalised() const;

    neptune::core::fm::FmSynthEngine engine_;
    HostProbe probe_;
    neptune::core::UndoManager undo_;
    neptune::core::settings::GlobalSettings settings_;
    neptune::core::uistate::Size modal_[2] {};
    std::unique_ptr<neptune::core::preset::Library> library_;
    std::string currentPresetId_;
    std::uint32_t presetRevision_ = 1;
    std::atomic<int> midiBank_ { 0 };
    std::atomic<int> pendingBank_ { -1 }, pendingProgram_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmSynthProcessor)
};

// Fenêtre du plugin FM6Op : thème, contraste, langue ; reconstruit l'interface quand ils changent.
class NovaFmPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit NovaFmPluginEditor (FmSynthProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void buildContent();
    FmSynthProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    std::unique_ptr<NovaFmEditor> content_;
    juce::ComponentBoundsConstrainer constrainer_;
};
