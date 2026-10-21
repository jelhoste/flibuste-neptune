#pragma once
// Empreinte de volume d'un fichier (spec §23) : contour continu à basse résolution (un point tous les ~10 ms) du niveau en dB, en trois courbes
// parallèles — Pic, RMS (non pondéré, fenêtre de 20 ms) et LUFS (pondération K, EBU R128, fenêtre de 100 ms, par libebur128 ; absent si la bibliothèque
// n'est pas liée). Calculée hors temps réel, une fois par fichier ; l'affichage et la navigation lisent ce contour sans relire le fichier.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace neptune::core
{
struct VolumeFootprint
{
    bool ok = false;
    std::string error;
    double sampleRate = 0.0;
    std::int64_t totalFrames = 0;
    std::int64_t hopFrames = 0;               // échantillons entre deux points
    std::vector<float> peakDb, rmsDb, lufs;   // une valeur par point, en dB (plancher -120) ; `lufs` vide si indisponible
    bool hasLufs() const noexcept { return ! lufs.empty(); }
    const std::vector<float>& curve (int kind) const noexcept { return kind == 0 ? peakDb : kind == 1 ? rmsDb : lufs; }   // 0 = Pic, 1 = RMS, 2 = LUFS
    std::int64_t frameOfPoint (std::size_t i) const noexcept { return static_cast<std::int64_t> (i) * hopFrames; }
    std::size_t pointOfFrame (std::int64_t frame) const noexcept
    {
        if (hopFrames <= 0 || peakDb.empty()) return 0;
        const auto i = frame < 0 ? 0 : frame / hopFrames;
        return static_cast<std::size_t> (i) >= peakDb.size() ? peakDb.size() - 1 : static_cast<std::size_t> (i);
    }
};

VolumeFootprint computeFootprint (const std::filesystem::path&, const std::atomic<bool>* cancel = nullptr);

// ---- navigation dans une courbe (indices de points)
// Point le plus fort dans [from, to) ; -1 si la plage est vide.
int loudestPoint (const std::vector<float>& curve, int from, int to) noexcept;
// Maxima locaux marqués : au moins `prominenceDb` au-dessus des creux qui les séparent d'un maximum plus haut (dans les 3 s), triés par position.
std::vector<int> localPeaks (const std::vector<float>& curve, double prominenceDb, int hopsPerSecond);
// Pic suivant (direction > 0) ou précédent (< 0) après / avant le point `from` ; -1 s'il n'y en a pas.
int neighbourPeak (const std::vector<int>& peaks, int from, int direction) noexcept;
} // namespace neptune::core
