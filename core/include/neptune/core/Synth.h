#pragma once
// Synthé soustractif (spec-modules §2). Structure fixe, non modulaire :
//   3 oscillateurs (catalogue de formes d'onde, unisson à bande limitée jusqu'à 8 voix, synchro dure 2 et 3 sur 1)
//   + 1 sous-oscillateur + 1 générateur de bruit -> mixeur des 5 sources (niveau, mute, pan)
//   -> 2 filtres (série ou parallèle, chacun avec son enveloppe) -> enveloppe de volume ; enveloppe de pitch dédiée.
//
//  - SynthDescription : réglages sauvegardés dans le projet (par piste).
//  - SynthPatch       : les mêmes réglages prêts à jouer (tables d'enveloppes), immuables, remis au thread audio.
// Les valeurs automatisables (niveaux des sources, fine tune, largeur d'impulsion, détune d'unisson, profondeur de l'enveloppe de
// pitch, coupures et résonances, gain, accordage) sont des paramètres de la piste (Mixer.h : samplerParam, synthParam).
#include <array>
#include <string>
#include <string_view>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/WaveCatalog.h"

namespace neptune::core
{
// Type de génération d'une piste (spec §1). FM et Entrée audio viendront plus tard.
enum class TrackType { Sampler = 0, Synth = 1 };
const char* toString (TrackType) noexcept;                         // "sampler" / "synth"
bool trackTypeFromString (std::string_view, TrackType& out);

enum class DetuneCurve { Linear, Supersaw };    // répartition du détune entre les voix d'unisson
enum class SubWave { Sine, Triangle, Square };
enum class NoiseType { White, Pink };
const char* toString (DetuneCurve) noexcept;
const char* toString (SubWave) noexcept;
const char* toString (NoiseType) noexcept;

constexpr int numOscillators = 3;
constexpr int numSources = 5;                   // osc 1..3, sous-oscillateur, bruit
constexpr int kSourceSub = 3, kSourceNoise = 4;
constexpr int maxUnison = 8;

struct OscDescription
{
    int wave = kWaveSaw;       // identifiant du catalogue
    int octave = 0;            // -3..+3
    int semitone = 0;          // -12..+12
    int unison = 1;            // 1..maxUnison
    double spread = 0.5;       // 0..1 : répartition stéréo des voix d'unisson
    DetuneCurve curve = DetuneCurve::Linear;
    bool retrigger = true;     // faux : phase libre (ne repart pas au Note On)
    double startPhase = 0.0;   // 0..1 (avec retrigger)
    bool sync = false;         // oscillateurs 2 et 3 : synchro dure sur l'oscillateur 1

    bool operator== (const OscDescription& o) const noexcept
    {
        return wave == o.wave && octave == o.octave && semitone == o.semitone && unison == o.unison && spread == o.spread
            && curve == o.curve && retrigger == o.retrigger && startPhase == o.startPhase && sync == o.sync;
    }
};

struct SourceMixDescription
{
    bool mute = false;
    double pan = 0.0;          // -1..1
    bool operator== (const SourceMixDescription& o) const noexcept { return mute == o.mute && pan == o.pan; }
};

struct SynthDescription
{
    std::array<OscDescription, numOscillators> osc;
    SubWave subWave = SubWave::Sine;
    int subOctave = -1;        // -1 ou -2
    NoiseType noise = NoiseType::White;
    std::array<SourceMixDescription, numSources> mix;
    EnvelopeDescription ampEnv;    // enveloppe de volume
    EnvelopeDescription pitchEnv;  // enveloppe de pitch (la profondeur est le paramètre de la piste)
    FilterBlock filters;           // 2 filtres, série / parallèle ; coupures et résonances = paramètres de la piste

    PolyphonyMode polyphony = PolyphonyMode::Poly;
    int maxVoices = defaultMaxVoices;
    VoiceStealMode stealMode = VoiceStealMode::Oldest;
    int glideMs = 0;

    static constexpr int defaultMaxVoices = 16, maxVoicesLimit = 64, minOctave = -3, maxOctave = 3, maxGlideMs = 2000;

    // Valeurs de départ : un oscillateur en dent de scie, attaque 5 ms (pas de clic), relâchement 150 ms, filtre 1 passe-bas 24 dB
    // ouvert, filtre 2 éteint ; enveloppe de pitch neutre (profondeur 0).
    SynthDescription()
    {
        ampEnv.attack.ms = 5.0;
        ampEnv.release.ms = 150.0;
        filters.filter[0].on = true;
        filters.filter[0].slopeDb = 24;
        filters.filter[1].slopeDb = 24;
    }

    bool operator== (const SynthDescription& o) const noexcept
    {
        return osc == o.osc && subWave == o.subWave && subOctave == o.subOctave && noise == o.noise && mix == o.mix
            && ampEnv == o.ampEnv && pitchEnv == o.pitchEnv && filters == o.filters && polyphony == o.polyphony
            && maxVoices == o.maxVoices && stealMode == o.stealMode && glideMs == o.glideMs;
    }
    bool operator!= (const SynthDescription& o) const noexcept { return ! (*this == o); }
};

SynthDescription sanitised (SynthDescription d); // bornes, valeurs finies, filtres et enveloppes bornés

// JSON : objet ; lecture tolérante (champ invalide ou absent -> défaut). Pas d'accès disque.
std::string toJson (const SynthDescription&);
SynthDescription synthFromJson (std::string_view text);

struct SynthPatch
{
    SynthDescription desc;                       // bornée
    EnvelopeRuntime ampEnv, pitchEnv;            // tables précalculées
    std::array<EnvelopeRuntime, 2> filterEnv;

    // Construit aussi (une fois) les tables d'onde : à faire hors temps réel, donc ici.
    explicit SynthPatch (const SynthDescription& d);
};
} // namespace neptune::core
