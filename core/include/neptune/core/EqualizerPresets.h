#pragma once
// Presets d'usine de l'égaliseur : jeux de bandes (type, pente, fréquence, gain, Q) appliqués sur le jeu de paramètres du module. Points de départ à
// caler à l'oreille. Un preset remet d'abord à leur défaut toutes les bandes, le gain de sortie et le gain automatique (pas l'échelle des gains, la
// phase, ni la clé externe).
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::equalizer
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();
const std::vector<ParamId>& presetParams();
} // namespace neptune::core::equalizer
