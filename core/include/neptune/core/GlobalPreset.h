#pragma once
// Preset global de Neptune (fichier `.neptuneset`, zip fait par l'adaptateur) : tout le mixeur (pistes avec instruments, inserts, modulation
// et Façades, pistes parallèles, bus, Master) et les valeurs des réglages. C'est l'état du projet, sans ce qui appartient à l'instance ou à
// la machine : identifiant d'instance, verrou de profil, couche de contrôle (assignations CC / OSC).
//   state.json   l'état (format de `state::save`), chemins de samples tels quels, images de Façade désignées par « asset:assets/... »
//   assets/ packs/   images et packs de styles des Façades (même disposition que `.neptunefacade`)
// Cœur portable, sans JUCE.
#include <functional>
#include <string>
#include <string_view>

#include "neptune/core/Facade.h"
#include "neptune/core/MixerState.h"

namespace neptune::core::globalpreset
{
// Retire de l'état ce qui n'appartient pas à un preset : identifiant d'instance, verrou de profil, couche de contrôle.
std::string withoutInstanceData (std::string_view stateText);

// Applique `edit` au mixeur de l'état (relu puis réécrit par la sérialisation du mixeur). Un texte sans mixeur est rendu tel quel.
std::string editMixer (std::string_view stateText, const std::function<void (MixerState&)>& edit);

// Les Façades des pistes (les seules à porter des images).
void forEachFacade (MixerState&, const std::function<void (facade::Facade&)>& visit);
} // namespace neptune::core::globalpreset
