#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include "PresetHost.h"
#include "neptune/core/FmPatch.h"
#include "neptune/core/I18n.h"

// Import / export des fichiers sysex DX7 (une voix ou un lot de 32) : commun à la piste FM de Neptune et au plugin FM.
//  - import : une voix remplace le patch courant (`applySingle`) ; un lot devient 32 presets dans une banque neuve de la bibliothèque ;
//  - export d'une voix : fichier de 163 octets ; export du lot : les presets FM de la banque du preset courant (au plus 32).
class FmSysexIO
{
public:
    FmSysexIO (juce::Component& owner, PresetHost&, const neptune::core::i18n::Translator&);

    void importFile (std::function<void (const neptune::core::fm::Patch&)> applySingle);
    void exportVoice (const neptune::core::fm::Patch&);
    void exportBank();

private:
    juce::String text (const char* key) const;
    void notify (const juce::String& title, const juce::String& message);
    void importBank (const std::vector<neptune::core::fm::Patch>&, const juce::String& name);

    juce::Component& owner_;
    PresetHost& host_;
    const neptune::core::i18n::Translator& tr_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::Component::SafePointer<juce::Component> safeOwner_;
};
