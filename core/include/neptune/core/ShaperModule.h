#pragma once
// Shaper de transitoires large bande (« La Flèche », série NOVA) : renforce ou adoucit l'attaque et le sustain d'un signal, par deux
// suiveurs d'enveloppe (rapide / lent) lus sur une clé de détection (le signal ou un sidechain filtré par un égaliseur de détection).
// Description de référence : design/nova/docs/NOVA_TransientShaper_params_v0.2.md et NOVA_Common_modules_v0.1.md (sidechain `sc_*`).
// Les identifiants du fichier de paramètres (`shp_*`, `sc_*`, `meter_*`) correspondent aux constantes ci-dessous.
//
// Chaîne : entrée (trim du ModuleRunner) -> mode de canaux (Stereo / Dual Mono / M-S) -> clé (interne / externe / mélange, gain, EQ de
// détection) -> suiveurs -> gain en dB lissé -> appliqué au signal retardé du lookahead, à la cadence suréchantillonnée.
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/Biquad.h"
#include "neptune/core/Oversampler.h"
#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace shaper
{
constexpr ParamId attackGain1 = 100, attackGain2 = 101, sustainGain1 = 102, sustainGain2 = 103;   // %, -100..+100
constexpr ParamId levelDependency = 104;   // %, -100..+100
constexpr ParamId levelThreshold = 105;    // dB, -60..0
constexpr ParamId levelKnee = 106;         // dB, 0..24
constexpr ParamId attackTime = 107;        // ms, 0,1..100 (log)
constexpr ParamId sustainTime = 108;       // ms, 10..1000 (log)
constexpr ParamId shapeSmoothing = 109;    // %, -50..+50 (0 = auto)
constexpr ParamId range = 110;             // dB, 6..24
constexpr ParamId stereoLink = 111;        // %, 0..100
constexpr ParamId channelMode = 112;       // choix : 0 Stereo, 1 Dual Mono, 2 M-S
constexpr ParamId delta = 113;             // bascule : n'écoute que la différence
constexpr ParamId lookahead = 114;         // ms, 0..10 (latence)
constexpr ParamId oversampling = 115;      // choix : 0 None, 1 2x, 2 4x (latence)
constexpr ParamId noiseFloor = 116;        // dBFS, -100..-40
constexpr ParamId detMode = 117;           // choix : 0 Peak, 1 RMS
constexpr ParamId detFastRatio = 118;      // x, 3..30 (log)

constexpr ParamId scEnable = 120;          // bascule : sidechain actif (sinon la clé est le signal lui-même)
constexpr ParamId scSource = 121;          // choix : 0 Internal, 1 External, 2 Mix (le mode MIDI viendra plus tard)
constexpr ParamId scGain = 122;            // dB, -24..+24 (Internal et External)
constexpr ParamId scIntVol = 123;          // dB (Mix : volume de la voie interne)
constexpr ParamId scExtVol = 124;          // dB (Mix : volume de la voie externe)
constexpr ParamId scPhaseInt = 125, scPhaseExt = 126;   // bascules (Mix)
constexpr ParamId scExtMs = 127;           // bascule : l'entrée externe est déjà M / S
constexpr ParamId scListen = 128;          // bascule : on entend la clé
constexpr ParamId scEqOn = 129, scAnalyzerOn = 130;
constexpr ParamId scHpFreq = 131, scHpSlope = 132, scLpFreq = 133, scLpSlope = 134;   // Hz / dB par octave (6..48, pas de 6)
constexpr ParamId scBand1Freq = 135, scBand1Gain = 136, scBand1Q = 137;
constexpr ParamId scBand2Freq = 138, scBand2Gain = 139, scBand2Q = 140;
constexpr ParamId scBand3Freq = 141, scBand3Gain = 142, scBand3Q = 143;

constexpr ParamId meterRmsMs = 150;        // ms, 10..3000 (réglage d'affichage enregistré avec le preset)
constexpr ParamId meterPeakDecay = 151;    // dB/s, 5..300
constexpr ParamId displaySpan = 152;       // %, 50..1600 : durée affichée par le graphe WAVE, en pour-cent de 1,6 s (défaut 940 % = 15 s)

constexpr double maxLookaheadMs = 10.0;

// Valeurs d'affichage (lues par l'interface sans verrou).
enum Meter : int
{
    kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kInRmsL, kInRmsR, kOutRmsL, kOutRmsR,
    kGainL, kGainR,                 // gain appliqué, dB (bipolaire)
    kNumMeters
};

// Une colonne de l'historique affiché (1,6 s sur kHistoryColumns colonnes).
constexpr int kHistoryColumns = 1024;
constexpr double kHistorySeconds = 1.6;
struct Column
{
    float inPeak = 0.0f, outPeak = 0.0f;   // crêtes linéaires de la colonne (signal d'entrée / de sortie)
    float fastDb = -120.0f, slowDb = -120.0f, gainDb = 0.0f;
    float fastLin = 0.0f, slowLin = 0.0f;   // les mêmes enveloppes en amplitude (calculées une fois par colonne, pas à chaque image)
};
constexpr int kKeyRing = 4096;             // dernières valeurs de la clé (somme des canaux), pour l'analyseur de spectre
} // namespace shaper

