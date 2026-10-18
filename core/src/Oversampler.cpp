#include "neptune/core/Oversampler.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
double bessel0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}
} // namespace

HalfbandStage::HalfbandStage (int taps) : taps_ (taps % 2 == 0 ? taps + 1 : taps), h_ (static_cast<std::size_t> (taps_)), histUp_ (static_cast<std::size_t> (taps_ / 2 + 2), 0.0f), histDown_ (static_cast<std::size_t> (taps_ - 1), 0.0f)
{
    // passe-bas à fc = 1/4 de la cadence haute (demi-bande), fenêtre de Kaiser (atténuation ~80 dB)
    const int m = (taps_ - 1) / 2;
    const double beta = 8.0, norm = bessel0 (beta);
    double sum = 0.0;
    for (int j = 0; j < taps_; ++j)
    {
        const double t = static_cast<double> (j - m);
        const double sinc = t == 0.0 ? 0.5 : std::sin (0.5 * kPi * t) / (kPi * t);
        const double r = t / static_cast<double> (std::max (1, m));
        const double win = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / norm;
        h_[static_cast<std::size_t> (j)] = static_cast<float> (sinc * win);
        sum += sinc * win;
    }
    for (auto& v : h_) v = static_cast<float> (v / sum);   // gain continu 1 à la cadence haute (l'interpolation x2 le double ensuite)
}

void HalfbandStage::prepare (int maxInput)
{
    work_.assign (static_cast<std::size_t> (2 * std::max (1, maxInput) + taps_ + 8), 0.0f);
    reset();
}

void HalfbandStage::reset() noexcept
{
    std::fill (histUp_.begin(), histUp_.end(), 0.0f);
    std::fill (histDown_.begin(), histDown_.end(), 0.0f);
}

// Interpolateur : insère un zéro après chaque entrée, filtre, gain x2.
void HalfbandStage::up (const float* in, int n, float* out) noexcept
{
    const int hist = static_cast<int> (histUp_.size());
    // tampon linéaire : historique puis entrées
    float* w = work_.data();
    std::copy (histUp_.begin(), histUp_.end(), w);
    std::copy (in, in + n, w + hist);
    for (int i = 0; i < n; ++i)
        for (int phase = 0; phase < 2; ++phase)
        {
            // sortie d'indice haut k = 2i + phase : somme sur j de h[j] * xz[k - j], où xz est l'entrée avec un zéro intercalé ;
            // seuls les j de même parité que `phase` tombent sur une entrée réelle, d'indice i - (j - phase) / 2
            float acc = 0.0f;
            for (int j = phase; j < taps_; j += 2) acc += h_[static_cast<std::size_t> (j)] * w[hist + i - (j - phase) / 2];
            out[2 * i + phase] = 2.0f * acc;
        }
    std::copy (w + n, w + n + hist, histUp_.begin());
}

// Décimateur : filtre à la cadence haute, garde un échantillon sur deux.
void HalfbandStage::down (const float* in, int n, float* out) noexcept
{
    const int hist = static_cast<int> (histDown_.size());
    float* w = work_.data();
    std::copy (histDown_.begin(), histDown_.end(), w);
    std::copy (in, in + 2 * n, w + hist);
    for (int i = 0; i < n; ++i)
    {
        const int k = 2 * i;   // un échantillon sur deux ; interpolateur + décimateur : retard total de (taps - 1) échantillons haute cadence, pair
        float acc = 0.0f;
        for (int j = 0; j < taps_; ++j) acc += h_[static_cast<std::size_t> (j)] * w[hist + k - j];
        out[i] = acc;
    }
    std::copy (w + 2 * n, w + 2 * n + hist, histDown_.begin());
}

void Oversampler::prepare (int maxBlock)
{
    maxBlock = std::max (1, maxBlock);
    s1_.prepare (maxBlock);
    s2_.prepare (2 * maxBlock);
    b1_.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    b2_.assign (static_cast<std::size_t> (2 * maxBlock), 0.0f);
    b4_.assign (static_cast<std::size_t> (4 * maxBlock), 0.0f);
}

void Oversampler::reset() noexcept
{
    s1_.reset();
    s2_.reset();
}

void Oversampler::setFactor (int factor) noexcept
{
    const int f = factor >= 4 ? 4 : factor >= 2 ? 2 : 1;
    if (f != factor_) { factor_ = f; reset(); }
}

float* Oversampler::up (const float* in, int n) noexcept
{
    lastN_ = n;
    if (factor_ == 1)
    {
        std::copy (in, in + n, b1_.begin());
        return b1_.data();
    }
    s1_.up (in, n, b2_.data());
    if (factor_ == 2) return b2_.data();
    s2_.up (b2_.data(), 2 * n, b4_.data());
    return b4_.data();
}

void Oversampler::down (float* out, int n) noexcept
{
    if (factor_ == 1)
    {
        std::copy (b1_.begin(), b1_.begin() + n, out);
        return;
    }
    if (factor_ == 4) s2_.down (b4_.data(), 2 * n, b2_.data());
    s1_.down (b2_.data(), n, out);
}
} // namespace neptune::core
