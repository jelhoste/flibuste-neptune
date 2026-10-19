#pragma once
// Tampon circulaire sans verrou (un producteur audio, un lecteur d'interface) qui transporte du signal du fil audio vers l'interface, avec
// les mesures de niveau de l'analyseur d'amplitude. Méthode reprise de ZL Compressor de zsliu98 (https://github.com/ZL-Audio/ZLCompressor,
// AGPLv3 comme ce projet) : le DSP ne fait que COPIER des flux audio (entrée, sortie compressée, sortie finale, clé) ; l'interface en tire,
// à intervalles réguliers, un niveau par flux (crête ou RMS en dB, avec le type de canaux choisi) et déduit la réduction de gain en comparant
// le niveau compressé au niveau d'entrée. Réécrit pour ce projet (sans Highway, sans verrou tournant).
// Copyright (C) 2025-2026 zsliu98 pour la méthode d'origine ; adaptation pour ce projet.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neptune::core
{
enum class MagType { Peak = 0, RMS = 1 };
enum class StereoType { MidSide = 0, LeftRight = 1, Left = 2, Right = 3, Mid = 4, Side = 5 };

class AnalyzerRing
{
public:
    // 32768 échantillons par canal (0,68 s à 48 kHz). Les données lues doivent avoir moins de la moitié de la capacité : l'écrivain ne peut alors
    // pas les avoir écrasées pendant la lecture.
    static constexpr std::size_t kCapacity = 32768;
    static constexpr std::size_t kMask = kCapacity - 1;

    AnalyzerRing (int streams, int channels) : streams_ (streams), channels_ (channels), data_ (static_cast<std::size_t> (streams * channels) * kCapacity, 0.0f) {}

    int streams() const noexcept { return streams_; }
    int channels() const noexcept { return channels_; }

    // ---- réglages (thread de préparation) ; actif : seul un éditeur ouvert fait remplir le tampon
    void setSampleRate (double fs) noexcept { sampleRate_.store (fs, std::memory_order_relaxed); }
    double sampleRate() const noexcept { return sampleRate_.load (std::memory_order_relaxed); }
    void setMaxBlock (int n) noexcept { maxBlock_.store (n, std::memory_order_relaxed); }
    int maxBlock() const noexcept { return maxBlock_.load (std::memory_order_relaxed); }
    void setActive (bool a) noexcept { active_.store (a, std::memory_order_relaxed); }
    bool active() const noexcept { return active_.load (std::memory_order_relaxed); }

    // ---- fil audio : `in` désigne streams x channels tampons de `n` échantillons (flux par flux, canal par canal)
    void push (const float* const* in, int n) noexcept
    {
        if (n <= 0) return;
        const auto w = write_.load (std::memory_order_relaxed);
        for (int s = 0; s < streams_ * channels_; ++s)
        {
            float* dst = data_.data() + static_cast<std::size_t> (s) * kCapacity;
            for (int i = 0; i < n; ++i) dst[(w + static_cast<std::uint64_t> (i)) & kMask] = in[s][i];
        }
        write_.store (w + static_cast<std::uint64_t> (n), std::memory_order_release);
    }

    // ---- interface
    std::uint64_t writePos() const noexcept { return write_.load (std::memory_order_acquire); }
    float sample (int stream, int channel, std::uint64_t pos) const noexcept
    {
        return data_[static_cast<std::size_t> (stream * channels_ + channel) * kCapacity + static_cast<std::size_t> (pos & kMask)];
    }

private:
    int streams_, channels_;
    std::vector<float> data_;
    std::atomic<std::uint64_t> write_ { 0 };
    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<int> maxBlock_ { 512 };
    std::atomic<bool> active_ { false };
};

// Niveau (dB) d'un flux sur les `n` échantillons à partir de `start`, comme MagReceiver::calculate de ZL. Retourne 0 si n = 0.
inline float analyzerLevelDb (const AnalyzerRing& ring, int stream, std::uint64_t start, int n, MagType mag, StereoType stereo) noexcept
{
    if (n <= 0) return 0.0f;
    auto peakDb = [] (double peak) { return static_cast<float> (20.0 * std::log10 (std::max (peak, 1.0e-12))); };
    auto sqrDb = [] (double meanSquare) { return static_cast<float> (10.0 * std::log10 (std::max (meanSquare, 1.0e-24))); };
    const bool two = ring.channels() == 2;
    double peak = 0.0, sum = 0.0;
    if (! two || stereo == StereoType::LeftRight || (stereo == StereoType::MidSide && mag == MagType::RMS))
    {
        // L+R : crête = la plus forte des voies, RMS = somme des énergies ; M+S en RMS : la même somme, multipliée par 0,5
        for (int c = 0; c < ring.channels(); ++c)
            for (int i = 0; i < n; ++i)
            {
                const double x = ring.sample (stream, c, start + static_cast<std::uint64_t> (i));
                peak = std::max (peak, std::fabs (x));
                sum += x * x / n;
            }
        if (two && stereo == StereoType::MidSide) sum *= 0.5;
        return mag == MagType::RMS ? sqrDb (sum) : peakDb (peak);
    }
    for (int i = 0; i < n; ++i)
    {
        const auto p = start + static_cast<std::uint64_t> (i);
        const double l = ring.sample (stream, 0, p), r = ring.sample (stream, 1, p);
        const double mid = 0.5 * (l + r), side = mid - r;
        switch (stereo)
        {
            case StereoType::Left: peak = std::max (peak, std::fabs (l)); sum += l * l; break;
            case StereoType::Right: peak = std::max (peak, std::fabs (r)); sum += r * r; break;
            case StereoType::Mid: peak = std::max (peak, std::fabs (mid)); sum += mid * mid; break;
            case StereoType::Side: peak = std::max (peak, std::fabs (side)); sum += side * side; break;
            case StereoType::MidSide: peak = std::max ({ peak, std::fabs (mid), std::fabs (side) }); break;   // M+S en crête
            case StereoType::LeftRight: break;
        }
    }
    return mag == MagType::RMS ? sqrDb (sum / n) : peakDb (peak);
}
} // namespace neptune::core
