#pragma once
// Métadonnées Neptune embarquées dans un fichier sample (spec §23) : un petit document JSON (`schema_version`, `volume_marker_ms`...) dans un chunk personnalisé,
// « nptn » (RIFF, WAV) ou « NPTN » (IFF, AIFF), ajouté à la fin du fichier : les autres chunks (smpl, cue, bext...) et les échantillons sont recopiés tels quels,
// le fichier reste un WAV / AIFF standard lisible partout. Les formats sans chunk simple à écrire (FLAC...) et les fichiers qu'on ne peut pas réécrire (verrouillés,
// en lecture seule) reçoivent un fichier voisin `<nom>.neptune.json` du même contenu. La lecture cherche d'abord le chunk, puis le fichier voisin.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace neptune::core
{
// Boucle d'un sample : positions en échantillons du fichier, `end` exclusif (< 0 : fin du fichier).
struct MetaLoop
{
    std::int64_t start = 0, end = -1, crossfade = 0;
    int repeats = 0;   // retours avant de passer à la boucle suivante (0 : sans fin)
    bool operator== (const MetaLoop& o) const noexcept { return start == o.start && end == o.end && crossfade == o.crossfade && repeats == o.repeats; }
};

// Ce qui appartient au sample lui-même (et le suit d'une zone à l'autre) : ses boucles, sa région de lecture, sa note racine, son marqueur de volume.
struct SampleMeta
{
    bool hasVolumeMarker = false;
    double volumeMarkerMs = 0.0;       // instant du marqueur de volume, en ms depuis le début du fichier
    std::vector<MetaLoop> loops;       // jusqu'à 4, dans l'ordre du fichier
    bool hasRegion = false;
    std::int64_t startFrame = 0, endFrame = -1;   // région jouée (END < 0 : fin du fichier)
    bool hasRoot = false;
    int rootNote = 60;
    double tuneCents = 0.0;
    double sampleRate = 0.0;           // fréquence du fichier à l'écriture : les positions en échantillons sont relatives à elle
    bool isEmpty() const noexcept { return ! hasVolumeMarker && loops.empty() && ! hasRegion && ! hasRoot; }
    bool operator== (const SampleMeta& o) const noexcept
    {
        return hasVolumeMarker == o.hasVolumeMarker && (! hasVolumeMarker || volumeMarkerMs == o.volumeMarkerMs) && loops == o.loops
            && hasRegion == o.hasRegion && (! hasRegion || (startFrame == o.startFrame && endFrame == o.endFrame))
            && hasRoot == o.hasRoot && (! hasRoot || (rootNote == o.rootNote && tuneCents == o.tuneCents));
    }
};

enum class MetaStore { None, Chunk, Sidecar };

// Lit les métadonnées (chunk puis fichier voisin). Faux si le fichier n'en porte pas.
bool readSampleMeta (const std::filesystem::path&, SampleMeta& out, MetaStore* where = nullptr);
// Écrit (ou remplace) les métadonnées ; si `meta` est vide, les retire. Rend l'endroit où elles sont maintenant (None : rien à écrire ni à retirer), ou
// faux avec `error` si rien n'a pu être écrit. `outStore` dit si c'est le fichier lui-même ou le fichier voisin.
bool writeSampleMeta (const std::filesystem::path&, const SampleMeta& meta, MetaStore* outStore = nullptr, std::string* error = nullptr);

// Tout ce que le fichier dit de lui-même : les métadonnées Neptune (chunk ou fichier voisin), complétées par les boucles et la note racine d'un chunk d'instrument
// standard (`smpl` d'un WAV, instrument d'un AIFF) pour ce que Neptune n'a pas écrit. Les positions rendues sont en échantillons du fichier, fin de boucle exclusive.
// Faux si le fichier ne porte rien d'utile.
bool readFileMarks (const std::filesystem::path&, SampleMeta& out);

// Repère de relâchement enregistré dans le fichier (orgues : début de la queue du tuyau) : la position la plus grande des repères `cue ` d'un WAV, ou de l'enveloppe
// RIFF d'un WavPack. Faux si le fichier n'en porte pas. Position en échantillons du fichier.
bool readReleaseMarker (const std::filesystem::path&, std::int64_t& frame);

// Document JSON <-> SampleMeta (exposés pour les tests).
std::string sampleMetaToJson (const SampleMeta&);
bool sampleMetaFromJson (const std::string& text, SampleMeta& out);
} // namespace neptune::core
