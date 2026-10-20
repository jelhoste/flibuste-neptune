#pragma once
// Presets d'usine de Microdécale : de simples jeux de valeurs (identifiant -> valeur réelle). « Default » est le réglage de départ de type H3000 MICROPITCHSHIFT
// (valeurs NON officielles, voir MicrodecaleModule.h) ; les deux suivants traduisent seulement la DESCRIPTION officielle des presets 232 et 247 (une voie
// micro-décalée, l'autre un retard court micro-décalé) avec des valeurs choisies ici, à régler à l'oreille.
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::microdecale
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();
int applyPreset (ParameterSet&, const FactoryPreset&);
const std::vector<ParamId>& presetParams();   // tout sauf rien : tous les réglages du module, plus le mix et la sortie
} // namespace neptune::core::microdecale
