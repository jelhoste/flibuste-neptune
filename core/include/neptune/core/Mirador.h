#pragma once
// Mirador (nom retenu par Jean le 2026-10-30) : plugin de mesure et de visualisation, transparent (l'audio passe sans changement). Cœur portable
// (aucune dépendance à JUCE) ; l'adaptateur relie l'hôte (paramètres, position de lecture) et l'interface dessine.
//
//  - Sonie : LoudnessMeter (libebur128) : momentanée, à court terme, intégrée, plage (LRA), crête vraie ; PLR et PSR en sont déduits ici.
//  - Niveau : crête avec retombée et maintien, valeur efficace (RMS), facteur de crête, voyants d'écrêtage.
//  - Stéréo : corrélation de phase, équilibre, largeur (rapport milieu / côtés).
//  - Signal pour l'interface : AnalyzerRing (spectre, vectorscope), rempli seulement quand un éditeur est ouvert.
// Le fil audio ne fait que des accumulations simples et copie le signal ; libebur128 et les FFT tournent ailleurs.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "neptune/core/AnalyzerRing.h"
#include "neptune/core/Loudness.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::mirador
{
constexpr const char* moduleId = "mirador";

enum Target : int { kTargetEbu = 0, kTargetAtsc, kTargetPodcast, kTargetStreaming, kTargetCustom, kNumTargets };
enum RmsWindow : int { kRms300ms = 0, kRms1s, kRms3s, kNumRmsWindows };

// ---- identifiants (stables : ne changent jamais une fois publiés)
constexpr ParamId target = 100, customTarget = 101, tolerance = 102, historyMinutes = 103, peakHold = 104, peakFall = 105, rmsWindow = 106, autoReset = 107;

// Hors temps réel.
bool addParams (ParameterSet& set, std::string* error = nullptr);

const char* targetName (int t) noexcept;                  // « -23 LUFS (EBU R128) »...
double targetLufs (int t, double custom) noexcept;        // valeur en LUFS du choix de cible
constexpr float kNoValue = -200.0f;                       // « -inf » (comme LoudnessMeter)

// Mesures dérivées de la lecture de sonie. kNoValue tant qu'une des deux grandeurs manque.
float plr (const LoudnessMeter::Reading&) noexcept;       // crête vraie - sonie intégrée
float psr (const LoudnessMeter::Reading&) noexcept;       // crête vraie - sonie à court terme

enum class TargetState { Unknown, Under, OnTarget, Over };
// Compare la sonie intégrée à la cible, avec la tolérance (en LU). Unknown tant que l'intégrée vaut « -inf ».
TargetState targetState (float integratedLufs, double targetLufsValue, double toleranceLu) noexcept;

// Photographie des mesures de niveau et de stéréo (dBFS pour les niveaux).
struct Levels
{
    float peakL = kNoValue, peakR = kNoValue;         // crête avec retombée
    float holdL = kNoValue, holdR = kNoValue;         // crête tenue
    float rmsL = kNoValue, rmsR = kNoValue;
    float crest = 0.0f;                               // facteur de crête (crête tenue sur la fenêtre - RMS), en dB ; 0 sans signal
    float correlation = 0.0f;                         // -1 (opposition de phase) .. +1 (mono) ; 0 sans signal
    float balance = 0.0f;                             // -1 (tout à gauche) .. +1 (tout à droite)
    float width = 0.0f;                               // 0 (mono) .. 1 (que des côtés)
    bool clipL = false, clipR = false;                // une valeur >= 0 dBFS a été vue depuis la dernière remise à zéro
};

// Historique de sonie pour la courbe défilante (thread d'interface seulement, pas de verrou).
class LoudnessHistory
{
public:
    struct Point { float momentary = kNoValue, shortTerm = kNoValue; };
    explicit LoudnessHistory (std::size_t capacity = 7200) : data_ (capacity > 0 ? capacity : 1) {}

    void setCapacity (std::size_t capacity);              // garde les points les plus récents
    std::size_t capacity() const noexcept { return data_.size(); }
    std::size_t size() const noexcept { return count_; }
    void add (float momentary, float shortTerm) noexcept;
    void clear() noexcept { count_ = 0; head_ = 0; }
    Point at (std::size_t i) const noexcept;               // 0 = le plus ancien ; i < size()

private:
    std::vector<Point> data_;
    std::size_t head_ = 0;   // prochaine case d'écriture
    std::size_t count_ = 0;
};

class MiradorEngine
{
public:
    MiradorEngine();

    ParameterSet& params() noexcept { return params_; }
    const ParameterSet& params() const noexcept { return params_; }
    bool ok() const noexcept { return ok_; }

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;                            // thread audio ou de préparation : niveaux, voyants, crêtes tenues
    void stop() { loudness_.stop(); }

    // --- thread de message
    void resetLoudness() noexcept { loudness_.reset(); }
    void resetLevels() noexcept { resetRequested_.store (true, std::memory_order_release); }   // crêtes tenues, voyants d'écrêtage, moyennes (pas la sonie)
    void resetAll() noexcept { resetRequested_.store (true, std::memory_order_release); loudness_.reset(); }
    LoudnessMeter::Reading loudness() const noexcept { return loudness_.read(); }

    // --- thread audio : ne modifie pas `left` ni `right` (`right` peut être nul : voie gauche seule, comptée aux deux). `playing` : transport de l'hôte.
    void process (const float* left, const float* right, int n, bool playing) noexcept;

    // --- interface (sans verrou)
    Levels levels() const noexcept;
    AnalyzerRing& ring() noexcept { return ring_; }
    const AnalyzerRing& ring() const noexcept { return ring_; }
    double sampleRate() const noexcept { return sr_; }

private:
    double p (std::size_t idx) const noexcept { return params_.effectiveAt (idx); }

    ParameterSet params_;
    bool ok_ = true;
    double sr_ = 48000.0;

    std::size_t iPeakHold_ = 0, iPeakFall_ = 0, iRmsWindow_ = 0, iAutoReset_ = 0;

    LoudnessMeter loudness_;
    AnalyzerRing ring_ { 1, 2, 65536 };

    // fil audio
    double msL_ = 0.0, msR_ = 0.0, sLR_ = 0.0, sMid_ = 0.0, sSide_ = 0.0;   // moyennes glissantes
    double envL_ = 0.0, envR_ = 0.0;                                          // crête avec retombée (linéaire)
    double holdL_ = 0.0, holdR_ = 0.0;                                        // crête tenue (linéaire)
    double holdLeftS_ = 0.0, holdRightS_ = 0.0;                               // temps restant avant de lâcher la crête tenue (échantillons)
    double winPeak_ = 0.0, prevWinPeak_ = 0.0;                                // plus forte crête sur la fenêtre en cours et sur la précédente (facteur de crête)
    double winLeft_ = 0.0;                                                    // échantillons restants dans la fenêtre en cours
    bool wasPlaying_ = false;
    std::atomic<bool> resetRequested_ { false };

    // publication
    std::atomic<float> peakL_ { kNoValue }, peakR_ { kNoValue }, holdLPub_ { kNoValue }, holdRPub_ { kNoValue };
    std::atomic<float> rmsLPub_ { kNoValue }, rmsRPub_ { kNoValue }, crest_ { 0.0f }, correlation_ { 0.0f }, balance_ { 0.0f }, width_ { 0.0f };
    std::atomic<bool> clipL_ { false }, clipR_ { false };
};
} // namespace neptune::core::mirador
