#pragma once
// Écoute libre (spec §13.5) : un sample qui n'appartient à aucune zone de l'instrument — un fichier de la réserve avant d'en faire une zone, le raccord d'une boucle —
// joué par le moteur dans une zone temporaire. L'objet est immuable ; l'interface le garde vivant quelques écoutes de plus que le moteur (voir PluginProcessor).
#include <memory>
#include <string>

#include "neptune/core/Instrument.h"
#include "neptune/core/SampleData.h"

namespace neptune::core
{
struct PreviewSample
{
    Zone zone;   // zone temporaire : `sample` rempli, un coup (one-shot) de tout le sample à sa note racine, enveloppe neutre
};

// `loop` : le sample tourne en boucle sur [loopStart, loopEnd) (fin < 0 : fin du sample).
std::shared_ptr<const PreviewSample> makePreviewSample (std::shared_ptr<const SampleData> sample, int rootNote = 60, bool loop = false, std::int64_t loopStart = 0, std::int64_t loopEnd = -1);
} // namespace neptune::core
