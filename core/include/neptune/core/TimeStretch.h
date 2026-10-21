#pragma once
// Étirement temporel et décalage de hauteur indépendants (spec §13.5 / §21), HORS temps réel : vocodeur de phase à verrouillage de phase par pics
// (Laroche-Dolson), fenêtre de 4096 points. Le décalage de hauteur est un étirement d'un rapport (durée x hauteur) suivi d'un ré-échantillonnage par
// sinus cardinal fenêtré : une seule machine, deux rapports indépendants. Pas de contrainte de latence, donc fenêtre large et bonne qualité.
#include <atomic>

#include "neptune/core/SampleEdit.h"

namespace neptune::core::sampleedit
{
// `timeRatio` : durée de sortie / durée d'entrée (0,25 à 4). `semitones` : décalage de hauteur (-24 à +24, fractions admises). Les deux à
// leur valeur neutre (1 et 0) rendent une copie. `cancel` : si le drapeau passe à vrai, le calcul s'arrête et rend un tampon vide.
// La durée obtenue est celle demandée à ~0,1 % près (le pas du vocodeur est entier).
Buffer stretchAndShift (const Buffer& in, double timeRatio, double semitones, const std::atomic<bool>* cancel = nullptr);
} // namespace neptune::core::sampleedit
