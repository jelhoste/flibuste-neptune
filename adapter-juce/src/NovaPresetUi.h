#pragma once
// Menu de presets NOVA par-dessus la bibliothèque de presets existante (banques, programmes MIDI, gestionnaire, corbeille) : libellé de la pastille de
// l'en-tête, menu (gestionnaire, enregistrer, précédent / suivant, presets de la banque, tous les presets), gestionnaire superposé à l'éditeur.
// Partagé par les éditeurs NOVA d'AnalogSynth et de FM6Op.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "NovaKit.h"
#include "PresetHost.h"
#include "PresetManager.h"
#include "neptune/core/I18n.h"

namespace nova
{
class PresetUi
{
public:
    // `owner` : le composant sur lequel le gestionnaire se superpose ; `reloaded` : appelé après tout chargement de preset (relire la description du patch).
    PresetUi (juce::Component& owner, PresetHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&, std::function<void()> reloaded);
    ~PresetUi();

    juce::String label() const;
    void showMenu (juce::Component* anchor, int matrixBank);
    void quickSave();
    void openManager (bool saveAs);
    void resized();
    juce::String statusText;

private:
    juce::String tx (const char* key) const;
    juce::Component& owner_;
    PresetHost& host_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    std::function<void()> reloaded_;
    std::unique_ptr<PresetManager> manager_;
};
} // namespace nova
