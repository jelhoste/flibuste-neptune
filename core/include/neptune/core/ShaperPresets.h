#pragma once
// Presets d'usine du shaper : de simples jeux de valeurs (identifiant -> valeur réelle) appliqués sur le jeu de paramètres du module.
// Points de départ à caler à l'écoute ; les réglages d'interface et de latence (lookahead, oversampling, détecteurs) ne sont jamais touchés.
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::shaper
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();

// Remet les réglages « sonores » du preset à leur défaut puis applique ses valeurs. Retourne le nombre de paramètres modifiés.
int applyPreset (ParameterSet&, const FactoryPreset&);
// Identifiants concernés par un preset (jeux 1 et 2, niveau, temps, plage, lien, sortie, sidechain) : ni latence ni réglages d'affichage.
const std::vector<ParamId>& presetParams();
} // namespace neptune::core::shaper
