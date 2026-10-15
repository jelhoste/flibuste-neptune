#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "neptune/core/I18n.h"
#include "neptune/core/MidiFilter.h"
#include "neptune/core/Theme.h"

// Éditeur du filtre MIDI avancé d'une piste (spec-modules §1) : l'arbre des règles à gauche (groupes ET / OU / OU exclusif / NON ET /
// NON OU / NON, et conditions), le détail du noeud sélectionné à droite. Superposé à la fenêtre comme les autres modales.
class MidiFilterEditor : public juce::Component, private juce::Timer
{
public:
    MidiFilterEditor (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, int track,
                      std::function<neptune::core::midi::FilterDescription()> read,
                      std::function<void (const neptune::core::midi::FilterDescription&)> write, std::function<void()> onClose);
    ~MidiFilterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentSizeChanged() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void timerCallback() override;
};
