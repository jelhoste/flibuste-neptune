#pragma once
// Décaleur de hauteur à raccords (« splice ») pour les petits écarts de hauteur (effet de type micro-pitch) : un pointeur de lecture dérive dans un court
// tampon à la vitesse (rapport - 1) échantillons par échantillon ; quand il atteint l'extrémité de sa course (la longueur de raccord), un deuxième pointeur
// reprend à l'autre extrémité et un fondu court passe de l'un à l'autre. Pour éviter les « glitchs », le point de raccord est cherché autour de la position
// théorique en maximisant la corrélation entre l'ancien et le nouveau pointeur sur les quelques dernières millisecondes (de-glitching). Aucune allocation
// pendant le traitement. Conçu pour des rapports proches de 1 (quelques cents) mais valable de -12 à +12 demi-tons.
// Le retard du décaleur varie dans [bas, bas + longueur] ; son retard moyen est `baseDelaySamples()`.
// La recherche du point de raccord (la partie chère : une corrélation par position candidate) est étalée sur plusieurs échantillons après le moment où le raccord
// devient nécessaire, pour qu'aucun bloc audio, même court (32 échantillons), ne porte tout le coût. Elle compare des zones figées du tampon, mesurées depuis
// le moment du déclenchement : le résultat est le même que si elle avait été faite d'un coup ; seul le début du fondu est retardé de quelques millisecondes, et
// le retard des deux pointeurs, qui dérivent ensemble, garde le même écart.
#include <algorithm>
#include <cmath>
#include <vector>

#include <array>

#include "neptune/core/DelayRead.h"

