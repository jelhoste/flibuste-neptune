#pragma once
// « Microdécale » : effet de micro-décalage de hauteur dans l'esprit du preset MICROPITCHSHIFT de l'Eventide H3000 (algorithme « Layered Shift » : un son épaissi et
// élargi « sans ajouter de couleur »). Deux voies, gauche et droite, chacune avec son décaleur de hauteur (quelques cents vers le haut d'un côté, vers le bas de
// l'autre), son retard et son feedback qui repasse par le décaleur, plus une prise de retard du signal sec ; niveaux (négatifs = phase inversée) et panoramiques des
// quatre sources, longueur de raccord du décaleur (« splice »), modulation de hauteur.
// Réglage de départ (valeurs NON officielles, d'un message de forum : gauche +9 cents sans retard, droite -9 cents avec 25 ms) : voir docs/passage-de-relais.md.
// Le décaleur est un moteur à raccords avec recherche du meilleur point de raccord (SpliceShifter.h), écrit pour ce projet : aucun code d'Eventide ni de Soundtoys.
// Le module rend le signal traité seul ; le mélange avec le sec, le gain d'entrée et de sortie sont ceux, communs, du ModuleRunner.
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/SpliceShifter.h"
#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace microdecale
{
constexpr ParamId mode = 100;                          // 0 stéréo (chaque voie écoute son canal), 1 mono (les deux écoutent la somme)
constexpr ParamId shiftL = 101, shiftR = 102;          // cents, -1200..1200
constexpr ParamId delayL = 103, delayR = 104;          // ms, 0..1400
constexpr ParamId feedbackL = 105, feedbackR = 106;    // %, -90..90
constexpr ParamId splice = 107;                        // ms, 5..100 : longueur de raccord du décaleur
constexpr ParamId levelShiftL = 108, levelShiftR = 109, levelDryL = 110, levelDryR = 111;   // %, -100..100
constexpr ParamId panShiftL = 112, panShiftR = 113, panDryL = 114, panDryR = 115;           // %, -100..100
constexpr ParamId mod = 116, modRate = 117, modWave = 118;   // profondeur (0..100 % de 25 cents), vitesse (Hz), forme (0 sinus, 1 aléatoire)

constexpr double maxDelayMs = 1400.0, maxModCents = 25.0;

enum Meter : int { kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kNumMeters };
} // namespace microdecale

class MicrodecaleModule : public StockModule
{
public:
    MicrodecaleModule();

    std::string_view typeId() const noexcept override { return "microdecale"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;
    double defaultMixPercent() const noexcept override { return 100.0; }

    int numMeters() const noexcept override { return microdecale::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < microdecale::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }
    // Retard moyen du décaleur (ms) pour la longueur de raccord donnée : utile à l'affichage.
    static double shifterDelayMs (double spliceMs) noexcept
    {
        const double s = spliceMs < 5.0 ? 5.0 : spliceMs > 100.0 ? 100.0 : spliceMs;
        const double search = 0.25 * s < 6.0 ? 0.25 * s : 6.0;
        return search + 2.0 + 0.5 * s;
    }

private:
    struct Voice
    {
        SpliceShifter shifter;
        std::vector<float> delayBuf, dryBuf;
        std::size_t pos = 0;
        double delaySm = 0.0, feedbackSm = 0.0, shiftSm = 0.0, fbState = 0.0;
        double lfoPhase = 0.0, randFrom = 0.0, randTo = 0.0, randPos = 0.0;
    };
    double param (ParamId id) const noexcept { return values_[static_cast<std::size_t> (id - 100)]; }

    static constexpr std::size_t kIds = 19;
    std::array<std::size_t, kIds> idx_ {};
    std::array<double, kIds> values_ {};
    std::size_t idxIn_ = ParameterSet::npos, idxOut_ = ParameterSet::npos, idxMix_ = ParameterSet::npos;

    double sampleRate_ = 48000.0;
    std::size_t bufSize_ = 0;
    std::array<Voice, 2> voices_;
    std::array<double, 8> gainSm_ {};            // niveaux (4) et panoramiques (4) lissés
    double lastSplice_ = -1.0;
    bool smoothInit_ = false;
    std::uint32_t noise_ = 22222u;
    std::array<std::atomic<float>, microdecale::kNumMeters> meters_ {};
    float peakHold_[4] = { 0, 0, 0, 0 };
};
} // namespace neptune::core
