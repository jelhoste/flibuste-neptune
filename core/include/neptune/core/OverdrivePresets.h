#pragma once
// Presets d'usine de Fournaise : de simples jeux de valeurs (identifiant -> valeur réelle) appliqués sur le jeu de paramètres du module. Points de départ à caler à l'écoute
// (guitare surtout, claviers type Rhodes, Wurlitzer ou orgue, basse).
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::overdrive
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();

// Remet les réglages « sonores » du preset à leur défaut puis applique ses valeurs. Retourne le nombre de paramètres modifiés.
int applyPreset (ParameterSet&, const FactoryPreset&);
// Identifiants concernés par un preset : tous les réglages sonores, le mélange ; ni le suréchantillonnage, ni les voies, ni les gains d'entrée et de sortie communs.
const std::vector<ParamId>& presetParams();
} // namespace neptune::core::overdrive
