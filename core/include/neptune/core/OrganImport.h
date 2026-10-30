#pragma once
// Import d'un orgue GrandOrgue (voir docs/etude-import-grandorgue.md) : lit le fichier de définition d'orgue (`.organ`, texte de type INI) et construit ce que
// Neptune sait jouer, à la manière de Neptune :
//   - chaque clavier (le pédalier est `Manual000`) devient une piste Sampler, sur son canal MIDI ;
//   - chaque jeu devient un Groupe, dont les tuyaux sont des zones (une touche, la hauteur d'origine du fichier + l'accord `PitchTuning`, la boucle et le repère de
//     relâchement lus dans le fichier) ; le jeu est tiré ou repoussé par un modulateur de la piste (un modulateur par interrupteur) ;
//   - les accouplements deviennent des règles du routeur (normal, basse, mélodie), commandées par les mêmes interrupteurs ;
//   - les généraux et les divisionnels deviennent des mémoires de combinaisons ; l'état initial des interrupteurs est rendu à part.
// Ce que l'importeur ne sait pas encore faire (tremblants, boîtes expressives, effets, panneaux) est signalé dans `warnings`, sans empêcher l'import.
// Cœur portable, sans JUCE. Les fichiers d'échantillons sont cherchés à partir du dossier du fichier `.organ` (la casse des noms est tolérée).
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neptune/core/Combinations.h"
#include "neptune/core/Facade.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/NoteRouter.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::organimport
{
// Fichier INI tolérant : sections et clefs sans distinction de casse, `;` et `#` pour les commentaires (même en fin de ligne après un espace), fins de ligne
// Windows ou Unix, texte en UTF-8 ou en Latin-1 (converti en UTF-8).
class IniFile
{
public:
    static IniFile parse (std::string_view text);

    bool hasSection (std::string_view section) const;
    bool has (std::string_view section, std::string_view key) const;
    std::string get (std::string_view section, std::string_view key, const std::string& def = {}) const;
    int getInt (std::string_view section, std::string_view key, int def) const;          // le début numérique de la valeur (le reste est ignoré)
    double getDouble (std::string_view section, std::string_view key, double def) const;
    bool getBool (std::string_view section, std::string_view key, bool def) const;       // Y / N, 1 / 0, true / false
    const std::vector<std::string>& sections() const noexcept { return order_; }          // dans l'ordre du fichier, casse d'origine

private:
    std::map<std::string, std::map<std::string, std::string>> data_;
    std::vector<std::string> order_;
};

std::string ensureUtf8 (std::string text);                  // Latin-1 -> UTF-8 si le texte n'est pas de l'UTF-8 valide
std::string numbered (std::string_view prefix, int number); // « Stop », 7 -> « Stop007 »

struct Keyboard
{
    std::string name;
    int odfIndex = 0;              // 0 : pédalier, 1.. : claviers
    int midiChannel = 1;           // 1..16
    InstrumentDescription instrument;
    int stops = 0, pipes = 0;
};

struct Options
{
    std::filesystem::path baseDir;   // dossier d'où partent les chemins d'échantillons du fichier d'orgue
    int firstTrack = 0;              // la piste de Neptune qui recevra le premier clavier (les identifiants de paramètres en dépendent)
    int firstChannel = 1;            // canal MIDI du premier manuel ; les suivants prennent les canaux suivants, le pédalier le canal d'après
    std::string stylePack;           // pack de styles d'images des pages de Façade générées (identifiant, ex. "organ-wood-brass") ; vide : dessin d'origine
    bool readSampleMarks = true;     // lire boucle et repère de relâchement dans les fichiers (faux : tests sans fichiers)
    bool streamFromDisk = false;     // vrai : les instruments gardent le début des échantillons en mémoire et lisent le reste sur le disque (gros orgues)
    std::vector<std::string> excludedPerspectives;   // perspectives (voir listPerspectives) dont les rangs ne sont pas chargés ; vide : toutes
};

struct Result
{
    bool ok = false;
    std::string error;
    std::string name;                                   // nom de l'orgue
    std::vector<Keyboard> keyboards;                    // dans l'ordre du fichier (pédalier d'abord) : le clavier i va à la piste firstTrack + i
    router::Config router;
    combi::Library combinations;
    facade::Facade facade;                              // Façade sommaire : une page par clavier (jeux et clavier), puis accouplements et combinaisons
    std::vector<std::pair<ParamId, double>> initialValues;   // état initial des interrupteurs (valeurs réelles)
    std::vector<std::string> warnings;                  // au plus 200 ; les fichiers d'échantillons introuvables n'y figurent que par les 2 premiers (voir missingSamples)
    int missingSamples = 0;                             // fichiers d'échantillons introuvables (comptés tous, avec `readSampleMarks`)
};

// Perspectives d'un orgue (positions de micro : « Front (Diffuse) », « Rear »...). Dans un fichier converti d'un format Hauptwerk, chaque perspective est un jeu de rangs
// séparé dont le nom commence par le nom de la perspective suivi de deux-points (« Rear: Ped Subbass 16 »), et un même jeu liste un rang par perspective. Un préfixe
// n'est une perspective que si au moins un jeu réunit des rangs de préfixes différents ; les autres rangs (bruits, jeux à un seul rang) sont toujours chargés.
// Rend la liste vide pour un orgue sans perspectives.
struct Perspective
{
    std::string name;
    int ranks = 0;                 // rangs qui portent ce préfixe
    std::int64_t pipes = 0;        // tuyaux logiques de ces rangs (NumberOfLogicalPipes)
};
std::vector<Perspective> listPerspectives (std::string_view odfText);
std::vector<Perspective> listPerspectivesFile (const std::filesystem::path& odfFile);

Result importOrgan (std::string_view odfText, const Options&);
Result importOrganFile (const std::filesystem::path& odfFile, Options options = {});   // baseDir = dossier du fichier si non précisé
} // namespace neptune::core::organimport
