#pragma once
// Packs de styles d'images de la Façade (`.neptunestyle`, une archive zip, ou un dossier de même structure).
//
// Convention de contenu (voir docs/specification-styles-facade.md) :
//   pack.json            facultatif : nom, auteur, licence, version, description du pack
//   <dossier>/           un dossier par style (les images à la racine forment un seul style)
//     style.json         facultatif : nom, nombre d'images de la bande, sens
//     off.svg on.svg     boutons et actions : éteint / fermé et allumé / ouvert
//     track.svg handle.svg  curseurs : rail et poignée
//     strip_32.svg       potards : bande de 32 images, empilées ; strip_32_h.svg : côte à côte
// Les formats sont SVG, PNG et JPG. Aucun code n'est exécuté : ce module ne fait que reconnaître des noms de fichiers.
// Aucune dépendance à JUCE ; la lecture de l'archive ou du dossier est faite par l'adaptateur, qui passe ici la liste des entrées.
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::stylepack
{
constexpr int formatVersion = 1;
constexpr std::size_t maxStylesPerPack = 64;

// Un style trouvé dans un pack. Les chemins sont ceux des entrées dans le pack (relatifs à sa racine, séparés par « / ») ; vide : absent.
struct PackStyle
{
    std::string folder;   // dossier du style dans le pack ("" : racine)
    std::string name;
    std::string off, on, track, handle, strip;
    int frames = 0;
    bool stripHorizontal = false;
};

struct PackInfo
{
    std::string id;          // identifiant : nom du fichier du pack sans extension, en minuscules
    std::string name, author, licence, version, description;
    std::vector<PackStyle> styles;
    std::vector<std::string> warnings;   // problèmes non bloquants, en anglais (pour le journal ou l'interface)
};

enum class Role { None, Off, On, Track, Handle, Strip };

struct ImageName
{
    Role role = Role::None;
    int frames = 0;              // Strip : nombre d'images lu dans le nom (strip_32), 0 si absent
    bool horizontal = false;     // Strip : images côte à côte (strip_32_h)
};

// Reconnaît le nom d'un fichier image (sans dossier) : rôle, bande. Extensions acceptées : svg, png, jpg, jpeg (casse ignorée).
// Synonymes acceptés : closed / released -> off ; open / pressed -> on ; rail / groove -> track ; thumb -> handle ; filmstrip -> strip.
ImageName parseImageName (std::string_view fileName);

// Vrai si le chemin d'une entrée est sûr : relatif, sans « .. », sans barre inverse ni lettre de lecteur.
bool isSafeEntryName (std::string_view);

// Analyse un pack à partir de la liste de ses entrées (chemins) ; `readText` rend le contenu d'une entrée texte (pack.json, style.json) ou "".
PackInfo analyse (std::string_view fileStem, const std::vector<std::string>& entries, const std::function<std::string (const std::string&)>& readText);

// Chemins « pack: » stockés dans les styles de la Façade : pack:<identifiant>!<entrée>.
std::string makeReference (std::string_view packId, std::string_view entry);
bool parseReference (std::string_view reference, std::string& packId, std::string& entry);
} // namespace neptune::core::stylepack
