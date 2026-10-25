#pragma once
// Matrice de modulation d'une piste (palier 5) : des SOURCES (CC MIDI, pitch bend, aftertouch, macros, LFO, enveloppes) reliées à des
// DESTINATIONS (n'importe quel paramètre continu) par une quantité signée. La modulation est un décalage VIVANT qui s'ajoute à la valeur
// réglée sans la changer (voir ParameterSet::modOffsetAt) : le potard garde sa valeur, l'automation de l'hôte et les presets aussi.
// Cœur portable : le modèle (JSON, bornes) et l'évaluation par bloc, sans JUCE.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/MidiFilter.h"
#include "neptune/core/Parameter.h"

namespace neptune::core::modulation
{
constexpr int kLfos = 4;
constexpr int kEnvelopes = 4;
constexpr std::size_t kMaxRoutes = 32;

enum class SourceKind { None = 0, ControlChange = 1, PitchBend = 2, Aftertouch = 3, Macro = 4, Lfo = 5, Envelope = 6 };
constexpr int numSourceKinds = 7;

// index : numéro de CC (0..127) | macro (0..31) | LFO (0..3) | enveloppe (0..3) ; channel : CC, pitch bend, aftertouch (0 = n'importe quel canal, 1..16)
struct Source
{
    SourceKind kind = SourceKind::None;
    int index = 0;
    int channel = 0;
    bool operator== (const Source& o) const noexcept { return kind == o.kind && index == o.index && channel == o.channel; }
};
// Une source bipolaire (-1..1) : pitch bend et LFO ; les autres vont de 0 à 1.
bool isBipolar (SourceKind) noexcept;

enum class LfoShape { Sine = 0, Triangle = 1, SawUp = 2, SawDown = 3, Square = 4, SampleHold = 5 };
constexpr int numLfoShapes = 6;

struct LfoDesc
{
    LfoShape shape = LfoShape::Sine;
    double rateHz = 1.0;    // 0,01..50 Hz
    double phase = 0.0;     // décalage de départ, 0..1 tour
    bool operator== (const LfoDesc& o) const noexcept { return shape == o.shape && rateHz == o.rateHz && phase == o.phase; }
};

// Enveloppe ADSR déclenchée par les notes de la piste : monte quand une touche est tenue, redescend au relâchement de la dernière.
struct EnvDesc
{
    double attackMs = 10.0, decayMs = 200.0, sustain = 0.7, releaseMs = 300.0;   // 0..10000 ms, sustain 0..1
    bool operator== (const EnvDesc& o) const noexcept { return attackMs == o.attackMs && decayMs == o.decayMs && sustain == o.sustain && releaseMs == o.releaseMs; }
};

struct Route
{
    Source source;
    ParamId dest = 0;
    double amount = 0.5;      // -1..1 : part de la course du paramètre (en unités normalisées) pour une source à son maximum
    bool centred = false;     // source unipolaire ramenée à -1..1 (le milieu de la source ne module pas)
    bool operator== (const Route& o) const noexcept { return source == o.source && dest == o.dest && amount == o.amount && centred == o.centred; }
};

struct Matrix
{
    std::array<LfoDesc, kLfos> lfos;
    std::array<EnvDesc, kEnvelopes> envelopes;
    std::vector<Route> routes;
    bool operator== (const Matrix& o) const noexcept { return lfos == o.lfos && envelopes == o.envelopes && routes == o.routes; }
    bool operator!= (const Matrix& o) const noexcept { return ! (*this == o); }
};

Matrix sanitised (Matrix);                 // bornes ; routes sans source ou sans destination retirées ; au plus kMaxRoutes
std::string toJson (const Matrix&);
Matrix fromJson (std::string_view);        // lecture tolérante

// ------------------------------------------------------------------------------------------------------------------ évaluation

// Matrice résolue pour le moteur : les destinations deviennent des indices du ParameterSet (seuls les paramètres continus et modulables
// sont gardés), les macros aussi. Construite sur le thread de message, lue par le thread audio.
struct CompiledRoute
{
    Source source;
    std::size_t dest = ParameterSet::npos;
    std::size_t macro = ParameterSet::npos;   // source Macro : indice du paramètre de la macro
    double amount = 0.0;
    bool centred = false;
};
struct Compiled
{
    std::array<LfoDesc, kLfos> lfos;
    std::array<EnvDesc, kEnvelopes> envelopes;
    std::vector<CompiledRoute> routes;
};
std::shared_ptr<const Compiled> compile (const Matrix&, const ParameterSet&);

// Évalue les sources d'une piste, bloc après bloc, et ajoute les décalages dans le ParameterSet. Un seul exemplaire par piste, thread audio.
class Modulator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setCompiled (const Compiled* c) noexcept { compiled_ = c; }
    // `gate` : une touche au moins est tenue sur la piste. `touched` reçoit les indices écrits (à remettre à zéro au bloc suivant).
    void process (int frames, bool gate, const midi::State& midi, const ParameterSet& set, std::vector<std::size_t>& touched) noexcept;

private:
    struct EnvState { enum class Stage { Idle, Attack, Decay, Sustain, Release } stage = Stage::Idle; double level = 0.0, releaseFrom = 0.0; };
    double sampleRate_ = 48000.0;
    const Compiled* compiled_ = nullptr;
    std::array<double, kLfos> lfoPhase_ {};
    std::array<double, kLfos> lfoHeld_ {};
    std::array<EnvState, kEnvelopes> env_ {};
    std::uint32_t rng_ = 12345u;
    bool lastGate_ = false;
};
} // namespace neptune::core::modulation
