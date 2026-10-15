#pragma once
// Enveloppe d'amplitude généralisée (palier 1) : Délai, Attaque, Hold, Decay, Sustain, Relâchement.
// (Décision du 2026-10-12 : un seul Hold, entre l'attaque et le decay ; le « second hold » de la roadmap est abandonné.)
//
//  - EnvelopeDescription : réglages sauvegardés dans le projet (par zone). Chaque segment a un interrupteur ; un segment
//    désactivé est sauté. Tout désactivé, ou tout à 0 ms avec un sustain à 1 = enveloppe neutre (gain constant 1).
//  - EnvelopeRuntime     : les mêmes réglages en secondes + tables de courbe précalculées (construit hors temps réel).
//  - EnvelopeRunner      : état d'une voix ; une valeur par échantillon, sans allocation.
//
// Formes de courbe (spec : linéaire, concave/convexe paramétrique, linéaire en dB) :
//   shape ∈ [-1, 1] : 0 = linéaire ; > 0 convexe (démarre vite, finit lentement) ; < 0 concave. g(x) = x^(2^(-3·shape)).
//   db = true       : variation à taux constant sur l'échelle dB (plancher -60 dB, ramené exactement à 0 en bout de course).
// Le segment reçoit une progression x ∈ [0,1] et rend la fraction g(x) du chemin parcouru entre son niveau de départ et d'arrivée.
#include <array>
#include <cstdint>

namespace neptune::core
{
struct CurveSpec
{
    double shape = 0.0;
    bool db = false;
    bool operator== (const CurveSpec& o) const noexcept { return shape == o.shape && db == o.db; }
};

struct EnvelopeSegment
{
    bool on = false;
    double ms = 0.0;
    CurveSpec curve;
    bool operator== (const EnvelopeSegment& o) const noexcept { return on == o.on && ms == o.ms && curve == o.curve; }
};

constexpr double envelopeMaxMs = 10000.0;

struct EnvelopeDescription
{
    EnvelopeSegment delay;   // retarde le début de l'attaque, pas le déclenchement du sample (spec-modules §3, délai de lecture)
    EnvelopeSegment attack;
    EnvelopeSegment hold;    // curve sans objet
    EnvelopeSegment decay;
    double sustain = 1.0;    // niveau (amplitude linéaire 0..1) atteint à la fin du decay et tenu ensuite
    EnvelopeSegment release; // Note Off en mode boucle ; par défaut actif, 50 ms (comme le fondu du jalon 1)

    // Par défaut TOUS les segments sont actifs (décision du 2026-10-12) : chaque poignée de la courbe est utilisable dès
    // le chargement. C'est sans effet audible : durées à 0 ms et sustain à 1 = enveloppe neutre ; seul le relâchement
    // (50 ms) agit, comme avant.
    EnvelopeDescription()
    {
        delay.on = attack.on = hold.on = decay.on = true;
        release.on = true;
        release.ms = 50.0;
    }
    bool operator== (const EnvelopeDescription& o) const noexcept
    {
        return delay == o.delay && attack == o.attack && hold == o.hold && decay == o.decay && sustain == o.sustain && release == o.release;
    }
};

EnvelopeDescription sanitised (EnvelopeDescription e); // temps 0..10 s (relâchement >= 1 ms), forme -1..1, niveau 0..1

// Réglages prédéfinis (des configurations de la même enveloppe, pas un autre type). Valeurs de départ à affiner à l'oreille.
enum class EnvelopePreset { Sustained, PercussionAttack, PercussionAttackDecay };
constexpr int numEnvelopePresets = 3;
EnvelopeDescription presetEnvelope (EnvelopePreset p);
const char* presetI18nKey (EnvelopePreset p) noexcept; // "envelope.preset_sustained", ...

// Fraction g(x) du chemin parcouru à la progression x ∈ [0,1] (même formule que les tables du temps réel ; sert à l'affichage).
//  kind : 0 = montée de 0 vers 1 ; 1 = descente de 1 vers `target` ; 2 = descente vers 0.
double envelopeCurveFraction (const CurveSpec& curve, int kind, double target, double x) noexcept;

// Correspondance temps <-> position d'affichage (éditeur graphique) : cubique, pour garder de la précision sur les temps courts.
double envelopeTimeFromUnit (double u) noexcept; // u ∈ [0,1] -> ms
double envelopeUnitFromTime (double ms) noexcept;

// ---- exécution ----

struct EnvelopeRuntime
{
    static constexpr int tableSize = 256; // tables de tableSize + 1 points

