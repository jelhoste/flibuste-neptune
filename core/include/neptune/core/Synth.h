#pragma once
// Synthé soustractif (palier 1, premier jet) : un oscillateur à bande limitée, une enveloppe de volume AHDSHR, un filtre avec
// son enveloppe, polyphonie. Structure fixe, non modulaire (spec-modules §2). Les oscillateurs supplémentaires, le sous-oscillateur,
// le bruit, l'unisson, le second filtre et l'enveloppe de pitch viendront s'ajouter à cette description sans la remettre en cause.
//
//  - SynthDescription : réglages sauvegardés dans le projet (par piste).
//  - SynthPatch       : les mêmes réglages prêts à jouer (tables d'enveloppes), immuables, remis au thread audio.
// Le gain, l'accordage, la coupure et la résonance du filtre sont les paramètres automatisables de la piste (Mixer.h :
// samplerParam), les mêmes que pour un Sampler : les potards du bas de la fenêtre restent donc les mêmes quel que soit le type.
#include <string>
#include <string_view>

#include "neptune/core/Envelope.h"
#include "neptune/core/Filter.h"
#include "neptune/core/Instrument.h"

namespace neptune::core
{
// Type de génération d'une piste (spec §1). FM et Entrée audio viendront plus tard.
enum class TrackType { Sampler = 0, Synth = 1 };
const char* toString (TrackType) noexcept;                         // "sampler" / "synth"
bool trackTypeFromString (std::string_view, TrackType& out);

enum class Waveform { Sine = 0, Triangle, Saw, Square };
constexpr int numWaveforms = 4;
const char* toString (Waveform) noexcept;                          // "sine" / "triangle" / "saw" / "square"
bool waveformFromString (std::string_view, Waveform& out);

struct SynthDescription
{
    Waveform waveform = Waveform::Saw;
    int octave = 0;                    // -3..+3 : transposition en octaves, avant l'accordage de la piste
    EnvelopeDescription ampEnv;        // enveloppe de volume (même moteur AHDSHR que le Sampler)
    FilterSettings filter;             // un filtre, son enveloppe, suivi du clavier, vélocité ; coupure/résonance = paramètres de la piste

    PolyphonyMode polyphony = PolyphonyMode::Poly; // Mono : une note à la fois ; Legato : la voix change de hauteur sans repartir
    int maxVoices = defaultMaxVoices;              // 1..maxVoicesLimit
    VoiceStealMode stealMode = VoiceStealMode::Oldest;
    int glideMs = 0;                               // glissando du legato, 0..maxGlideMs

    static constexpr int defaultMaxVoices = 16, maxVoicesLimit = 64, minOctave = -3, maxOctave = 3, maxGlideMs = 2000;

    // Valeurs de départ : une dent de scie avec une attaque de 5 ms (pas de clic) et un relâchement de 150 ms, filtre passe-bas
    // 24 dB ouvert.
    SynthDescription()
    {
        ampEnv.attack.ms = 5.0;
        ampEnv.release.ms = 150.0;
        filter.on = true;
        filter.slopeDb = 24;
    }

    bool operator== (const SynthDescription& o) const noexcept
    {
        return waveform == o.waveform && octave == o.octave && ampEnv == o.ampEnv && filter == o.filter && polyphony == o.polyphony
            && maxVoices == o.maxVoices && stealMode == o.stealMode && glideMs == o.glideMs;
    }
    bool operator!= (const SynthDescription& o) const noexcept { return ! (*this == o); }
};

SynthDescription sanitised (SynthDescription d); // bornes, valeurs finies, filtre et enveloppes bornés

// JSON : objet ; lecture tolérante (champ invalide ou absent -> défaut). Pas d'accès disque.
std::string toJson (const SynthDescription&);
SynthDescription synthFromJson (std::string_view text);

struct SynthPatch
{
    SynthDescription desc;      // bornée
    EnvelopeRuntime ampEnv;     // tables précalculées
    EnvelopeRuntime filterEnv;

    explicit SynthPatch (const SynthDescription& d)
        : desc (sanitised (d)), ampEnv (desc.ampEnv), filterEnv (desc.filter.env) {}
};
} // namespace neptune::core
