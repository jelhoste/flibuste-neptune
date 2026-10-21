#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "NovaFmEditor.h"
#include "NovaSynthEditor.h"
#include "PluginProcessor.h"

// Éditeur NOVA d'une piste Synthé ou FM de Neptune, superposé à la fenêtre (comme l'éditeur d'un insert) : prend toute la place disponible,
// grossit avec la fenêtre. Un clic hors du panneau, Échap ou le bouton ferme ; il se ferme aussi si la piste sélectionnée ou son type change.
class InstrumentOverlay : public juce::Component, private juce::Timer
{
public:
    InstrumentOverlay (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, neptune::core::theme::KnobStyle, std::function<void()> onClose);
    ~InstrumentOverlay() override { stopTimer(); }

    bool valid() const { return synth_ != nullptr || fm_ != nullptr; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    juce::Colour colour (neptune::core::theme::Token t) const;

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::function<void()> onClose_;
    int track_ = 0;
    neptune::core::TrackType type_ = neptune::core::TrackType::Synth;
    std::unique_ptr<NovaSynthEditor> synth_;
    std::unique_ptr<NovaFmEditor> fm_;
    juce::TextButton closeButton_;
    juce::String title_;
    juce::Rectangle<int> panel_;
};
