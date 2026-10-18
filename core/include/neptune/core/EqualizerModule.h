#pragma once
// Égaliseur paramétrique (série NOVA), PORTÉ de ZL Equalizer de zsliu98 (https://github.com/ZL-Audio/ZLEqualizer, AGPLv3 comme ce projet) :
// 16 bandes IIR à phase minimale (cloche, étagères, passe-haut / passe-bas jusqu'à 96 dB/oct, coupe-bande, passe-bande, inclinaison, passe-tout,
// gain plat), chaque bande en stéréo, gauche, droite, milieu ou côtés ; bandes dynamiques (le gain de la bande glisse entre son gain et un gain
// cible selon le niveau d'un signal de détection filtré, avec seuil, genou, attaque, relâchement, lissage, RMS) ; clé interne ou externe ;
// gain automatique, échelle des gains, gain de sortie, inversion de phase. Voir third_party/zl/README-NEPTUNE.md pour les écarts (pas encore :
// phase linéaire FIR, structures SVF / parallèle, « Flat Tilt », 24 bandes, analyseur).
// Copyright (C) 2025-2026 zsliu98 pour les algorithmes d'origine ; adaptation pour ce projet.
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/Biquad.h"
#include "neptune/core/DynamicsDsp.h"
#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace equalizer
{
constexpr ParamId outputGain = 100, autoGain = 101, gainScale = 102, phaseFlip = 103, externalSide = 104;
constexpr int kBands = 16;
constexpr ParamId bandBase = 120;
constexpr int kBandStride = 24;
constexpr ParamId bandParam (int band, int k) { return bandBase + static_cast<ParamId> (band * kBandStride + k); }
// Paramètres d'une bande (décalage dans la bande).
enum BandParam : int
{
    kStatus = 0, kType, kOrder, kStereo, kFreq, kGain, kQ,
    kDynOn, kDynBypass, kTarget, kThreshold, kKnee, kAttack, kRelease, kSmooth, kRmsLength, kRmsMix,
    kSideType, kSideOrder, kSideFreq, kSideQ, kSideLink
};
enum Status : int { kOff = 0, kBypassed, kOn };
enum Type : int { kPeak = 0, kLowShelf, kLowPass, kHighShelf, kHighPass, kNotch, kBandPass, kTiltShelf, kAllPass, kFlatGain, kNumTypes };
enum Stereo : int { kStereoBoth = 0, kLeft, kRight, kMid, kSide };
enum SideType : int { kSideBandPass = 0, kSideLowPass, kSideHighPass, kSideAll };
constexpr std::array<int, 7> kOrders { 1, 2, 4, 6, 8, 12, 16 };   // ordre du filtre pour 6, 12, 24, 36, 48, 72, 96 dB/oct
constexpr int kMaxSections = 16;

// Valeurs d'affichage : crêtes d'entrée et de sortie, compensation du gain automatique, puis le gain courant de chaque bande (dB).
enum Meter : int { kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kAutoGainDb, kBandGain0, kNumMeters = kBandGain0 + kBands };

// Conception d'un filtre de bande : remplit `out` avec `count` sections (le gain linéaire `scale` s'applique en plus) ; retourne `count`.
int designFilter (Type type, int order, double fs, double freq, double gainDb, double q, std::array<BiquadCoefs, kMaxSections>& out, double& scale) noexcept;
// Compensation de gain (dB) estimée pour une bande (valeur ajoutée par le gain automatique), formules de ZL.
double gainCompensationDb (Type type, double freq, double gainDb, double q) noexcept;
} // namespace equalizer

class EqualizerModule : public StockModule
{
public:
    EqualizerModule();

    std::string_view typeId() const noexcept override { return "equalizer"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;
    bool wantsKey() const noexcept override { return true; }
    void setKey (const float* left, const float* right) noexcept override { keyL_ = left; keyR_ = right; }

    int numMeters() const noexcept override { return equalizer::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < equalizer::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }
    // Réponse en amplitude (dB) d'une bande à la fréquence f ; `current` : avec son gain dynamique du moment (sinon le gain réglé).
    double bandResponseDb (const ParameterSet&, int band, double f, bool current) const noexcept;
    // Réponse de l'ensemble des bandes actives (sans le gain de sortie).
    double totalResponseDb (const ParameterSet&, double f, bool current) const noexcept;

private:
    static constexpr int kChunk = 16;
    struct Chain { std::array<std::array<double, 2>, equalizer::kMaxSections> z {}; void reset() noexcept { for (auto& s : z) s = { 0.0, 0.0 }; } };
    struct Cfg
    {
        int status = 0, type = 0, order = 2, stereo = 0, sideType = 0, sideOrder = 2;
        bool dynOn = false, dynBypass = false, sideLink = false;
        double freq = 1000.0, gain = 0.0, q = 0.707, target = 0.0, threshold = -40.0, knee = 8.0, attack = 100.0, release = 500.0, smooth = 1.0;
        double rmsLength = 0.0, rmsMix = 0.5, sideFreq = 1000.0, sideQ = 0.707;
    };
    struct Band
    {
        Cfg cfg;
        bool haveCfg = false;
        double appliedScaleGain = 1e9;                 // gain scalé pour lequel les coefficients courants ont été calculés
        std::array<BiquadCoefs, equalizer::kMaxSections> coefs {};
        int count = 0;
        double scale = 1.0;
        std::array<Chain, 2> chain;
        std::array<BiquadCoefs, equalizer::kMaxSections> sideCoefs {};
        int sideCount = 0;
        std::array<Chain, 2> sideChain;
        dyn::Follower follower;
        std::vector<double> ring;                      // carrés des échantillons de détection (fenêtre RMS)
        std::size_t ringPos = 0, ringFill = 0;
        double ringSum = 0.0;
        double currentGain = 0.0;                      // dB, avec échelle
    };

    void readSettings (const ParameterSet&) noexcept;
    void designSide (Band&) noexcept;
    void processBand (int index, int n) noexcept;
    double param (ParamId id) const noexcept { return values_[static_cast<std::size_t> (id - 100)]; }
    double scaledGain (double g) const noexcept { return g * gainScale_; }

    std::array<std::size_t, 404> idx_ {};
    std::array<double, 404> values_ {};
    double sampleRate_ = 48000.0, gainScale_ = 1.0;
    int maxBlock_ = 0;
    std::array<Band, equalizer::kBands> bands_;
    std::vector<double> wl_, wr_;
    double autoSmoothed_ = 0.0, lastGain_ = 1.0;
    const float *keyL_ = nullptr, *keyR_ = nullptr;
    std::array<std::atomic<float>, equalizer::kNumMeters> meters_ {};
};
} // namespace neptune::core
