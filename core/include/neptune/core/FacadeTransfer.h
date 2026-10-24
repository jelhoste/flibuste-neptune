#pragma once
// Façade portable (fichier `.neptunefacade`, zip fait par l'adaptateur) : les transformations pures qui la rendent transportable.
//  - remapTrack : une Façade faite sur la piste A se charge sur la piste B ; les widgets se rebranchent sur les mêmes réglages de B.
//  - forEachImagePath : tous les chemins d'images (fond et styles), pour les copier dans le fichier ou les réécrire au chargement.
// Cœur portable, sans JUCE.
#include <functional>
#include <string>

#include "neptune/core/Facade.h"

namespace neptune::core::facade
{
// Identifiant du même réglage sur la piste `toTrack` (0..15) : paramètres de la piste (volume, envois, synthé, FM, orgue, emplacements
// et modulateurs du Sampler, réglages de l'instrument). Ce qui n'appartient à aucune piste (master, bus, pistes parallèles, macros)
// est rendu tel quel, comme tout identifiant inconnu.
ParamId remapParamTrack (ParamId id, int fromTrack, int toTrack) noexcept;

// Rebranche sur `toTrack` les paramètres de tous les widgets (lien, instantané des boutons Action).
void remapTrack (Facade&, int fromTrack, int toTrack);

// Appelle `visit` sur chaque chemin d'image non vide : le fond puis les images de chaque style.
void forEachImagePath (Facade&, const std::function<void (std::string&)>& visit);
} // namespace neptune::core::facade
