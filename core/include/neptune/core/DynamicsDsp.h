#pragma once
// Briques de traitement de la dynamique (compresseur), PORTÉES de ZL Compressor de zsliu98 (https://github.com/ZL-Audio/ZLCompressor, AGPLv3 comme
// ce projet) : courbe de gain (compression, expansion, gonflement), suiveur « pump / punch / smooth », suiveur RMS, écrêteur tanh, maintien.
// Les algorithmes et leurs constantes sont ceux de ZL (fichiers source/dsp/compressor/*), réécrits en C++ portable sans Highway, sans atomiques
// (les réglages sont lus par le module à chaque bloc) et en double précision. Références de ZL :
//   Giannoulis, Massberg, Reiss, « Digital dynamic range compressor design - A tutorial and analysis », JAES 2012.
// Copyright (C) 2025-2026 zsliu98 pour les algorithmes d'origine ; adaptation pour ce projet.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace neptune::core::dyn
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kLogMin = 1e-12;
constexpr double kLogMul = 8.6858896380650365530;      // 20 / ln 10
constexpr double kLogSqrMin = 1e-24;
constexpr double kLogSqrMul = 4.3429448190325182765;   // 10 / ln 10

inline double decibelsToGain (double db) noexcept { return std::pow (10.0, db * 0.05); }
inline double gainToDecibels (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1e-12)); }
inline double squareToDecibels (double v) noexcept { return 10.0 * std::log10 (std::max (v, 1e-24)); }

enum class Direction { Compress = 0, Inflate = 1, Expand = 2, Shape = 3 };   // l'ordre des choix de ZL
enum class Style { Clean = 0, Classic = 1, Optical = 2, Vocal = 3 };

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Courbe : renvoie la DIFFÉRENCE de niveau (dB) que la courbe applique à un niveau d'entrée x (dB). Compression et Shape : au-dessus du seuil, genou
// quadratique, courbe de transition entre ratio linéaire et ratio « en bas » / « en haut » ; Expansion et Inflation : sous le seuil, jusqu'au plancher.
class GainComputer
{
public:
    void set (Direction d, double thresholdDb, double ratio, double kneeWidth, double curve, double floorDb) noexcept
    {
        direction_ = d;
        threshold_ = thresholdDb;
        ratio_ = std::max (1.0, ratio);
        knee_ = std::max (kneeWidth, 0.01);
        curve_ = std::clamp (curve, -1.0, 1.0);
        floor_ = floorDb;
        update();
    }
    Direction direction() const noexcept { return direction_; }

    double eval (double x) const noexcept
    {
        if (direction_ == Direction::Compress || direction_ == Direction::Shape)
        {
            if (x <= low_) return 0.0;
            if (x < high_) return (mid_[0] * x + mid_[1]) * x + mid_[2];
            if (x < 0.0) return (up_[0] * x + up_[1]) * x + up_[2];
            return over_[0] * x + over_[1];
        }
        if (x >= high_) return 0.0;
        if (x > low_) return (mid_[0] * x + mid_[1]) * x + mid_[2];
        if (x > cFloor_)
        {
            const double s = x - cFloor_;
            return (lowPoly_[0] * s + lowPoly_[1]) * s * s;
        }
        return 0.0;
    }
    // Niveau de sortie (dB) pour un niveau d'entrée (dB) : pour tracer la courbe.
    double evalOutput (double x) const noexcept { return x + eval (x); }

private:
    void update() noexcept
    {
        low_ = threshold_ - knee_;
        high_ = threshold_ + knee_;
        if (direction_ == Direction::Compress || direction_ == Direction::Shape)
        {
            const double a0 = (1.0 / ratio_ - 1.0) / (knee_ * 4.0), a1 = -low_;
            mid_[0] = a0;
            mid_[1] = 2.0 * a0 * a1;
            mid_[2] = a0 * a1 * a1;
            // courbe de transition : mélange de la droite de ratio et de la courbe « basse » (curve >= 0) ou « haute » (curve < 0)
            const double lb = 1.0 / ratio_, lc = threshold_ * (1.0 - lb);
            double aa, ab, ac;
            if (curve_ >= 0.0)
            {
                const double alpha = 1.0 - curve_, beta = curve_, tmp = 0.5 / ratio_;
                const double da = tmp / std::min(threshold_ + knee_, -0.0001), dc = tmp * (knee_ - threshold_) + threshold_;
                ac = alpha * lc + beta * dc;
                ab = alpha * lb;
                aa = beta * da;
            }
            else
            {
                const double alpha = 1.0 + curve_, beta = -curve_, tmp = 0.5 * (1.0 - ratio_) / ratio_;
                const double ua = tmp / std::min(threshold_ + knee_, -0.0001), uc = tmp * (knee_ - threshold_), ub = 1.0;
                ac = alpha * lc + beta * uc;
                ab = alpha * lb + beta * ub;
                aa = beta * ua;
            }
            up_ = { aa, ab - 1.0, ac };
            if (high_ <= 0.0) over_ = { up_[1], up_[2] };
            else over_ = { lb - 1.0, lc };
        }
        else
        {
            const double r = direction_ == Direction::Expand ? ratio_ : std::clamp (2.0 - 1.0 / ratio_, 1.0, 2.0);
            cFloor_ = std::min (floor_, low_ - 1.0);
            const double a0 = (1.0 - r) / (4.0 * knee_), a1 = -high_;
            mid_[0] = a0;
            mid_[1] = 2.0 * a0 * a1;
            mid_[2] = a0 * a1 * a1;
            const double scale = r - 1.0, base = -cFloor_ + threshold_, denom = base - knee_;
            const double inv2 = 1.0 / (denom * denom), inv3 = inv2 / denom;
            lowPoly_ = { scale * (base + knee_) * inv3, -scale * (base + knee_ + knee_) * inv2 };
        }
    }

