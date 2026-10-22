#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace neptune::core
{
// Fichier Opus (Ogg Opus, RFC 7845), décodé en entier en mémoire. Opus se décode toujours à 48 kHz : si le fichier a été
// encodé à une autre fréquence (champ « input sample rate » de OpusHead), c'est une information, pas la fréquence de sortie.
struct OpusFileInfo
{
    bool ok = false;
    std::string error;            // en anglais ; vide si ok
    int channels = 0;             // 1 ou 2 (mapping family 0 seulement)
    std::int64_t frames = 0;      // longueur à 48 kHz, pré-saut et fin de flux déjà retirés
    int inputRate = 0;            // fréquence d'origine annoncée par l'encodeur (0 = inconnue)
    double outputGainDb = 0.0;    // gain de sortie d'OpusHead (déjà appliqué au décodage)
};

struct OpusFileData
{
    OpusFileInfo info;
    std::vector<float> samples;   // entrelacés, info.frames * info.channels
};

constexpr double kOpusRate = 48000.0;

// Vrai si le fichier commence par une page Ogg dont le premier paquet est « OpusHead » (l'extension ne suffit pas : .ogg peut
// contenir du Vorbis).
bool isOpusFile (const std::filesystem::path&);
// Lit les en-têtes et la dernière page seulement (aucun décodage). Fonctionne même sans libopus.
OpusFileInfo probeOpusFile (const std::filesystem::path&);
// Décode tout le fichier. Sans libopus compilée (NEPTUNE_HAS_OPUS absent), échoue avec un message.
OpusFileData decodeOpusFile (const std::filesystem::path&);
bool opusDecodingAvailable() noexcept;
} // namespace neptune::core
