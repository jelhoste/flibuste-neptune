#pragma once
// Import d'un instrument SFZ (fichier texte `.sfz`, format ouvert) : chaque `<region>` devient une zone du Sampler, avec ce que Neptune sait en faire :
//   - étendue de notes et de vélocités, note racine, accordage (`tune`, `transpose`), volume, panoramique, retard ;
//   - boucle (`loop_mode`, `loop_start`, `loop_end`, ou les repères du fichier), région jouée (`offset`, `end`) ;
//   - enveloppe d'amplitude (`ampeg_*`), filtre (`cutoff`, `resonance`, `fil_type`, suivi du clavier et de la vélocité), suivi de la vélocité (`amp_veltrack`) ;
//   - déclenchement (`trigger` : attack, release, first, legato), groupes de coupure (`group`, `off_by`), alternances (`seq_length` / `seq_position` en ordre,
//     `lorand` / `hirand` au hasard).
// L'héritage `<control>`, `<global>`, `<master>`, `<group>`, `<region>`, les `#define`, les `#include` et les noms de notes sont gérés.
// Ce qui n'est pas converti (commutateurs de clavier `sw_*`, contrôleurs `locc*` / `hicc*`, oscillateurs internes `*sine`, effets, modulations par contrôleur...) est
// signalé dans `warnings`, sans empêcher l'import. Cœur portable, sans JUCE. Les échantillons sont cherchés à partir du dossier du fichier (la casse est tolérée).
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Instrument.h"

namespace neptune::core::sfzimport
{
struct Options
{
    std::filesystem::path baseDir;   // dossier d'où partent les chemins d'échantillons et les `#include`
    bool readSampleFiles = true;     // lire la boucle et la note racine dans les fichiers, vérifier qu'ils existent (faux : tests sans fichiers)
};

struct Result
{
    bool ok = false;
    std::string error;
    std::string name;                // nom de l'instrument (celui du fichier)
    InstrumentDescription instrument;
    int regions = 0;                 // régions trouvées dans le fichier
    int imported = 0;                // zones créées
    std::vector<std::string> warnings;
};

// Nom de note SFZ (« c4 », « f#3 », « db-1 », « 60 ») -> numéro MIDI ; c4 = 60. -1 si le texte n'est pas une note.
int noteFromText (std::string_view text);

Result importSfz (std::string_view sfzText, const Options&);
Result importSfzFile (const std::filesystem::path& sfzFile, Options options = {});   // baseDir = dossier du fichier si non précisé
} // namespace neptune::core::sfzimport
