#pragma once
// « Convolution » : chargeur de réponses impulsionnelles (cabine, salle, ressort...) à latence nulle, avec EQ 3 bandes + passe-haut / passe-bas, largeur stéréo,
// pré-délai et vu-mètres (crête, RMS, LUFS). Moteur : tête FIR directe + partitions FFT non uniformes (Gardner), écrit pour le projet LowLatConv de l'auteur
// (conv/*.hpp, PFFFT pour la FFT). Le module rend le signal traité seul ; gains d'entrée / sortie et mélange dry/wet sont ceux, communs, du ModuleRunner.
//
// Le fichier d'IR n'est pas un paramètre numérique : c'est l'état « texte » du module (setIrPath / irPath), sauvegardé avec l'insert ou le projet.
// Un thread de chargement lit le fichier, prépare l'IR (rééchantillonnage, rognage, enveloppe...), construit le moteur puis le fait basculer par un fondu ;
// le thread audio n'alloue, ne verrouille et ne libère jamais rien.
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "neptune/core/StockModule.h"
#include "neptune/core/conv/ir_processor.hpp"
#include "neptune/core/conv/processor.hpp"
#include "neptune/core/conv/stereo_engine.hpp"

namespace neptune::core
{
namespace convolution
{
constexpr ParamId width = 100;          // %, 0..200 (voie traitée)
constexpr ParamId preDelay = 101;       // ms, 0..2000 (voie traitée)
constexpr ParamId eqOn = 102;
// bandes d'EQ : 3 groupes de 5 (actif, type, fréquence, gain, Q)
constexpr ParamId bandBase = 103;
constexpr ParamId bandParam (int band, int k) { return bandBase + static_cast<ParamId> (5 * band + k); }   // 103..117
enum BandField : int { kOn = 0, kType = 1, kFreq = 2, kGain = 3, kQ = 4 };
constexpr ParamId hpOn = 118, hpFreq = 119, hpSlope = 120, lpOn = 121, lpFreq = 122, lpSlope = 123;
// réglages qui reconstruisent l'IR (non automatisables)
constexpr ParamId inputMode = 124;      // 0 stéréo, 1 somme, 2 gauche, 3 droite
constexpr ParamId stretch = 125;        // %, 50..200
constexpr ParamId autoTrim = 126;
constexpr ParamId begin = 127;          // ms, 0..500 (sur l'axe de l'IR rognée)
constexpr ParamId end = 128;            // ms, 0..10000 (0 = jusqu'au bout)
constexpr ParamId autoLen = 129;
constexpr ParamId attack = 130;         // ms, 0..500
constexpr ParamId decay = 131;          // dB, 0..60
constexpr ParamId reverse = 132;
constexpr ParamId norm = 133;           // 0 aucune, 1 crête, 2 énergie
// réglage des vu-mètres (non automatisable)
constexpr ParamId meterRmsMs = 134;     // 10..3000
constexpr ParamId meterPeakDecay = 135; // dB/s, 5..300
constexpr std::size_t numParams = 36;   // identifiants 100..135

enum Status : int { kNoIr = 0, kLoading = 1, kLoaded = 2, kError = 3 };
// Vu-mètres : IN = entrée du module, OUT = sortie de la voie traitée (avant le mélange dry/wet commun).
enum Meter : int
{
    kPeakInL = 0, kPeakInR, kPeakOutL, kPeakOutR,
    kPeakMaxInL, kPeakMaxInR, kPeakMaxOutL, kPeakMaxOutR,
    kRmsInL, kRmsInR, kRmsOutL, kRmsOutR,
    kLufsMIn, kLufsMOut, kLufsSIn, kLufsSOut,
    kNumMeters
};
} // namespace convolution

class ConvolutionModule : public StockModule
{
public:
    ConvolutionModule();
    ~ConvolutionModule() override;

    std::string_view typeId() const noexcept override { return "convolution"; }
    bool declareParams (ParameterSet&, std::string* error) const override;
    void bind (const ParameterSet&) override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const ParameterSet& params) noexcept override;

    int numMeters() const noexcept override { return convolution::kNumMeters; }
    float meter (int i) const noexcept override
    {
        return i >= 0 && i < convolution::kNumMeters ? meters_[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

    // ---- état texte : le fichier d'IR (chemin absolu UTF-8, vide = aucune IR)
    bool hasStateString() const noexcept override { return true; }
    std::string stateString() const override { return irPath(); }
    void setStateString (const std::string& s) override { setIrPath (s); }
    void setIrPath (const std::string& path);
    std::string irPath() const;

    // ---- pour l'interface (lecture sans verrou, sauf rawIr)
    int irStatus() const noexcept { return status_.load (std::memory_order_relaxed); }
    double irLengthMs() const noexcept { return lengthMs_.load (std::memory_order_relaxed); }
    std::uint32_t irVersion() const noexcept { return irVersion_.load (std::memory_order_relaxed); }   // change quand l'IR brute ou les options changent : redessiner
    std::shared_ptr<const llc::IRBuffer> rawIr() const;                                                // IR lue dans le fichier, avant toute retouche (nul si aucune)
    llc::IROptions irOptions() const;                                                                  // retouches actuelles, à la cadence de travail
    llc::InputMode inputModeValue() const;
    void resetPeakMax() noexcept { resetPeak_.store (true, std::memory_order_release); }
    // Réponse en fréquence (dB) de l'EQ pour les réglages donnés (0 si l'EQ est coupé) : pour la courbe de l'interface, hors temps réel.
    static double eqResponseDb (const ParameterSet&, double freq, double sampleRate);
    static double eqBandResponseDb (const ParameterSet&, int band, double freq, double sampleRate);   // 0..2 : bandes, 3 : passe-haut, 4 : passe-bas

private:
    double value (ParamId id) const noexcept
    {
        const auto i = idx_[static_cast<std::size_t> (id - 100)];
        return params_ != nullptr && i != ParameterSet::npos ? params_->getAt (i) : 0.0;
    }
    llc::IROptions irOptionsFrom (const std::array<double, convolution::numParams>& v) const;
    void pushProcessorParams (const std::array<double, convolution::numParams>& v, bool snap) noexcept;
    void workerLoop();
    void setStatus (int s, double ms) noexcept { status_.store (s, std::memory_order_relaxed); lengthMs_.store (ms, std::memory_order_relaxed); }

    const ParameterSet* params_ = nullptr;
    std::array<std::size_t, convolution::numParams> idx_ {};
    std::array<double, convolution::numParams> last_ {};
    bool haveLast_ = false;
    std::atomic<double> sr_ { 48000.0 };
    llc::ConvolutionProcessor proc_;
    std::array<std::atomic<float>, convolution::kNumMeters> meters_ {};

    mutable std::mutex pathMutex_, rawMutex_;
    std::string irPath_;
    std::shared_ptr<const llc::IRBuffer> raw_;
    std::atomic<std::uint32_t> irStamp_ { 0 }, irVersion_ { 0 };
    std::atomic<int> status_ { convolution::kNoIr };
    std::atomic<double> lengthMs_ { 0.0 };
    std::atomic<bool> resetPeak_ { false }, quit_ { false };
    std::atomic<bool> builtOnce_ { false };   // le thread de chargement a traité au moins une demande : une réactivation à la même cadence n'a pas à refaire l'IR
    std::thread worker_;
};
} // namespace neptune::core
