#pragma once
// Chorus stéréo (« NOVA Chorus », série NOVA) : une ligne à retard modulée par un LFO par canal (1 à 4 voix, largeur stéréo, feedback, caractère « BBD »,
// filtres passe-haut et passe-bas de la voie traitée, synchronisation au tempo), plus un MODE TAL qui reproduit la structure du chorus de TAL-NoiseMaker /
// TAL-Chorus-LX : deux unités I et II (LFO triangulaires de 0,5 et 0,83 Hz par défaut, opposés de phase entre gauche et droite), retard central de 2,8 ms
// modulé de ± 2,1 ms, passe-bas à un pôle, filtre anti-continu, et sortie = signal sec + 1,4 x signal traité.
// Description de référence : design/nova/docs/NOVA_Chorus_params_v0.1.md. Le mode TAL est écrit d'après la LECTURE du comportement des dépôts
// soerenbnoergaard/talchorus et SpotlightKid/ykchorus (GPL version 2, donc incompatible avec cette licence : aucun code n'en est copié, tout est réécrit ici).
// Le module rend le signal traité seul (mode Standard) ou sec + 1,4 x traité (mode TAL) ; le mélange sec / traité, le gain d'entrée et le gain de sortie sont
// ceux, communs, du ModuleRunner (stock::mix, stock::inputTrim, stock::outputTrim).
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/Biquad.h"
#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace chorus
{
constexpr ParamId mode = 100;              // 0 Standard, 1 TAL
constexpr ParamId rate = 101, sync = 102, syncDiv = 103, depth = 104, delayMs = 105, lfoShape = 106, voices = 107;
constexpr ParamId width = 108, feedback = 109;
constexpr ParamId lowCut = 110, lowCutSlope = 111, lowCutOn = 112, highCut = 113, highCutSlope = 114, highCutOn = 115, bbd = 116, bbdOn = 117;
constexpr ParamId interp = 118, lfoStart = 119;                     // Settings : 0 linéaire, 1 cubique ; 0 libre, 1 à chaque lecture
constexpr ParamId talI = 120, talII = 121, talRate1 = 122, talRate2 = 123;   // mode TAL : unités activées et leurs vitesses (Hz)

constexpr int kMaxVoices = 4, kDivisions = 11;
// Durée d'un cycle de LFO en temps (noires) pour chaque division : 1/1, 1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16.
constexpr double divisionBeats (int index)
{
    constexpr double beats[kDivisions] = { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25 };
    return beats[index < 0 ? 0 : index >= kDivisions ? kDivisions - 1 : index];
}
constexpr const char* divisionNames[kDivisions] = { "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16" };

// Constantes du mode TAL (retard central et excursion, en ms).
constexpr double talCentreMs = 2.8, talSwingMs = 2.1;

enum Meter : int { kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kLfoPhase, kNumMeters };   // kLfoPhase : phase du LFO gauche (voix 1), 0..1
} // namespace chorus

class ChorusModule : public StockModule
{
public:
    ChorusModule();

    std::string_view typeId() const noexcept override { return "chorus"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;
    double defaultMixPercent() const noexcept override { return 50.0; }

    int numMeters() const noexcept override { return chorus::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < chorus::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }
    // Tempo de l'hôte (battements par minute) pour SYNC ; 0 ou négatif : 120. Et état du transport (LFO « à chaque lecture »). N'importe quel fil.
    void setTempo (double bpm) noexcept { tempo_.store (bpm, std::memory_order_relaxed); }
    double tempo() const noexcept { return tempo_.load (std::memory_order_relaxed); }
    void setPlaying (bool playing) noexcept { playing_.store (playing, std::memory_order_relaxed); }

private:
    struct Voice
    {
        std::vector<float> buf;
        std::size_t pos = 0;
        Biquad bbd;                         // passe-bas du second ordre (caractère BBD)
        ButterworthCascade hp, lp;
        double lastOut = 0.0;               // sortie de la voix (après BBD et filtres), réinjectée par le feedback
    };
    struct TalLine
    {
        std::vector<float> buf;
        std::size_t pos = 0;
        double lp = 0.0, dcIn = 0.0, dcOut = 0.0;
    };
    double param (ParamId id) const noexcept { return values_[static_cast<std::size_t> (id - 100)]; }
    void designFilters (double fs) noexcept;
    void resetPhases() noexcept;

    static constexpr std::size_t kIds = 24;
    std::array<std::size_t, kIds> idx_ {};
    std::array<double, kIds> values_ {};
    std::size_t idxIn_ = ParameterSet::npos, idxOut_ = ParameterSet::npos, idxMix_ = ParameterSet::npos;

    double sampleRate_ = 48000.0;
    std::size_t bufSize_ = 0;
    std::array<std::array<Voice, chorus::kMaxVoices>, 2> voices_;
    std::array<std::array<TalLine, 2>, 2> tal_;             // [unité][canal]
    double phase_ = 0.0, talPhase_[2] = { 0.0, 0.0 };       // phases maîtres (cycles)
    double depthSm_ = 0.5, delaySm_ = 7.0, widthSm_ = 1.0, fbSm_ = 0.0, rateSm_ = 0.5, bbdSm_ = 0.0, tal1On_ = 0.0, tal2On_ = 0.0;
    double designed_[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::uint32_t noise_ = 12345u;
    bool wasPlaying_ = false, smoothInit_ = false;
    std::atomic<double> tempo_ { 0.0 };
    std::atomic<bool> playing_ { false };
    std::array<std::atomic<float>, chorus::kNumMeters> meters_ {};
    float peakHold_[4] = { 0, 0, 0, 0 };
};
} // namespace neptune::core