namespace neptune::core
{
class SpliceShifter
{
public:
    void prepare (double sampleRate)
    {
        fs_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        buf_.assign (static_cast<std::size_t> (0.35 * fs_) + 64, 0.0f);
        setSplice (30.0);
        reset();
    }
    // Longueur de raccord (ms, 5 à 100) : étendue de la course du retard.
    void setSplice (double ms) noexcept
    {
        spliceSmp_ = std::clamp (ms, 5.0, 100.0) * 0.001 * fs_;
        search_ = std::min (0.25 * spliceSmp_, 0.006 * fs_);
        low_ = search_ + 0.002 * fs_;
        high_ = low_ + spliceSmp_;
        window_ = std::max (32.0, std::min ({ 0.004 * fs_, 0.25 * spliceSmp_, static_cast<double> (kMaxWindow) }));
        d1_ = std::clamp (d1_, low_, high_);
        fading_ = false;
        searching_ = false;
    }
    void reset() noexcept
    {
        std::fill (buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        fading_ = false;
        searching_ = false;
        d1_ = low_ + 0.5 * spliceSmp_;
    }
    double baseDelaySamples() const noexcept { return low_ + 0.5 * spliceSmp_; }   // retard initial et moyen du décaleur
    double currentDelaySamples() const noexcept { return d1_; }
    long candidatesPerSample() const noexcept { return perSample_; }   // positions candidates évaluées par échantillon pendant la recherche en cours (ou de la dernière)

    // Un échantillon. `ratio` : rapport de fréquences (1 = inchangé, 2^(cents/1200)).
    float process (float x, double ratio) noexcept
    {
        buf_[pos_] = x;
        pos_ = (pos_ + 1) % buf_.size();
        const double drift = ratio - 1.0;   // le retard varie de -drift échantillon par échantillon
        const double xf = std::max (16.0, std::min (0.25 * spliceSmp_, 0.008 * fs_) / (1.0 + 2.0 * std::fabs (drift)));
        float y;
        if (fading_)
        {
            const double w = 0.5 - 0.5 * std::cos (3.14159265358979 * fadePos_ / fadeLen_);
            y = static_cast<float> ((1.0 - w) * read (d1_) + w * read (d2_));
            d1_ -= drift;
            d2_ -= drift;
            if (++fadePos_ >= fadeLen_) { d1_ = d2_; fading_ = false; }
        }
        else if (searching_)
        {
            y = read (d1_);
            d1_ -= drift;
            continueSearch();
        }
        else
        {
            y = read (d1_);
            d1_ -= drift;
            if (drift > 1.0e-9 && d1_ <= low_ + drift * xf) beginSearch (+1, xf, drift);
            else if (drift < -1.0e-9 && d1_ >= high_ + drift * xf) beginSearch (-1, xf, drift);
        }
        d1_ = std::clamp (d1_, 3.0, high_ + search_ + 8.0);
        return y;
    }

private:
    float read (double d) const noexcept { return readHermite (buf_, pos_, std::max (3.0, d)); }
    float at (long delay) const noexcept
    {
        const auto size = static_cast<long> (buf_.size());
        return buf_[static_cast<std::size_t> ((((static_cast<long> (pos_) - delay) % size) + size) % size)];
    }
    // dir = +1 : la hauteur monte, le retard diminue, le nouveau pointeur repart une longueur de raccord plus loin dans le passé ; -1 : l'inverse
    // Prépare la recherche : copie la fenêtre de référence (la zone que lit le pointeur actuel) et fixe le nombre d'échantillons sur lesquels étaler les candidats.
    void beginSearch (int dir, double xf, double drift) noexcept
    {
        dir_ = dir;
        xf_ = xf;
        target_ = d1_ + dir * spliceSmp_;
        searchD1_ = d1_;
        searchPos_ = pos_;
        elapsed_ = 0;
        window_i_ = std::min (static_cast<long> (window_), static_cast<long> (kMaxWindow));
        searchRadius_ = static_cast<long> (search_);
        cursor_ = -searchRadius_;
        bestScore_ = -2.0;
        bestShift_ = 0;
        const long size = static_cast<long> (buf_.size());
        long idx = indexAt (std::lround (d1_));
        energyA_ = 0.0;
        for (long i = 0; i < window_i_; ++i)
        {
            const float v = buf_[static_cast<std::size_t> (idx)];
            ref_[static_cast<std::size_t> (i)] = v;
            const double a = v;
            energyA_ += a * a;
            idx = idx == 0 ? size - 1 : idx - 1;
        }
        // Le pointeur actuel continue de dériver pendant la recherche : on lui laisse au plus la moitié de sa marge avant les limites du retard.
        const double slack = dir > 0 ? d1_ - 8.0 : (high_ + search_ + 8.0) - d1_;
        const double room = std::max (1.0, 0.5 * slack / std::max (1.0e-9, std::fabs (drift)));
        const long samples = std::clamp (static_cast<long> (room), 1L, kMaxSearchSamples);
        const long candidates = 2 * searchRadius_ + 1;
        perSample_ = energyA_ > 1.0e-9 ? std::max (1L, (candidates + samples - 1) / samples) : candidates;
        searching_ = true;
    }
    // Indice du tampon pour un retard compté depuis l'instant du déclenchement ; la lecture recule d'un cran par échantillon plus ancien.
    long indexAt (long delay) const noexcept
    {
        const long size = static_cast<long> (buf_.size());
        const long i = (static_cast<long> (searchPos_) - delay) % size;
        return i < 0 ? i + size : i;
    }
    // Quelques candidats par échantillon ; le dernier lance le fondu.
    void continueSearch() noexcept
    {
        ++elapsed_;
        const long size = static_cast<long> (buf_.size());
        const long w = window_i_;
        long done = 0;
        if (energyA_ > 1.0e-9)
        {
            while (cursor_ <= searchRadius_ && done < perSample_)
            {
                const long cand = std::lround (target_) + cursor_;
                ++done;
                const long shift = cursor_++;
                if (cand < 4 || cand + w + 4 + kMaxSearchSamples >= size) continue;
                double ab = 0.0, bb = 0.0;
                long idx = indexAt (cand);
                for (long i = 0; i < w; ++i)
                {
                    const double a = ref_[static_cast<std::size_t> (i)], b = buf_[static_cast<std::size_t> (idx)];
                    ab += a * b;
                    bb += b * b;
                    idx = idx == 0 ? size - 1 : idx - 1;
                }
                const double score = ab / std::sqrt (energyA_ * bb + 1.0e-12);
                if (score > bestScore_) { bestScore_ = score; bestShift_ = shift; }
            }
        }
        else
            cursor_ = searchRadius_ + 1;
        if (cursor_ > searchRadius_)
        {
            // Les deux pointeurs dérivent du même pas : l'écart mesuré au déclenchement est conservé.
            d2_ = d1_ + (target_ + static_cast<double> (bestShift_) - searchD1_);
            fadeLen_ = xf_;
            fadePos_ = 0.0;
            fading_ = true;
            searching_ = false;
        }
    }

    double fs_ = 48000.0, spliceSmp_ = 1440.0, search_ = 288.0, low_ = 384.0, high_ = 1824.0, window_ = 192.0;
    double d1_ = 1104.0, d2_ = 0.0, fadeLen_ = 64.0, fadePos_ = 0.0;
    static constexpr std::size_t kMaxWindow = 2048;   // fenêtre de corrélation au plus (4 ms jusqu'à 512 kHz)
    std::array<float, kMaxWindow> ref_ {};            // copie de la fenêtre de référence pendant la recherche du raccord
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    bool fading_ = false;
    // Recherche du point de raccord en cours (étalée sur plusieurs échantillons, voir plus haut)
    static constexpr long kMaxSearchSamples = 1024;   // durée d'étalement au plus (21 ms à 48 kHz)
    bool searching_ = false;
    int dir_ = 1;
    double xf_ = 64.0, target_ = 0.0, searchD1_ = 0.0, energyA_ = 0.0, bestScore_ = -2.0;
    std::size_t searchPos_ = 0;
    long elapsed_ = 0, window_i_ = 0, searchRadius_ = 0, cursor_ = 0, perSample_ = 1, bestShift_ = 0;
};
} // namespace neptune::core
