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
struct SampleData
{
    int channels = 0;           // 1 ou 2
    std::int64_t frames = 0;
    double sampleRate = 0.0;    // fréquence d'échantillonnage du fichier
    int sourceBitDepth = 0;     // information (0 = inconnu)
    std::string name;           // nom de fichier, pour l'affichage

    std::vector<std::int16_t> pcm16; // non vide si le sample est gardé en 16 bits
    std::vector<float> pcm32;        // sinon

    bool isInt16() const noexcept { return ! pcm16.empty(); }
    const std::int16_t* int16Channel (int c) const noexcept { return pcm16.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (frames); }
    const float* floatChannel (int c) const noexcept { return pcm32.data() + static_cast<std::size_t> (c) * static_cast<std::size_t> (frames); }
    std::size_t bytes() const noexcept { return pcm16.size() * sizeof (std::int16_t) + pcm32.size() * sizeof (float); }
    double durationSeconds() const noexcept { return sampleRate > 0.0 ? static_cast<double> (frames) / sampleRate : 0.0; }

    // Valeur normalisée [-1, 1[ ; 0 hors des bornes (utilisé par l'interpolation aux extrémités).
    float at (int channel, std::int64_t frame) const noexcept
    {
        if (frame < 0 || frame >= frames) return 0.0f;
        return isInt16() ? static_cast<float> (int16Channel (channel)[frame]) * (1.0f / 32768.0f) : floatChannel (channel)[frame];
    }
};
} // namespace neptune::core
