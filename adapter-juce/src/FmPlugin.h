#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "FmSysexIO.h"
#include "FmView.h"
#include "HostProbe.h"
#include "KnobModal.h"
#include "NeptuneKnob.h"
#include "ParamHost.h"
#include "PresetBar.h"
#include "PresetHost.h"
#include "PresetManager.h"
#include "ScaledRoot.h"
#include "neptune/core/FmPreset.h"
#include "neptune/core/FmSynth.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Theme.h"
#include "neptune/core/Undo.h"

// « FM6Op » (nom provisoire) : le module FM de type DX7 de Neptune en plugin CLAP instrument indépendant. Même moteur (msfa), même
// vue (FmView), mêmes presets (bibliothèque partagée avec Neptune et AnalogSynth), import / export sysex DX7.
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

class FmSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FmSynthEditor (FmSynthProcessor&);
    ~FmSynthEditor() override { stopTimer(); }

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    void openManager (bool saveAs);
    void paintContent (juce::Graphics&);
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const;

    ScaledRoot root_;
    FmSynthProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    neptune::core::theme::KnobStyle knobStyle_;

    std::unique_ptr<FmView> view_;
    std::unique_ptr<PresetBar> presetBar_;
    std::unique_ptr<FmSysexIO> sysex_;
    juce::OwnedArray<NeptuneKnob> knobs_;
    juce::TextButton undoButton_, redoButton_, panicButton_;
    juce::Label brandLabel_, infoLabel_;
    std::unique_ptr<KnobModal> modal_;
    std::unique_ptr<PresetManager> manager_;
    std::uint32_t lastUndoRevision_ = 0;
    std::uint32_t lastExtraVersions_ = 0;
    int lastVoices_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FmSynthEditor)
};
