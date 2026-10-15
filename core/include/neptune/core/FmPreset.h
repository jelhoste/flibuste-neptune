#pragma once
// Presets du module FM (Preset.h) : le patch DX7 complet et les réglages de voix vont dans `state` (FmDescription en JSON), les
// valeurs automatisables des macros dans `parameters`. Les clés sont celles de la piste 0 ; `idForTrack` les ramène à une piste.
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/FmPatch.h"
#include "neptune/core/FmVoices.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/Preset.h"

namespace neptune::core::fm
{
constexpr const char* moduleId = "fm6op";

struct KeyedParam { const char* key; ParamId id; };
const std::vector<KeyedParam>& keyedParams();
ParamId paramIdForKey (std::string_view key) noexcept;
ParamId idForTrack (ParamId track0Id, int track) noexcept;

preset::LibraryConfig defaultConfig(); // rien n'est « libre » : une macro fait partie du son

// Instantané du son courant : macros capturées dans `parameters`, description complète dans `state`.
preset::Preset capturePreset (const ParameterSet&, const FmDescription&, const preset::LibraryConfig& cfg, const std::string& name,
                              const std::string& type = {}, const preset::Preset* base = nullptr, int track = 0);
// Un preset à partir d'un patch DX7 seul (import sysex) : réglages de voix par défaut, macros à zéro.
preset::Preset presetFromPatch (const Patch&, const std::string& type = {});
FmDescription presetDescription (const preset::Preset&, const FmDescription& current, const preset::LibraryConfig& cfg);
void applyPresetValues (const preset::Preset&, ParameterSet&, const preset::LibraryConfig& cfg, int track = 0);
bool differsFromPreset (const preset::Preset&, const ParameterSet&, const FmDescription&, const preset::LibraryConfig& cfg, int track = 0);
// Presets livrés : quelques patchs de départ de notre cru (aucune donnée Yamaha). Ids stables « factory-fm6op-NN ».
std::vector<preset::Preset> factoryPresets();
} // namespace neptune::core::fm
