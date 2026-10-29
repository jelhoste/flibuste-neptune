#pragma once
// Fichiers WavPack (.wv, libwavpack) : décodés en entier en mémoire, ou en partie (début résident) quand l'instrument lit le reste sur le disque (WavPackReader). Les échantillons d'orgue de GrandOrgue sont des WavPack, souvent
// nommés « .wav » : le format se reconnaît donc à la signature « wvpk » du début du fichier, jamais à l'extension.
// WavPack garde l'en-tête RIFF d'origine du WAV et ses chunks de queue (boucles `smpl`, repères `cue `) : `readWavPackMarks` les relit.
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace neptune::core
{
struct WavPackInfo
{
    bool ok = false;
    std::string error;            // en anglais ; vide si ok
    int channels = 0;
    std::int64_t frames = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;        // 8..32 ; 32 aussi pour le flottant
    bool isFloat = false;
};

struct WavPackData
{
    WavPackInfo info;
    std::vector<float> samples;          // entrelacés, normalisés (entiers / 2^(bits-1)), decodedFrames * info.channels
    std::int64_t decodedFrames = 0;      // = info.frames sauf décodage partiel (maxFrames) ; alors header seul, pas de trailer
    std::vector<std::uint8_t> header;    // enveloppe RIFF d'origine, avant les échantillons (vide si le fichier n'en garde pas) ; dans un fichier d'un seul bloc, elle contient déjà les chunks de queue
    std::vector<std::uint8_t> trailer;   // chunks d'après les échantillons (smpl, cue, ...) quand ils sont dans un autre bloc que l'en-tête
};

// Ce que les chunks RIFF d'un WAV disent du sample.
struct WavPackMarks
{
    std::vector<std::pair<std::int64_t, std::int64_t>> loops;   // début, fin INCLUSE (convention du chunk smpl), dans l'ordre du fichier
    bool hasRoot = false;
    int rootNote = 60;
    double detuneCents = 0.0;                                   // -50..+50
    std::vector<std::int64_t> cues;                             // position (en échantillons) de chaque repère `cue `, dans l'ordre du fichier
    bool empty() const noexcept { return loops.empty() && ! hasRoot && cues.empty(); }
};

// Vrai si le fichier commence par la signature d'un bloc WavPack (« wvpk »). Ne dépend pas de libwavpack.
bool isWavPackFile (const std::filesystem::path&);
// Lit l'en-tête seulement (aucun décodage). Sans libwavpack compilée (NEPTUNE_HAS_WAVPACK absent), échoue avec un message.
WavPackInfo probeWavPackFile (const std::filesystem::path&);
// Décode tout le fichier (entiers 8 à 32 bits et flottants ; mono ou stéréo seulement). `maxFrames` > 0 : ne décode que les premières frames
// (info.frames reste la longueur du fichier ; le trailer, connu seulement après le dernier échantillon, reste vide).
WavPackData decodeWavPackFile (const std::filesystem::path&, std::int64_t maxFrames = -1);

// Lecture séquentielle avec positionnement, sans tout décoder (streaming disque). Hors temps réel ; un lecteur par flux. Non copiable.
// Un positionnement décode depuis le début du bloc WavPack qui contient la frame voulue.
class WavPackReader
{
public:
    WavPackReader();
    ~WavPackReader();
    WavPackReader (const WavPackReader&) = delete;
    WavPackReader& operator= (const WavPackReader&) = delete;

    bool open (const std::filesystem::path&, std::string* error = nullptr);
    void close();
    bool isOpen() const noexcept;
    const WavPackInfo& info() const noexcept { return info_; }
    bool seek (std::int64_t frame);
    // Jusqu'à `frames` frames entrelacées normalisées ; rend le nombre lu (0 = fin ou erreur).
    std::int64_t read (float* interleaved, std::int64_t frames);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    WavPackInfo info_;
};
// Boucles, note racine et repères du fichier. Décode tout le fichier : les chunks de queue ne sont lisibles qu'après le dernier échantillon.
WavPackMarks readWavPackMarks (const std::filesystem::path&);
// Les mêmes, à partir des octets d'enveloppe (exposé pour les tests) : l'en-tête RIFF jusqu'à l'en-tête du chunk `data` inclus, puis les chunks de queue.
WavPackMarks parseRiffMarks (const std::vector<std::uint8_t>& header, const std::vector<std::uint8_t>& trailer);
bool wavPackDecodingAvailable() noexcept;
} // namespace neptune::core
