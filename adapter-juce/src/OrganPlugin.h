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
#include "OrganView.h"
#include "ParamHost.h"
#include "ScaledRoot.h"
#include "neptune/core/I18n.h"
#include "neptune/core/OrganSynth.h"
#include "neptune/core/Settings.h"
#include "neptune/core/Theme.h"
#include "neptune/core/Undo.h"

// « Orgamon » (nom provisoire) : l'orgue à roues phoniques de Neptune (moteur de setBfree) en plugin CLAP instrument indépendant.
class OrganProcessor : public juce::AudioProcessor,
                       public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                       public ParamHost
{
public:
    OrganProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Orgamon"; }
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

    neptune::core::organ::OrganDescription organDescription() const { return engine_.description(); }
    void uiEditOrgan (neptune::core::organ::OrganDescription, std::string name, std::string mergeKey = {});
    void uiPanic() { engine_.postPanic(); }
    neptune::core::UndoManager& undoManager() { return undo_; }
    neptune::core::organ::OrganSynthEngine& engine() { return engine_; }
    const neptune::core::settings::GlobalSettings& settings() const { return settings_; }

private:
    static BusesProperties makeBuses();

    neptune::core::organ::OrganSynthEngine engine_;
    HostProbe probe_;
    neptune::core::UndoManager undo_;
    neptune::core::settings::GlobalSettings settings_;
    neptune::core::uistate::Size modal_[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganProcessor)
};

class OrganEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OrganEditor (OrganProcessor&);
    ~OrganEditor() override { stopTimer(); }

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    void closeModal();
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const;

    ScaledRoot root_;
    OrganProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    neptune::core::theme::Palette palette_;
    neptune::core::theme::KnobStyle knobStyle_;

    std::unique_ptr<OrganView> view_;
    juce::Label brandLabel_, infoLabel_;
    juce::TextButton undoButton_, redoButton_, panicButton_;
    std::unique_ptr<KnobModal> modal_;
    std::uint32_t lastUndoRevision_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrganEditor)
};
