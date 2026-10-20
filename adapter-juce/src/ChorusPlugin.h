#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wunused-parameter", "-Wextra-semi", "-Wnon-virtual-dtor")
#include <clap-juce-extensions/clap-juce-extensions.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE
#include <memory>
#include "HostProbe.h"
#include "ParamHost.h"
#include "NovaChorusEditor.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Settings.h"
#include "neptune/core/StockModule.h"
#include "neptune/core/Theme.h"
#include "neptune/core/Undo.h"

// « Chœur » (nom provisoire) : le chorus de la suite (série NOVA, moteur standard et mode TAL), publié comme plugin CLAP effet indépendant. Même DSP et même
// interface que l'insert de Neptune (ChorusModule / NovaChorusEditor), autre adaptateur : ici, c'est l'hôte qui porte le plugin et fournit le tempo et le transport.
class ChorusProcessor : public juce::AudioProcessor,
                             public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                             public ParamHost
{
public:
    ChorusProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String::fromUTF8 ("Ch\xc5\x93" "ur"); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ParamHost
    neptune::core::ParameterSet& parameters() override { return runner_.params(); }
    void uiBeginEdit (neptune::core::ParamId) override;
    void uiSetNormalised (neptune::core::ParamId, double) override;
    void uiEndEdit (neptune::core::ParamId) override;
    void uiResetToDefault (neptune::core::ParamId) override;
    bool uiUndo() override;
    bool uiRedo() override;
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const override { return modal_[t == neptune::core::uistate::Terminal::Touch ? 1 : 0]; }
    void setModalSize (neptune::core::uistate::Terminal t, neptune::core::uistate::Size s) override { modal_[t == neptune::core::uistate::Terminal::Touch ? 1 : 0] = s; }

    neptune::core::ModuleRunner& runner() { return runner_; }
    const neptune::core::settings::GlobalSettings& settings() const { return settings_; }
    void applySettings (const neptune::core::settings::GlobalSettings&);   // met à jour et enregistre les réglages globaux de la suite (thème, contraste, langue)

private:
    static BusesProperties makeBuses();

    neptune::core::ModuleRunner runner_;
    HostProbe probe_;
    neptune::core::UndoManager undo_;
    neptune::core::settings::GlobalSettings settings_;
    neptune::core::uistate::Size modal_[2] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusProcessor)
};

class ChorusPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChorusPluginEditor (ChorusProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    ChorusProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    void buildContent();   // (re)crée l'interface : au démarrage et quand le thème, le contraste ou la langue changent
    std::unique_ptr<NovaChorusEditor> content_;
    juce::ComponentBoundsConstrainer constrainer_;
};
