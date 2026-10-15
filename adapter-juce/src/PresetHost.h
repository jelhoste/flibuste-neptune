#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "neptune/core/Preset.h"
#include "neptune/core/PresetLibrary.h"

// Ce que le gestionnaire de presets et la barre de presets attendent de leur hôte : AnalogSynth (un synthé) et Neptune (le synthé
// de la piste sélectionnée). Thread de message uniquement.
class PresetHost
{
public:
    virtual ~PresetHost() = default;

    virtual neptune::core::preset::Library& library() = 0;
    virtual std::string currentPresetId() const = 0;
    virtual const neptune::core::preset::Preset* currentPreset() const = 0;
    virtual std::uint32_t presetRevision() const = 0;      // change quand la bibliothèque ou le preset courant change
    virtual void touchPresets() = 0;

    virtual bool loadPreset (const std::string& id) = 0;   // faux si ce preset ne peut pas être appliqué ici
    virtual bool stepPreset (int direction) = 0;           // suivant (+1) / précédent (-1)
    virtual bool isModified() const = 0;                   // le son courant diffère du preset chargé
    virtual bool canSaveCurrent() const = 0;               // preset utilisateur, non verrouillé
    virtual bool saveCurrent (std::string* error = nullptr) = 0;
    virtual bool overwritePreset (const std::string& id, std::string* error = nullptr) = 0;
    virtual bool saveCurrentAs (const std::string& name, const std::string& type, neptune::core::preset::Preset* out = nullptr,
                                std::string* error = nullptr) = 0;
    virtual bool saveConfig (const neptune::core::preset::LibraryConfig&) = 0;
    virtual const neptune::core::preset::LibraryConfig& presetConfig() const = 0;
    virtual bool presetsAvailable() const { return true; } // faux : pas de synthé à régler (ex. piste Sampler de Neptune)
    // Ce preset peut-il être appliqué ici ? (les presets d'AnalogSynth et du FM partagent la même bibliothèque : chacun ne montre
    // et ne charge que les siens.)
    virtual bool acceptsPreset (const neptune::core::preset::Preset&) const { return true; }
    // Clés des paramètres que ce module capture (onglet Réglages du gestionnaire).
    virtual std::vector<std::string> presetKeys() const { return {}; }
};
