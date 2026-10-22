#pragma once
// Métadonnées Neptune embarquées dans un fichier sample (spec §23) : un petit document JSON (`schema_version`, `volume_marker_ms`...) dans un chunk personnalisé,
// « nptn » (RIFF, WAV) ou « NPTN » (IFF, AIFF), ajouté à la fin du fichier : les autres chunks (smpl, cue, bext...) et les échantillons sont recopiés tels quels,
// le fichier reste un WAV / AIFF standard lisible partout. Les formats sans chunk simple à écrire (FLAC...) et les fichiers qu'on ne peut pas réécrire (verrouillés,
// en lecture seule) reçoivent un fichier voisin `<nom>.neptune.json` du même contenu. La lecture cherche d'abord le chunk, puis le fichier voisin.
#include <cstdint>
#include <filesystem>
#include <string>

namespace neptune::core
{
struct SampleMeta
{
    bool hasVolumeMarker = false;
    double volumeMarkerMs = 0.0;   // instant du marqueur de volume, en ms depuis le début du fichier
    bool operator== (const SampleMeta& o) const noexcept { return hasVolumeMarker == o.hasVolumeMarker && (! hasVolumeMarker || volumeMarkerMs == o.volumeMarkerMs); }
};

enum class MetaStore { None, Chunk, Sidecar };

// Lit les métadonnées (chunk puis fichier voisin). Faux si le fichier n'en porte pas.
bool readSampleMeta (const std::filesystem::path&, SampleMeta& out, MetaStore* where = nullptr);
// Écrit (ou remplace) les métadonnées ; si `meta` est vide, les retire. Rend l'endroit où elles sont maintenant (None : rien à écrire ni à retirer), ou
// faux avec `error` si rien n'a pu être écrit. `outStore` dit si c'est le fichier lui-même ou le fichier voisin.
bool writeSampleMeta (const std::filesystem::path&, const SampleMeta& meta, MetaStore* outStore = nullptr, std::string* error = nullptr);

// Document JSON <-> SampleMeta (exposés pour les tests).
std::string sampleMetaToJson (const SampleMeta&);
bool sampleMetaFromJson (const std::string& text, SampleMeta& out);
} // namespace neptune::core