    Direction direction_ = Direction::Compress;
    double threshold_ = -18.0, ratio_ = 2.0, knee_ = 0.25, curve_ = 0.0, floor_ = -140.0;
    double low_ = 0.0, high_ = 0.0, cFloor_ = -140.0;
    std::array<double, 3> mid_ {}, up_ {};
    std::array<double, 2> over_ {}, lowPoly_ {};
};

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Suiveur « pump / punch / smooth » : lissage exponentiel dont les temps d'attaque et de relâchement (ms) sont des CONSTANTES DE TEMPS : après un échelon, la sortie a
// parcouru 63 % du chemin au bout de ce temps (relevé par le banc de test de la suite, 2026-10-29 : la version précédente prenait le réglage
// pour une période, et la constante réelle était 2 pi fois plus courte que le temps affiché). Plus,
//  - PUMP (> 0) : après une attaque, la pente est rendue plus douce (la compression « respire ») ; PUNCH (< 0) : l'inverse ;
//  - SMOOTH : étage de relâchement sur l'entrée, entièrement ou en mélange (0..1).
enum class PunchState { Off, Punch, Pump };
enum class SmoothState { Off, Full, Mix };

class Follower
{
public:
    void prepare (double sampleRate) noexcept
    {
        factor_ = -1000.0 / sampleRate;   // exp(-1 / (constante de temps en échantillons)) = exp(factor_ / ms)
        update();
    }
    void reset (double v = 0.0) noexcept { y_ = v; state_ = v; slope_ = 0.0; }
    void set (double attackMs, double releaseMs, double pumpPunch, double smooth) noexcept
    {
        attackMs_ = std::max(0.0, attackMs);
        releaseMs_ = std::max(0.0, releaseMs);
        pp_portion_ = std::clamp (pumpPunch, -1.0, 1.0);
        smooth_portion_ = std::clamp (smooth, 0.0, 1.0);
        update();
    }
    PunchState punchState() const noexcept { return ppState_; }
    SmoothState smoothState() const noexcept { return sState_; }

