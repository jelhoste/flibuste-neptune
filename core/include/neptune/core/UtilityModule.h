#pragma once
// Utilitaire (spec-modules §8) : tranche de console minimale et outils stéréo. Sans latence, sauf le délai d'alignement voulu
// par l'utilisateur (non compensé par l'hôte : c'est un décalage choisi, par exemple entre deux micros).
//
// Chaîne de traitement (ordre réel) :
//   trim d'entrée (ModuleRunner) -> décodage M/S (entrée M/S) -> inversion de phase L/R -> échange L<->R -> délai -> filtre DC
//   -> largeur -> somme mono -> pan (simple ou double) -> volume -> encodage M/S (sortie M/S) -> trim de sortie, mix (ModuleRunner).
// Mode « Double mono » : volume, phase, délai et DC sont propres à chaque canal ; échange, largeur, mono, pan et conversions
// M/S ne s'appliquent pas. Mode « Stéréo » : volume et délai de gauche valent pour les deux canaux (réglages liés).
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace utility
{
constexpr ParamId mode = 100;       // choix : 0 Stéréo, 1 Double mono
constexpr ParamId io = 101;         // choix : 0 Stéréo>Stéréo, 1 Stéréo>M/S, 2 M/S>Stéréo
constexpr ParamId volumeL = 102;    // dB, -100..+12
constexpr ParamId volumeR = 103;
constexpr ParamId phaseL = 104;     // bascules
constexpr ParamId phaseR = 105;
constexpr ParamId swap = 106;
constexpr ParamId mono = 107;
constexpr ParamId width = 108;      // %, 0..200
constexpr ParamId panMode = 109;    // choix : 0 Simple, 1 Double
constexpr ParamId pan = 110;        // %, -100..+100
constexpr ParamId panL = 111;       // pan du canal gauche en mode Double (défaut -100)
constexpr ParamId panR = 112;       // pan du canal droit en mode Double (défaut +100)
constexpr ParamId panLaw = 113;     // choix : 0 Balance, 1 Pan vrai (pan simple seulement)
constexpr ParamId delayL = 114;     // ms, 0..100
constexpr ParamId delayR = 115;
constexpr ParamId dc = 116;         // bascule : suppression du DC offset

constexpr double maxDelayMs = 100.0;

// Indices des valeurs d'affichage.
enum Meter : int { kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kCorrelation, kInRmsL, kInRmsR, kOutRmsL, kOutRmsR, kNumMeters };
} // namespace utility

class UtilityModule : public StockModule
{
public:
    std::string_view typeId() const noexcept override { return "utility"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;

    int numMeters() const noexcept override { return utility::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < utility::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

private:
    struct Matrix
    {
        float a = 1, b = 0, c = 0, d = 1; // [L'] = [a b] [L]
        //                                    [R']   [c d] [R]
    };
    struct Channel
    {
        std::vector<float> line;  // ligne à retard circulaire
        std::size_t pos = 0;
        float delay = 0.0f;       // retard courant, en échantillons
        float dcX = 0.0f, dcY = 0.0f;
    };

    static Matrix mul (const Matrix& x, const Matrix& y) noexcept; // x puis y : y . x
    void channelPass (Channel& ch, float* x, int n, float delayTarget, bool dcOn) noexcept;

    std::array<std::size_t, 17> idx_ {}; // index des paramètres, par (id - 100)
    double sampleRate_ = 48000.0;
    float dcR_ = 0.999f;
    Channel chL_, chR_;
    Matrix lastPre_, lastPost_;
    bool primed_ = false;
    std::vector<float> scratch_;
    float corrLL_ = 0, corrRR_ = 0, corrLR_ = 0, corrAlpha_ = 0.999f;
    std::array<std::atomic<float>, utility::kNumMeters> meters_ {};
};
} // namespace neptune::core
