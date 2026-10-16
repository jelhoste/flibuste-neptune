#pragma once
// Presets de l'orgue (Preset.h) : les 34 réglages de mixer::OrganKind (tirettes, percussion, vibrato / chorus, Leslie, overdrive,
// réverb) dans `parameters`. Les clés sont celles de la piste 0 ; `idForTrack` les ramène à une piste de Neptune.
// Capturé / libre (docs/modele-donnees-presets.md §9) : tirettes, percussion, vibrato, overdrive, réverb : capturés ; vitesse du
// Leslie : libre (objet physique unique et partagé). Le volume de sortie et les canaux / split / transposition des claviers
// (configuration de jeu) ne font pas partie d'un preset.
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Parameter.h"
#include "neptune/core/Preset.h"

namespace neptune::core::organ
{
constexpr const char* moduleId = "organ";
constexpr const char* leslieSpeedKey = "organ.leslie.speed";

struct KeyedParam { const char* key; ParamId id; };
const std::vector<KeyedParam>& keyedParams();
ParamId paramIdForKey (std::string_view key) noexcept;
ParamId idForTrack (ParamId track0Id, int track) noexcept;

preset::LibraryConfig defaultConfig(); // la vitesse du Leslie est libre

// Instantané du son courant. `base` (facultatif) : preset dont on garde l'identité et les métadonnées (« Enregistrer »).
preset::Preset capturePreset (const ParameterSet&, const preset::LibraryConfig& cfg, const std::string& name, const std::string& type = {},
                              const preset::Preset* base = nullptr, int track = 0);
// Écrit les valeurs capturées (une clé absente du preset remet le réglage à son défaut ; les réglages libres ne bougent pas).
void applyPresetValues (const preset::Preset&, ParameterSet&, const preset::LibraryConfig& cfg, int track = 0);
bool differsFromPreset (const preset::Preset&, const ParameterSet&, const preset::LibraryConfig& cfg, int track = 0);
// Presets livrés : des points de départ à régler à l'oreille. Ids stables « factory-organ-NN ».
std::vector<preset::Preset> factoryPresets();
} // namespace neptune::core::organ
