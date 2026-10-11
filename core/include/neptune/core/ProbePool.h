#pragma once
// Pool de paramètres « sonde » (palier 0, point 4) : reproduit la forme du futur pool de slots
// (pistes, inserts, envois, paramètres discrets) pour mesurer ce que l'hôte et clap-juce-extensions encaissent.
// Les identifiants suivent un schéma fixe et stable : piste t -> base 1000 + 100·t.
#include "neptune/core/Parameter.h"

namespace neptune::core::probe
{
constexpr ParamId masterVolume = 1;
constexpr ParamId masterPan = 2;
constexpr ParamId masterMute = 3;
constexpr ParamId algorithm = 4; // discret 1..32 (comme le sélecteur d'algorithme FM)

constexpr ParamId trackBase = 1000;
constexpr ParamId trackStride = 100;
constexpr int insertsPerTrack = 8;
constexpr int sendsPerTrack = 8;

enum TrackOffset : ParamId { kVolume = 0, kPan = 1, kMute = 2, kSolo = 3, kType = 4, kInsertBypass = 10, kSendLevel = 20 };

constexpr ParamId trackParam (int track, ParamId offset) { return trackBase + trackStride * static_cast<ParamId> (track) + offset; }

constexpr int paramsPerTrack = 5 + insertsPerTrack + sendsPerTrack;
constexpr int expectedCount (int tracks) { return 4 + tracks * paramsPerTrack; }

// Retourne false (et remplit `error`) si une définition est refusée.
bool buildPool (ParameterSet& set, int tracks, std::string* error = nullptr);
} // namespace neptune::core::probe