    bool delayOn = false, attackOn = false, holdOn = false, decayOn = false, releaseOn = true;
    double delaySec = 0, attackSec = 0, holdSec = 0, decaySec = 0, releaseSec = 0.05;
    float sustain = 1.0f;

    std::array<float, tableSize + 1> attackCurve {};  // de 0 vers 1
    std::array<float, tableSize + 1> decayCurve {};   // fraction du chemin de 1 vers `sustain`
    std::array<float, tableSize + 1> releaseCurve {}; // fraction du chemin du niveau courant vers 0

    EnvelopeRuntime(); // enveloppe neutre avec relâchement 50 ms
    explicit EnvelopeRuntime (const EnvelopeDescription& d);
};

// Temps et niveau de sustain à utiliser à la place de ceux des tables (paramètres de piste automatisables). Délai, maintien, courbes
// et interrupteurs de segment restent ceux de l'enveloppe.
struct EnvelopeTiming
{
    double attackSec = 0.0, decaySec = 0.0, releaseSec = 0.05;
    float sustain = 1.0f;
    static EnvelopeTiming of (const EnvelopeRuntime& e) noexcept
    {
        EnvelopeTiming t;
        t.attackSec = e.attackSec; t.decaySec = e.decaySec; t.releaseSec = e.releaseSec; t.sustain = e.sustain;
        return t;
    }
};

class EnvelopeRunner
{
public:
    // Démarre au premier segment actif. `timing` (facultatif) remplace attaque, décroissance, relâchement et sustain de `env` pour
    // toute la note (pris au Note On).
    void begin (const EnvelopeRuntime& env, double sampleRate, const EnvelopeTiming* timing = nullptr) noexcept;
    // Note Off : relâchement depuis le niveau courant. Sans relâchement actif : fondu de 2 ms (anti-clic). `timing` (facultatif) :
    // seul son temps de relâchement est repris (la valeur au moment du Note Off).
    void noteOff (const EnvelopeRuntime& env, double sampleRate, const EnvelopeTiming* timing = nullptr) noexcept;
    // Valeur du prochain échantillon (puis avance d'un échantillon).
    float next (const EnvelopeRuntime& env, double sampleRate) noexcept;
    // Avance de `frames` échantillons d'un coup (enveloppes lues à cadence réduite, ex. modulation de filtre toutes les 16 frames).
    // Aux changements de segment le reste de pas est perdu (erreur < `frames` échantillons). Lire ensuite value().
    void advance (const EnvelopeRuntime& env, double sampleRate, double frames) noexcept { step (env, sampleRate, frames); }

    bool finished() const noexcept { return stage_ == Stage::Done; }
    bool releasing() const noexcept { return stage_ == Stage::Release; }
    float value() const noexcept { return value_; }

private:
    enum class Stage { Delay, Attack, Hold, Decay, Sustain, Release, Done };
    void step (const EnvelopeRuntime& env, double sampleRate, double frames) noexcept;
    void enterFirstStage (const EnvelopeRuntime& env) noexcept;
    void enterAfterHold (const EnvelopeRuntime& env) noexcept;

    Stage stage_ = Stage::Done;
    EnvelopeTiming t_;
    double pos_ = 0.0;         // échantillons écoulés dans le segment
    double releaseFrames_ = 1.0;
    float releaseStart_ = 0.0f;
    float value_ = 0.0f;
    bool declick_ = false;     // relâchement de secours (segment désactivé) : linéaire
};
} // namespace neptune::core
