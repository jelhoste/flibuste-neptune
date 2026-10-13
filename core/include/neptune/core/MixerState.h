#pragma once
// État du mixeur sauvegardé dans le projet (schéma v6) : pour chaque piste son type (Sampler / Synthé), son instrument ou son
// patch de synthé et ses réglages de bande, les bus, le master et la piste sélectionnée. Les valeurs automatisables (fader, pan, mute, solo, envois, coupure...) sont des paramètres
// et vivent dans le jeu de paramètres, pas ici.
#include <array>
#include <string>
#include <string_view>

#include "neptune/core/Instrument.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/Synth.h"

namespace neptune::core
{
struct TrackState
{
    // Piste « active » : affichée par le mixeur et jouée. Le moteur garde ses 16 emplacements (pool fixe, spec) ; une piste
    // inactive est à l'état d'origine et n'apparaît pas. La piste 1 est active par défaut.
    bool active = false;
    TrackType type = TrackType::Sampler;
    InstrumentDescription instrument;   // type Sampler
    SynthDescription synth;             // type Synthé
    mixer::StripSettings strip;
    bool operator== (const TrackState& o) const noexcept
    {
        return active == o.active && type == o.type && instrument == o.instrument && synth == o.synth && strip == o.strip;
    }
};

struct MixerState
{
    std::array<TrackState, mixer::numTracks> tracks;
    std::array<mixer::StripSettings, mixer::numBuses> buses;
    mixer::MasterSettings master;
    int selectedTrack = 0;

    MixerState();  // canaux MIDI par défaut : piste 1 « tous », piste n : canal n ; piste 1 active (Sampler)

    // Projet neuf : une seule piste, réglée sur Synthé (l'utilisateur en ajoute ou change de type ensuite).
    static MixerState freshProject();

    int activeCount() const noexcept;
    // Première piste inactive, ou -1 si les 16 sont actives.
    int firstInactive() const noexcept;
    // Remet une piste à l'état d'origine (inactive, Sampler, instrument vide, réglages de bande par défaut).
    void resetTrack (int track);
    // La piste sélectionnée est toujours active (la première active si besoin).
    void fixSelection() noexcept;

    bool operator== (const MixerState& o) const noexcept
    {
        return tracks == o.tracks && buses == o.buses && master == o.master && selectedTrack == o.selectedTrack;
    }
};

std::string toJson (const MixerState&);
MixerState mixerFromJson (std::string_view text); // lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées
} // namespace neptune::core
