#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "NovaFmEditor.h"
#include "NovaSynthEditor.h"
#include "PluginProcessor.h"

// Éditeur NOVA de la piste Synthé ou FM sélectionnée, intégré à la page Edit de Neptune (sous la barre de menu). Il est lié à une piste et à un type : l'éditeur de
// Neptune le reconstruit quand la sélection change. L'échelle suit la place disponible (1 au maximum de la conception, jusqu'à 2).
class InstrumentPanel : public juce::Component
{
public:
    InstrumentPanel (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, neptune::core::theme::KnobStyle);

    bool valid() const { return synth_ != nullptr || fm_ != nullptr; }
    bool matches (int track, neptune::core::TrackType type) const noexcept { return track == track_ && type == type_; }
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    int track_ = 0;
    neptune::core::TrackType type_ = neptune::core::TrackType::Synth;
    std::unique_ptr<NovaSynthEditor> synth_;
    std::unique_ptr<NovaFmEditor> fm_;
};
