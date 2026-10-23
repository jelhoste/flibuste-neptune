#pragma once
// Presets d'usine de Convolution : des réglages de la voie traitée (EQ, filtres, largeur, pré-délai, mix, sortie), pas de fichier d'IR ni de retouche d'IR. Ce sont de
// simples points de départ à régler à l'oreille.
#include <string>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::convolution
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<ParamId, double>> values;   // les paramètres absents reprennent leur valeur par défaut
};
const std::vector<FactoryPreset>& factoryPresets();
int applyPreset (ParameterSet&, const FactoryPreset&);
const std::vector<ParamId>& presetParams();   // largeur, pré-délai, EQ, filtres, mix et sortie
} // namespace neptune::core::convolution