    double process (double x) noexcept
    {
        double y0;
        if (sState_ == SmoothState::Off) y0 = x >= y_ ? attack_ * (y_ - x) + x : release_ * (y_ - x) + x;
        else if (sState_ == SmoothState::Full)
        {
            state_ = std::max (x, release_ * (state_ - x) + x);
            y0 = attack_ * (y_ - state_) + state_;
        }
        else
        {
            state_ = std::max (x, release_ * (state_ - x) + x);
            const double y1 = attack_ * (y_ - state_) + state_, y2 = x >= y_ ? attack_ * (y_ - x) + x : release_ * (y_ - x) + x;
            y0 = smooth_ * (y1 - y2) + y2;
        }
        if (ppState_ == PunchState::Pump)
        {
            const double s0 = y0 - y_;
            slope_ = s0 < slope_ ? pp_ * (slope_ - s0) + s0 : s0;
            y_ += slope_;
        }
        else if (ppState_ == PunchState::Punch)
        {
            const double s0 = y0 - y_;
            slope_ = (s0 > slope_ && slope_ >= 0.0) ? pp_ * (slope_ - s0) + s0 : s0;
            y_ += slope_;
        }
        else y_ = y0;
        return y_;
    }

private:
    void update() noexcept
    {
        if (attackMs_ < 0.001) attack_ = 0.0;
        else if (std::abs (pp_portion_) > 0.0001) attack_ = std::exp (factor_ / attackMs_ / (1.0 - std::pow (std::abs (pp_portion_), 2.0) * 0.125));
        else attack_ = std::exp (factor_ / attackMs_);
        release_ = releaseMs_ < 0.001 ? 0.0 : std::exp (factor_ / releaseMs_);
        smooth_ = smooth_portion_;
        sState_ = smooth_ < 0.0001 ? SmoothState::Off : smooth_ > 0.9999 ? SmoothState::Full : SmoothState::Mix;
        if (attackMs_ < 0.0001 || std::abs (pp_portion_) < 0.0001)
        {
            slope_ = 0.0;
            pp_ = 0.0;
            ppState_ = PunchState::Off;
        }
        else
        {
            ppState_ = pp_portion_ > 0.0 ? PunchState::Pump : PunchState::Punch;
            pp_ = std::exp (factor_ / attackMs_ / std::abs (pp_portion_));
        }
    }

