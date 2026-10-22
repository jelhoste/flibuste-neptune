#pragma once
// Détection de la hauteur d'un sample (spec §3 : « détection automatique de la note du sample », type YIN), hors temps réel : propose la note racine et l'écart
// en cents. Fiable sur les sons harmoniques tenus ; sur les sons percussifs ou sans hauteur franche le résultat est « indéterminé » (found = faux).
#include <cstdint>
#include <filesystem>

#include "neptune/core/SampleEdit.h"

namespace neptune::core
{
struct PitchResult
{
    bool found = false;
    double hz = 0.0;
    double midi = 0.0;        // numéro MIDI fractionnaire (69 = 440 Hz)
    int note = 60;            // note la plus proche
    double cents = 0.0;       // écart de la hauteur à cette note (-50..+50) ; pour que le sample sonne juste, l'accordage de la zone est l'opposé
    double confidence = 0.0;  // 0..1
};

// Analyse la fenêtre la plus forte de [from, to) (après l'attaque). `to` < 0 : jusqu'à la fin.
PitchResult detectPitch (const sampleedit::Buffer&, std::int64_t from = 0, std::int64_t to = -1);
// Lit au plus les 3 premières secondes du fichier, puis détecte. Faux (found = false) si le fichier est illisible.
PitchResult detectPitchOfFile (const std::filesystem::path&);
} // namespace neptune::core
