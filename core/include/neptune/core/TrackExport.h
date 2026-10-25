#pragma once
// Export d'une piste complète (fichier `.neptunetrack`, zip fait par l'adaptateur) : tout ce qui fait l'instrument et sa bande
// (type, instrument ou patch, inserts, modulation, Façade, courbe de vélocité, valeurs des réglages de la piste), de façon à le
// reposer sur une autre piste, un autre projet ou une autre machine. Cœur portable, sans JUCE.
//   track.json   {"format":1,"track":n,"state":{...piste...},"params":{"<id>":valeur,...}}
//   samples/...  fichiers audio de l'instrument et IR de convolution, désignés dans track.json par « asset:samples/... »
//   assets/ packs/   images et packs de styles de la Façade (même disposition que `.neptunefacade`)
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/MixerState.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::trackexport
{
struct Bundle
{
    int track = 0;                          // piste d'origine
    TrackState state;
    std::map<ParamId, double> params;       // valeurs des réglages de la piste d'origine (identifiants de cette piste)
};

std::string toJson (const Bundle&);
// Rend rien si le texte n'est pas un export de piste lisible ; sinon lecture tolérante (bornes appliquées).
std::optional<Bundle> fromJson (std::string_view);

// Identifiants des réglages qui appartiennent à la piste (bande, synthé, FM, orgue, Sampler, emplacements, modulateurs).
std::vector<ParamId> ownedParams (const ParameterSet&, int track);

// Reporte l'export sur la piste `toTrack` : identifiants des réglages, destinations de la matrice de modulation, liens de la Façade.
// Le canal MIDI de la piste n'est pas touché ici (l'appelant garde celui de la piste cible).
void remapTo (Bundle&, int toTrack);

// Chaque chemin de fichier de l'instrument (zones du Sampler) et des inserts (IR de la convolution) : pour les copier dans l'archive
// ou les réécrire à la lecture.
void forEachFilePath (TrackState&, const std::function<void (std::string&)>& visit);
} // namespace neptune::core::trackexport