    double y_ = 0.0, state_ = 0.0, slope_ = 0.0, attack_ = 0.0, release_ = 0.0, pp_ = 0.0, smooth_ = 0.0;
    double attackMs_ = 50.0, releaseMs_ = 100.0, pp_portion_ = 0.0, smooth_portion_ = 0.0;
    double factor_ = -1000.0 / 48000.0;
    PunchState ppState_ = PunchState::Off;
    SmoothState sState_ = SmoothState::Off;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Suiveur RMS : somme glissante des carrés sur une fenêtre (secondes).
class RmsTracker
{
public:
    // Réserve de quoi suivre `maxSamples` échantillons (hors temps réel) ; `setRate` change la cadence sans réallouer.
    void allocate (std::size_t maxSamples) { ring_.assign (std::max<std::size_t> (1, maxSamples) + 2, 0.0f); setSeconds (seconds_); reset(); }
    void setRate (double sampleRate) noexcept { sampleRate_ = sampleRate; setSeconds (seconds_); }
    void reset() noexcept { std::fill (ring_.begin(), ring_.end(), 0.0f); head_ = 0; filled_ = 0; sum_ = 0.0; }
    void setSeconds (double s) noexcept
    {
        seconds_ = s;
        size_ = std::clamp<std::size_t> (static_cast<std::size_t> (std::max (1.0, s * sampleRate_)), 1, std::max<std::size_t> (1, ring_.size() - 1));
        sizeInv_ = 1.0 / static_cast<double> (size_);
        while (filled_ > size_) { sum_ -= ring_[(head_ + ring_.size() - filled_) % ring_.size()]; --filled_; }
    }
    void process (double x) noexcept
    {
        const auto sq = static_cast<float> (x * x);
        if (filled_ == size_) { sum_ -= ring_[(head_ + ring_.size() - filled_) % ring_.size()]; --filled_; }
        sum_ = std::max (sum_, 0.0);
        ring_[head_] = sq;
        head_ = (head_ + 1) % ring_.size();
        ++filled_;
        sum_ += sq;
    }
    double meanSquare() const noexcept { return sum_ * sizeInv_; }

private:
    std::vector<float> ring_;
    std::size_t head_ = 0, filled_ = 0, size_ = 1;
    double sum_ = 0.0, sizeInv_ = 1.0, sampleRate_ = 48000.0, seconds_ = 0.0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Écrêteur tanh dont le gain est calé pour que le niveau 0 dBFS soit atténué de `reductionAtUnitDb` (par la quantité de « drive »).
class TanhClipper
{
public:
    void set (double wetPercent, double reductionAtUnitDb) noexcept
    {
        wet_ = std::clamp (wetPercent, 0.0, 100.0) / 100.0;
        on_ = wet_ > 1e-5;
        if (on_) updateK (std::clamp (reductionAtUnitDb, -80.0, 0.0) * wet_);
    }
    bool on() const noexcept { return on_; }
    double process (double x) const noexcept { return std::tanh (x * k1_) * k2_; }

private:
    static double evalP (double y, double k) { return std::tanh (k) / k / y; }
    static double getK (double y, double kl, double kr)
    {
        constexpr double kLow = 0.999, kHigh = 1.001;
        if (evalP (y, kr) >= kLow) return kr;
        if (evalP (y, kl) <= kHigh) return kl;
        for (int i = 0; i < 100; ++i)
        {
            const double k = std::sqrt (kl * kr), p = evalP (y, k);
            if (p > kHigh) kl = k;
            else if (p < kLow) kr = k;
            else return k;
        }
        return std::sqrt (kl * kr);
    }
    void updateK (double reduction) noexcept
    {
        const double g = decibelsToGain (reduction);
        if (reduction > -2.895296501106843e-08) k1_ = 1e-4;
        else if (reduction > -2.895295870818236e-06) k1_ = getK (g, 1e-4, 1e-3);
        else if (reduction > -0.0002895228991000713) k1_ = getK (g, 1e-3, 1e-2);
        else if (reduction > -0.02888559791501009) k1_ = getK (g, 1e-2, 1e-1);
        else if (reduction > -2.3655279428366893) k1_ = getK (g, 1e-1, 1.0);
        else if (reduction > -20.000000035805904) k1_ = getK (g, 1.0, 10.0);
        else if (reduction > -40.0) k1_ = getK (g, 10.0, 100.0);
        else k1_ = 1.0 / g;
        k2_ = 1.0 / k1_;
    }
    double wet_ = 0.0, k1_ = 1.0, k2_ = 1.0;
    bool on_ = false;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------
// Minimum glissant sur les `size` dernières valeurs (maintien de la réduction la plus forte), sans allocation après setCapacity().
class MinHold
{
public:
    void setCapacity (std::size_t capacity) { cap_ = std::max<std::size_t> (2, capacity + 2); values_.assign (cap_, 0.0f); stamps_.assign (cap_, 0); clear(); }
    void setSize (std::size_t size) noexcept { size_ = std::min (size, cap_ - 2); }
    std::size_t size() const noexcept { return size_; }
    void clear() noexcept { head_ = tail_ = 0; count_ = 0; time_ = 0; }
    float push (float x) noexcept
    {
        if (size_ == 0) return x;
        ++time_;
        while (count_ > 0 && values_[(head_ + cap_ - 1) % cap_] >= x) { head_ = (head_ + cap_ - 1) % cap_; --count_; }   // on retire par la queue les valeurs plus fortes
        values_[head_] = x;
        stamps_[head_] = time_;
        head_ = (head_ + 1) % cap_;
        ++count_;
        // on retire par la tête les valeurs sorties de la fenêtre
        while (count_ > 0 && time_ - stamps_[tail_] >= size_) { tail_ = (tail_ + 1) % cap_; --count_; }
        return values_[tail_];
    }

private:
    std::vector<float> values_;
    std::vector<std::uint64_t> stamps_;
    std::size_t cap_ = 2, size_ = 0, head_ = 0, tail_ = 0, count_ = 0;
    std::uint64_t time_ = 0;
};

// Retard entier (échantillons), circulaire.
class IntegerDelay
{
public:
    void prepare (std::size_t maxSamples) { ring_.assign (maxSamples + 2, 0.0f); pos_ = 0; delay_ = 0; }
    void reset() noexcept { std::fill (ring_.begin(), ring_.end(), 0.0f); pos_ = 0; }
    void setDelay (std::size_t d) noexcept { delay_ = std::min (d, ring_.size() - 1); }
    std::size_t delay() const noexcept { return delay_; }
    float process (float x) noexcept
    {
        if (delay_ == 0) return x;
        ring_[pos_] = x;
        const float y = ring_[(pos_ + ring_.size() - delay_) % ring_.size()];
        pos_ = (pos_ + 1) % ring_.size();
        return y;
    }

private:
    std::vector<float> ring_;
    std::size_t pos_ = 0, delay_ = 0;
};
} // namespace neptune::core::dyn
