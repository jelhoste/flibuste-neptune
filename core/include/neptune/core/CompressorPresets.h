#pragma once
// Presets d'usine du compresseur : de simples jeux de valeurs (identifiant -> valeur réelle) appliqués sur le jeu de paramètres du module.
// Points de départ à caler à l'écoute ; ni la latence (lookahead, suréchantillonnage), ni le sidechain, ni les modes stéréo ne sont touchés.
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::compressor
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();
// Identifiants remis à leur défaut puis écrasés par un preset (réglages « sonores » seulement).
const std::vector<ParamId>& presetParams();
} // namespace neptune::core::compressor
