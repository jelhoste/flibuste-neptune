#pragma once
// Données audio d'un sample en mémoire (palier 1) : immuables après création, partagées entre le thread qui les charge
// et le thread audio par std::shared_ptr<const SampleData>.
//
// Mémoire (décision de l'utilisateur, 2026-10-12) : les fichiers 16 bits restent en int16 (moitié de RAM), tout le reste
// est converti en float32. Disposition « planaire » : le canal c commence à c * frames.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neptune::core
{
// Longueur maximale (octets UTF-8) d'un chemin de fichier lu en streaming ; au-delà, le fichier est chargé en entier.
constexpr std::size_t maxStreamPathBytes = 1024;

struct SampleData
{
    int channels = 0;           // 1 ou 2
    std::int64_t frames = 0;    // longueur TOTALE du fichier (en streaming, seul le début est en mémoire : voir residentFrames)
    double sampleRate = 0.0;    // fréquence d'échantillonnage du fichier
    int sourceBitDepth = 0;     // information (0 = inconnu)
    std::string name;           // nom de fichier, pour l'affichage

    // Streaming disque : seules les `residentFrames` premières frames sont en mémoire (préchargement) ; le reste est lu
    // dans le fichier `path` par DiskStreamer. -1 = tout est résident (cas normal).
    std::int64_t residentFrames = -1;
    std::string path;                // chemin UTF-8 du fichier (rempli par le chargeur)

    std::vector<std::int16_t> pcm16; // non vide si le sample est gardé en 16 bits
    std::vector<float> pcm32;        // sinon

    std::int64_t resident() const noexcept { return residentFrames < 0 || residentFrames > frames ? frames : residentFrames; }
    bool isStreamed() const noexcept { return resident() < frames; }

    bool isInt16() const noexcept { return ! pcm16.empty(); }
    // Disposition planaire sur les frames résidentes : le canal c commence à c * resident().
    const std::int16_t* int16Channel (int c) const noexcept { return pcm16.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (resident()); }
    const float* floatChannel (int c) const noexcept { return pcm32.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (resident()); }
    std::size_t bytes() const noexcept { return pcm16.size() * sizeof (std::int16_t) + pcm32.size() * sizeof (float); }
    double durationSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }

    // Valeur normalisée [-1, 1[ ; 0 hors des bornes (utilisé par l'interpolation aux extrémités). Ne couvre que la
    // partie résidente : au-delà, un échantillon en streaming vient du tampon de la voix (SamplerVoices).
    float at (int channel, std::int64_t frame) const noexcept
    {
        if (frame < 0 || frame >= resident()) return 0.0f;
        return isInt16() ? static_cast<float> (int16Channel (channel)[frame]) * (1.0f / 32768.0f) : floatChannel (channel)[frame];
    }
};
} // namespace neptune::core
