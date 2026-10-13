#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
#include "PluginProcessor.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Theme.h"

// Vue Mixeur : les pistes actives (jusqu'à 16, bouton « + Piste »), 16 bus et le Master (palier 1, jalon 9). En haut de chaque piste :
// le générateur de son (Sampler / Synthé) et le retrait de la piste. Chaque bande : nom, canal MIDI (pistes), routage et envois
// (fenêtre), pan, fader avec vu-mètre, mute, solo (Maj+clic : solo exclusif). Cliquer le nom sélectionne la piste : la vue Mapping
// édite alors son instrument. Les valeurs automatisables sont des paramètres (fader, pan, mute, solo, niveaux d'envoi) :
// les curseurs d'ici passent par les mêmes gestes que les potards (annulation et automation de l'hôte).
class MixerView : public juce::Component, private juce::Timer
{
public:
    MixerView (NeptuneProcessor&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);
    ~MixerView() override;

    std::function<void (int track)> onSelectTrack;

    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh(); // noms, canaux, routage, sélection : à rappeler quand l'état du mixeur change

private:
    class Strip;
    class MasterStrip;
    void timerCallback() override;
    void exclusiveSolo (bool isBus, int index);
    void requestTypeChange (int track, neptune::core::TrackType); // confirmation si la piste a du contenu
    void requestRemove (int track);
    void layoutTracks();
    juce::Colour colour (neptune::core::theme::Token t) const;

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::array<std::unique_ptr<Strip>, neptune::core::mixer::numTracks> tracks_;
    std::array<std::unique_ptr<Strip>, neptune::core::mixer::numBuses> buses_;
    std::unique_ptr<MasterStrip> master_;
    juce::Label tracksTitle_, busesTitle_;
    juce::TextButton addTrack_;
    std::array<bool, neptune::core::mixer::numTracks> shownTracks_ {}; // pistes actives telles qu'affichées
    std::uint32_t lastRevision_ = 0xFFFFFFFFu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
