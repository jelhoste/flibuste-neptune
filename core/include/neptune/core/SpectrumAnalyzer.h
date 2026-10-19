#pragma once
// Analyseur de spectre en temps réel (FFT) pour l'égaliseur, PORTÉ de ZL Equalizer de zsliu98 (https://github.com/ZL-Audio/ZLEqualizer, AGPLv3 comme
// ce projet ; fichiers source/dsp/analyzer/fft_analyzer/*, source/panel/curve_panel/fft_panel) : filtre passe-haut de suppression du continu sur les
// échantillons tirés, fenêtre de Hann périodique, spectre de puissance (stéréo : somme des puissances des deux voies ; gauche, droite, milieu ou côtés),
// lissage par moyenne glissante appliquée deux fois (octave ou échelle ERB), « Normal » = une FFT de taille moyenne, « High » = trois FFT (grave,
// moyenne, aiguë) mélangées aux croisements 200-400 Hz et 2-4 kHz avec égalisation de la puissance du bruit blanc, conversion en dB, pente de compensation
// (tilt) autour de 1 kHz, retombée exponentielle avec montée immédiate. Réécrit pour ce projet (FFT radix 2 maison, sans Highway) ; niveau normalisé pour
// qu'une sinusoïde pleine échelle affiche 0 dB.
// Copyright (C) 2025-2026 zsliu98 pour les algorithmes d'origine ; adaptation pour ce projet.
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstdint>
#include <vector>

#include "neptune/core/AnalyzerRing.h"

namespace neptune::core
{
enum class SpectrumChannels { Stereo = 0, Left, Right, Mid, Side };

struct SpectrumSettings
{
    bool highQuality = false;
    int speedIdx = 2;        // très rapide, rapide, moyen, lent, très lent : 4, 2, 1, 0,5, 0,25
    int tiltIdx = 3;         // 0, 1,5, 3, 4,5, 6 dB/oct
    int smoothType = 1;      // 0 octave, 1 ERB
    int smoothOct = 1;       // 1/24, 1/12, 1/6, 1/3, 1
    int smoothErb = 1;       // 1/4, 1/2, 1, 2, 4
    float topDb = 0.0f;      // haut de l'échelle
    float rangeDb = -60.0f;  // la plage descend de topDb à topDb + rangeDb
    SpectrumChannels channels = SpectrumChannels::Stereo;
};

constexpr std::array<double, 5> kSpectrumSpeeds { 4.0, 2.0, 1.0, 0.5, 0.25 };
constexpr std::array<float, 5> kSpectrumTilts { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f };
constexpr std::array<double, 5> kSpectrumSmoothOct { 1.0 / 24.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0, 1.0 };
constexpr std::array<double, 5> kSpectrumSmoothErb { 0.25, 0.5, 1.0, 2.0, 4.0 };

// ---- FFT complexe radix 2 (taille 2^ordre), avec un seul calcul pour deux signaux réels (l'un en partie réelle, l'autre en partie imaginaire)
class Fft
{
public:
    explicit Fft (int order);
    std::size_t size() const noexcept { return n_; }
    // `re` et `im` : N échantillons chacun (écrasés par le résultat)
    void transform (float* re, float* im) const noexcept;

private:
    std::size_t n_;
    std::vector<std::uint32_t> rev_;
    std::vector<float> cosT_, sinT_;
};

// ---- Makima : interpolation d'Akima modifiée (abscisses croissantes), comme `SeqMakima` de ZL ; sert à lisser le tracé aux basses fréquences
class Makima
{
public:
    // `x`, `y` : n points ; les pentes extrêmes sont imposées
    void prepare (const float* x, const float* y, std::size_t n, float leftSlope, float rightSlope);
    void eval (const float* xOut, float* yOut, std::size_t m) const;

private:
    const float *x_ = nullptr, *y_ = nullptr;
    std::size_t n_ = 0;
    std::vector<float> d_, delta_;
};

class SpectrumAnalyzer
{
public:
    static constexpr int kSources = 2;   // entrée, sortie

    // Allocation (hors temps réel). Les ordres des FFT suivent la fréquence d'échantillonnage (4096 points à 44,1 / 48 kHz pour la FFT moyenne).
    void prepare (double sampleRate, bool highQuality);
    void setSettings (const SpectrumSettings&);
    const SpectrumSettings& settings() const noexcept { return settings_; }

    // Tire les échantillons nouveaux de `ringStream` (deux canaux) dans l'historique de `source` ; `readPos` est la position de lecture de l'appelant
    void pull (int source, const AnalyzerRing&, int ringStream, std::uint64_t& readPos);
    // Calcule le spectre de `source` (en dB, sur frequencies()) à partir de l'historique, avec lissage, pente et retombée (`refreshRate` en Hz)
    void analyze (int source, double refreshRate);
    void reset();

    const std::vector<float>& frequencies() const noexcept { return frequencies_; }
    const std::vector<float>& spectrumDb (int source) const noexcept { return spectrumDb_[static_cast<std::size_t> (source)]; }
    std::size_t numPoints() const noexcept { return frequencies_.size(); }
    double sampleRate() const noexcept { return sampleRate_; }
    bool highQuality() const noexcept { return highQuality_; }
    std::size_t historySize() const noexcept { return history_; }

    static int scaledOrder (double sampleRate);   // ordre de la FFT moyenne

private:
    struct Resolution
    {
        std::unique_ptr<Fft> fft;
        std::vector<float> window;
        std::vector<std::size_t> lowIdx, highIdx;
        std::vector<float> countReq;
        float noiseScale = 1.0f;
        std::vector<float> power;   // puissance lissée de la source en cours
    };
    void configureSmoothing();
    void forward (int source, Resolution&);
    static void smooth (Resolution&, std::vector<double>& cum);

    double sampleRate_ = 0.0;
    bool highQuality_ = false, prepared_ = false;
    SpectrumSettings settings_;
    std::size_t history_ = 0;
    std::array<Resolution, 3> res_;   // 0 grave (FFT la plus grande), 1 moyenne, 2 aiguë
    std::vector<float> frequencies_, tiltShift_;
    std::array<std::vector<std::vector<float>>, kSources> hist_;   // [source][canal]
    std::array<std::array<float, 2>, kSources> hpX_ {}, hpY_ {};
    std::array<std::vector<float>, kSources> spectrumDb_, decayState_;
    std::vector<float> re_, im_, blendLow_, blendMid_, blendHigh_;
    std::vector<double> cum_;
    float decayP_ = 1.0f;
    double decayRefresh_ = 0.0;
};
} // namespace neptune::core
