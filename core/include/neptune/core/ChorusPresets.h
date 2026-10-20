#pragma once
// Presets d'usine du chorus : de simples jeux de valeurs (identifiant -> valeur réelle) appliqués sur le jeu de paramètres du module. Points de départ à caler
// à l'écoute. Les trois presets « TAL Chorus » utilisent le mode TAL (voir ChorusModule.h) ; les autres, le moteur standard.
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::chorus
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();

// Remet les réglages « sonores » du preset à leur défaut puis applique ses valeurs. Retourne le nombre de paramètres modifiés.
int applyPreset (ParameterSet&, const FactoryPreset&);
// Identifiants concernés par un preset : tout sauf les réglages de Settings (interpolation, départ du LFO).
const std::vector<ParamId>& presetParams();
} // namespace neptune::core::chorus
