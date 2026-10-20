#pragma once
// Delay à huit lignes (« NOVA Delay », série NOVA) dans l'esprit de ReaDelay : chaque ligne a son temps (en ms ou en division du tempo), son niveau, son
// panoramique, son décalage droit / gauche, son feedback, ses filtres passe-haut et passe-bas, son inversion de phase et sa modulation de temps.
// Réglages communs : routage parallèle ou en cascade, feedback global, ping-pong, saturation de la boucle, freeze, ducking, largeur.
// Description de référence : design/nova/docs/NOVA_Delay_params_v0.1.md. Le module ne produit que le signal retardé (« mouillé ») : le mélange avec le
// signal sec, le gain d'entrée et le gain de sortie sont ceux, communs, du ModuleRunner (stock::mix, stock::inputTrim, stock::outputTrim).
// Changement de temps : Glide (le temps glisse, la hauteur des échos bouge comme sur un delay à bande) ou Crossfade (fondu entre l'ancien et le nouveau temps,
// sans variation de hauteur). Décalage de hauteur (PITCH) : décaleur à deux grains croisés (fenêtres de Hann) dans la ligne, avant les filtres, dans la boucle de feedback (chaque
// répétition est décalée de nouveau : effet « shimmer »). Pas encore de changement de temps par fondu (Crossfade).
#include <array>
#include <atomic>
#include <vector>

#include "neptune/core/Biquad.h"
#include "neptune/core/StockModule.h"

namespace neptune::core
{
namespace delay
{
// Réglages communs.
constexpr ParamId sync = 100, routing = 101, freeze = 102, duckAmount = 103, duckThreshold = 104, duckRelease = 105;
constexpr ParamId feedbackGlobal = 106, pingPong = 107, drive = 108, width = 109;
// Réglages de Settings (non automatisables).
constexpr ParamId glideMs = 110, safetyLimiter = 111, pitchQuality = 112, timeMode = 113;
// pitchQuality : 0 Standard (grain de 40 ms), 1 High (80 ms) ; timeMode : 0 Glide, 1 Crossfade (glideMs sert aux deux)

constexpr int kLines = 8;
constexpr ParamId lineBase = 200, lineStride = 16;
constexpr ParamId lineParam (int line, int k) { return lineBase + static_cast<ParamId> (line) * lineStride + static_cast<ParamId> (k); }   // line : 0..7
enum LineParam : int { kOn = 0, kSolo, kTime, kDiv, kLevel, kPan, kOffset, kFeedback, kPitch, kLowCut, kHighCut, kInvert, kModDepth, kModRate, kLineParams };

constexpr double maxTimeMs = 4000.0, maxOffsetMs = 100.0, maxModMs = 8.0;
constexpr int kDivisions = 12;
// Longueur d'une division en temps (noires) : 1/1, 1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16, 1/16T.
constexpr double divisionBeats (int index)
{
    constexpr double beats[kDivisions] = { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
    return beats[index < 0 ? 0 : index >= kDivisions ? kDivisions - 1 : index];
}
constexpr const char* divisionNames[kDivisions] = { "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16T" };

enum Meter : int { kInPeakL = 0, kInPeakR, kOutPeakL, kOutPeakR, kDuckDb, kNumMeters };   // kDuckDb : gain de ducking courant (0 .. -24 dB)
} // namespace delay

class DelayModule : public StockModule
{
public:
    DelayModule();

    std::string_view typeId() const noexcept override { return "delay"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;
    double defaultMixPercent() const noexcept override { return 35.0; }

    int numMeters() const noexcept override { return delay::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < delay::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }
    // Tempo de l'hôte (battements par minute) pour SYNC ; 0 ou négatif : 120. Appelable depuis n'importe quel fil.
    void setTempo (double bpm) noexcept { tempo_.store (bpm, std::memory_order_relaxed); }
    double tempo() const noexcept { return tempo_.load (std::memory_order_relaxed); }

private:
    struct Line
    {
        std::array<std::vector<float>, 2> buf;
        std::size_t pos = 0;
        std::array<Biquad, 2> hp, lp;
        std::array<double, 2> y { 0.0, 0.0 };          // sortie de la ligne à cet échantillon, après ses filtres (réinjectée dans la ligne)
        std::array<double, 2> src { 0.0, 0.0 };        // entrée de la ligne à cet échantillon (signal d'entrée, ou sortie de la ligne précédente en cascade)
        double timeSmp = 0.0, level = 1.0, panL = 1.0, panR = 1.0, fb = 0.0, offsetSmp = 0.0, lowHz = 20.0, highHz = 20000.0, modDepth = 0.0, modRate = 0.5;
        double phase = 0.0, hpDesigned = -1.0, lpDesigned = -1.0;
        std::array<std::vector<float>, 2> shifter;   // entrée du décaleur de hauteur (grain de 80 ms au plus)
        std::size_t shPos = 0;
        double timeOld = 0.0, xf = 1.0;                // changement de temps par fondu : ancien temps, avancement du fondu (1 = terminé)
        double pitchSm = 0.0, pitchAct = 0.0, shPhi = 0.0;   // hauteur lissée (demi-tons), part du signal décalé (fondu), phase des deux têtes
        bool shEngaged = false;
        bool active = false;
    };
    double param (ParamId id) const noexcept { return values_[static_cast<std::size_t> (id - 100)]; }
    double lineValue (int line, int k) const noexcept { return param (delay::lineParam (line, k)); }

    static constexpr std::size_t kIds = 226;   // identifiants 100..325
    std::array<std::size_t, kIds> idx_ {};
    std::array<double, kIds> values_ {};
    std::size_t idxIn_ = ParameterSet::npos, idxOut_ = ParameterSet::npos, idxMix_ = ParameterSet::npos;

    double sampleRate_ = 48000.0;
    std::size_t bufSize_ = 0, shifterSize_ = 0;
    std::array<Line, delay::kLines> lines_;
    double freezeSm_ = 0.0, fbGlobalSm_ = 0.0, duckEnv_ = 0.0, duckGain_ = 1.0, duckAmountSm_ = 0.0;
    std::atomic<double> tempo_ { 0.0 };
    std::array<std::atomic<float>, delay::kNumMeters> meters_ {};
    float peakHold_[4] = { 0, 0, 0, 0 };
};
} // namespace neptune::core