class ShaperModule : public StockModule
{
public:
    ShaperModule();

    std::string_view typeId() const noexcept override { return "shaper"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;
    int latencySamples (const ParameterSet&) const noexcept override;
    int maxLatencySamples (double sampleRate) const noexcept override;
    bool wantsKey() const noexcept override { return true; }
    void setKey (const float* left, const float* right) noexcept override { keyL_ = left; keyR_ = right; }

    int numMeters() const noexcept override { return shaper::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < shaper::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }
    // Historique : copie les kHistoryColumns dernières colonnes, de la plus ancienne à la plus récente (lecture sans verrou : une colonne en
    // cours d'écriture peut être incohérente, sans conséquence pour l'affichage).
    void copyHistory (shaper::Column* dst) const noexcept;
    // Avancement (0..1) de la colonne en cours d'accumulation : l'interface le soustrait au défilement pour qu'il soit continu.
    double columnsPerSecond() const noexcept { return sampleRate_ / static_cast<double> (std::max (1, colSamples_)); }   // vitesse du défilement
    std::uint32_t historyHead() const noexcept { return historyHead_.load (std::memory_order_acquire); }   // nombre total de colonnes écrites
    float historyPhase() const noexcept { return historyPhase_.load (std::memory_order_relaxed); }
    void copyKey (float* dst, int n) const noexcept;    // les n dernières valeurs de la clé (n <= kKeyRing)
    // Réponse (module) de l'égaliseur de détection à la fréquence f, avec les mêmes coefficients que le DSP.
    double detectionResponseDb (const ParameterSet&, double f) const noexcept;

private:
    struct Channel
    {
        Oversampler os;
        std::vector<float> delay, dryDelay;   // lookahead ; signal sec aligné sur la latence totale (DELTA)
        std::size_t delayPos = 0, dryPos = 0;
        std::vector<float> gainRing;          // gain linéaire à la cadence haute, retardé du retard de l'interpolateur
        std::size_t gainPos = 0;
        ButterworthCascade hp, lp;
        std::array<Biquad, 3> band;
        double fastEnv = 0.0, slowEnv = 0.0;
        double gainDb = 0.0, lastGainLin = 1.0;
        float atkG = 0.0f, susG = 0.0f;       // gains lissés
    };
    void designKeyFilters (const ParameterSet&) noexcept;
    double param (ParamId id) const noexcept { return values_[static_cast<std::size_t> (id - 100)]; }
    void readParams (const ParameterSet&) noexcept;

    std::array<std::size_t, 53> idx_ {};
    std::array<double, 53> values_ {};
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0, lookaheadSamples_ = 0, factor_ = 4;
    std::array<Channel, 2> ch_;
    std::vector<float> in0_, in1_, key0_, key1_, work0_, work1_, gain0_, gain1_, hiGain_;
    const float *keyL_ = nullptr, *keyR_ = nullptr;
    // gains de commande lissés (une pole par échantillon)
    double sLevel_ = 0.0, sThr_ = -24.0, sKnee_ = 6.0, sRange_ = 15.0, sLink_ = 1.0;
    int designCountdown_ = 0;
    std::array<double, 20> lastDesign_ {};
    bool designed_ = false;

    std::array<std::atomic<float>, shaper::kNumMeters> meters_ {};
    float peakHold_[4] = { 0, 0, 0, 0 };
    double rmsSum_[4] = { 0, 0, 0, 0 };
    int rmsCount_ = 0;
    std::vector<shaper::Column> history_;
    std::atomic<std::uint32_t> historyHead_ { 0 };
    shaper::Column acc_;
    int accCount_ = 0, colSamples_ = 1;
    std::atomic<float> historyPhase_ { 0.0f };
    std::vector<float> keyRing_;
    std::atomic<std::uint32_t> keyHead_ { 0 };
};
} // namespace neptune::core
