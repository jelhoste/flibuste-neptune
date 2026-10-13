#pragma once
// État du mixeur sauvegardé dans le projet (schéma v5) : pour chaque piste son instrument et ses réglages de bande, les bus,
// le master et la piste sélectionnée. Les valeurs automatisables (fader, pan, mute, solo, envois, coupure...) sont des paramètres
// et vivent dans le jeu de paramètres, pas ici.
#include <array>
#include <string>
#include <string_view>

#include "neptune/core/Instrument.h"
#include "neptune/core/Mixer.h"

namespace neptune::core
{
struct TrackState
{
    InstrumentDescription instrument;
    mixer::StripSettings strip;
    bool operator== (const TrackState& o) const noexcept { return instrument == o.instrument && strip == o.strip; }
};

struct MixerState
{
    std::array<TrackState, mixer::numTracks> tracks;
    std::array<mixer::StripSettings, mixer::numBuses> buses;
    mixer::MasterSettings master;
    int selectedTrack = 0;

    MixerState();  // canaux MIDI par défaut : piste 1 « tous », piste n : canal n

    bool operator== (const MixerState& o) const noexcept
    {
        return tracks == o.tracks && buses == o.buses && master == o.master && selectedTrack == o.selectedTrack;
    }
};

std::string toJson (const MixerState&);
MixerState mixerFromJson (std::string_view text); // lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées
} // namespace neptune::core
