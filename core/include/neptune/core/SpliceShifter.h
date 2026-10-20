#pragma once
// Décaleur de hauteur à raccords (« splice ») pour les petits écarts de hauteur (effet de type micro-pitch) : un pointeur de lecture dérive dans un court
// tampon à la vitesse (rapport - 1) échantillons par échantillon ; quand il atteint l'extrémité de sa course (la longueur de raccord), un deuxième pointeur
// reprend à l'autre extrémité et un fondu court passe de l'un à l'autre. Pour éviter les « glitchs », le point de raccord est cherché autour de la position
// théorique en maximisant la corrélation entre l'ancien et le nouveau pointeur sur les quelques dernières millisecondes (de-glitching). Aucune allocation
// pendant le traitement. Conçu pour des rapports proches de 1 (quelques cents) mais valable de -12 à +12 demi-tons.
// Le retard du décaleur varie dans [bas, bas + longueur] ; son retard moyen est `baseDelaySamples()`.
#include <algorithm>
#include <cmath>
#include <vector>

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
        window_ = std::max (32.0, std::min (0.004 * fs_, 0.25 * spliceSmp_));
        d1_ = std::clamp (d1_, low_, high_);
        if (fading_) fading_ = false;
    }
    void reset() noexcept
    {
        std::fill (buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        fading_ = false;
        d1_ = low_ + 0.5 * spliceSmp_;
    }
    double baseDelaySamples() const noexcept { return low_ + 0.5 * spliceSmp_; }   // retard initial et moyen du décaleur
    double currentDelaySamples() const noexcept { return d1_; }

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
        else
        {
            y = read (d1_);
            d1_ -= drift;
            if (drift > 1.0e-9 && d1_ <= low_ + drift * xf) startSplice (+1, xf);
            else if (drift < -1.0e-9 && d1_ >= high_ + drift * xf) startSplice (-1, xf);
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
    void startSplice (int dir, double xf) noexcept
    {
        const double target = d1_ + dir * spliceSmp_;
        const long w = static_cast<long> (window_), s = static_cast<long> (search_), base = std::lround (d1_);
        double bestScore = -2.0;
        long bestShift = 0;
        double energyA = 0.0;
        for (long i = 0; i < w; ++i) { const double a = at (base + i); energyA += a * a; }
        if (energyA > 1.0e-9)
        {
            for (long shift = -s; shift <= s; ++shift)
            {
                const long cand = std::lround (target) + shift;
                if (cand < 4 || cand + w + 4 >= static_cast<long> (buf_.size())) continue;
                double ab = 0.0, bb = 0.0;
                for (long i = 0; i < w; ++i)
                {
                    const double a = at (base + i), b = at (cand + i);
                    ab += a * b;
                    bb += b * b;
                }
                const double score = ab / std::sqrt (energyA * bb + 1.0e-12);
                if (score > bestScore) { bestScore = score; bestShift = shift; }
            }
        }
        d2_ = target + static_cast<double> (bestShift);
        fadeLen_ = xf;
        fadePos_ = 0.0;
        fading_ = true;
    }

    double fs_ = 48000.0, spliceSmp_ = 1440.0, search_ = 288.0, low_ = 384.0, high_ = 1824.0, window_ = 192.0;
    double d1_ = 1104.0, d2_ = 0.0, fadeLen_ = 64.0, fadePos_ = 0.0;
    std::vector<float> buf_;
    std::size_t pos_ = 0;
    bool fading_ = false;
};
} // namespace neptune::core
