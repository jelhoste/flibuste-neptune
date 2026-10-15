#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PresetHost.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Barre de presets : précédent, nom (« * » si modifié, verrou), suivant, Enregistrer, gestionnaire. Commune à AnalogSynth et à Neptune.
class PresetBar : public juce::Component
{
public:
    PresetBar (PresetHost&, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator&);

    std::function<void (bool saveAs)> onOpenManager; // ouvre le gestionnaire (saveAs : champ du nom actif)
    std::function<void()> onLoaded;                  // un preset vient d'être chargé / enregistré : l'éditeur relit le patch
    std::function<void (const juce::String&)> onStatus;

    void refresh();                                  // relit le nom, l'état modifié
    void quickSave();                                // Ctrl+S : enregistre, ou « Enregistrer sous » si impossible
    bool needsRefresh() const;                       // le preset courant / la bibliothèque / l'état modifié ont changé

    void resized() override;

private:
    juce::String text (const char* key) const;

    PresetHost& host_;
    const neptune::core::i18n::Translator& tr_;
    juce::TextButton prev_, next_, name_, save_, manager_;
    bool lastModified_ = false;
    std::string lastId_;
    std::uint32_t lastRevision_ = 0;
};
