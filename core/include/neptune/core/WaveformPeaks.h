#pragma once
// Enveloppe d'un fichier audio pour l'affichage de la forme d'onde : pour chaque case (« bin ») d'une région, la valeur minimale et maximale de
// chaque canal. Hors temps réel (lit le disque), annulable ; sert l'éditeur de sample à tout niveau de zoom (une région à la fois).
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace neptune::core
{
struct WaveformPeaks
{
    bool ok = false;
    std::string error;
    int channels = 0;
    std::int64_t totalFrames = 0;      // longueur du fichier
    std::int64_t from = 0, to = 0;     // région couverte [from, to)
    int bins = 0;
    std::vector<float> lo, hi;         // canal c, case i : lo[c * bins + i] / hi[c * bins + i], valeurs dans [-1, 1]

    float low (int channel, int bin) const noexcept { return lo[static_cast<std::size_t> (channel) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (bin)]; }
    float high (int channel, int bin) const noexcept { return hi[static_cast<std::size_t> (channel) * static_cast<std::size_t> (bins) + static_cast<std::size_t> (bin)]; }
};

// Région [from, to) (to < 0 : jusqu'à la fin) découpée en `bins` cases. `cancel` : si ce drapeau passe à vrai, la lecture s'arrête (résultat non valide).
WaveformPeaks computePeaks (const std::filesystem::path& path, std::int64_t from, std::int64_t to, int bins, const std::atomic<bool>* cancel = nullptr);
} // namespace neptune::core
