#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Réglage avancé du choke des zones sélectionnées : « coupe » (le groupe que le déclenchement de la zone coupe) et « est
// coupée par » (le groupe qui peut couper la zone), indépendants. Le réglage simple (un seul numéro, symétrique) est dans la barre.
class ChokePanel : public juce::Component
{
public:
    ChokePanel (const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, int cuts, int cutBy);

    std::function<void (int cuts, int cutBy)> onChange;
    std::function<void()> onUndo, onRedo;

    void setValues (int cuts, int cutBy); // après annuler/rétablir
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    juce::Colour colour (neptune::core::theme::Token t) const;
    juce::String text (const char* key) const;

    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    juce::Label cutsLabel_, cutByLabel_, hint_;
    juce::ComboBox cuts_, cutBy_;
    bool updating_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChokePanel)
};
