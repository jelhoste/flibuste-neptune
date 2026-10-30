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
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/WaveCatalog.h"

namespace neptune::core
{
// Type de génération d'une piste (spec §1). L'Entrée audio viendra plus tard.
enum class TrackType { Sampler = 0, Synth = 1, Fm = 2, Organ = 3, AudioIn = 4 };   // AudioIn : pas d'instrument, reçoit des envois et l'entrée audio de l'hôte
const char* toString (TrackType) noexcept;                         // "sampler" / "synth" / "fm" / "organ"
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
    int bendRange = 2;         // plage de la molette de hauteur (pitch bend), en demi-tons de chaque côté ; 0 : la molette ne change rien (plugin seul : Neptune n'envoie pas de pitch bend au synthé)

    static constexpr int defaultMaxVoices = 16, maxVoicesLimit = 64, minOctave = -3, maxOctave = 3, maxGlideMs = 2000, defaultBendRange = 2, maxBendRange = 24;

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
            && maxVoices == o.maxVoices && stealMode == o.stealMode && glideMs == o.glideMs && bendRange == o.bendRange;
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

    // Génération du patch (numéro croissant attribué à la construction) : les voix gardées en vie à un changement de preset
    // y font référence ; le publieur garde le patch vivant tant qu'une voix s'en sert (voir SynthPatchKeeper).
    std::uint64_t generation = 0;
    // Changement de preset (capture par voix) : à l'adoption de ce patch, les notes en cours gardent l'ancien patch et leurs
    // valeurs de paramètres d'avant ; les nouvelles notes prennent ce patch. Faux pour une simple retouche du patch (les notes en
    // cours suivent alors le nouveau réglage).
    bool freezeRunningVoices = false;

    // Construit aussi (une fois) les tables d'onde : à faire hors temps réel, donc ici.
    explicit SynthPatch (const SynthDescription& d);
};

// Garde en vie les anciens patchs tant que des voix figées (capture par voix) s'en servent. Thread de message uniquement ;
// la lecture de ce que l'audio publie se fait par `SynthVoices::oldestFrozenGeneration()` / `adoptedGeneration()`.
class SynthPatchKeeper
{
public:
    void add (std::shared_ptr<const SynthPatch> patch) { kept_.push_back (std::move (patch)); }
    // Retire ce qui n'est plus utile : tout patch de génération inférieure à la plus ancienne voix figée ET à celle du patch
    // actuellement adopté par l'audio. Le plus récent est toujours gardé.
    void prune (std::uint64_t oldestFrozen, std::uint64_t adopted)
    {
        const std::uint64_t floor = oldestFrozen < adopted ? oldestFrozen : adopted;
        std::size_t out = 0;
        for (std::size_t i = 0; i < kept_.size(); ++i)
        {
            const auto g = kept_[i]->generation;
            // utile : adopté ou plus ancien mais encore utilisé par une voix figée ; ou le plus récent (peut-être pas encore adopté)
            if ((g <= adopted && g >= floor) || i + 1 == kept_.size()) kept_[out++] = std::move (kept_[i]);
        }
        kept_.resize (out);
    }
    std::size_t size() const noexcept { return kept_.size(); }

private:
    std::vector<std::shared_ptr<const SynthPatch>> kept_;
};
} // namespace neptune::core
