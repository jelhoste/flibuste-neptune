#pragma once
// Mixeur (palier 1, jalon 9) : modèle des pistes, bus et master, identifiants de paramètres et lois de pan.
//
// Chemin du signal d'une piste (ordre réel de traitement, voir spec §1) : instrument -> [trim = gain de l'instrument] ->
// (inserts : 8 emplacements, voir StockModule.h) -> fader -> pan -> sorties. Les 4 points de captation d'un
// envoi sont donc : 0 pré-inserts, 1 post-inserts / pré-fader (identiques tant qu'il n'y a pas d'inserts), 2 post-fader /
// pré-pan, 3 post-pan (défaut).
//
// Routage (par piste et par bus) :
//  - sortie principale : le Master (défaut) ou un bus ; activable / désactivable (le « bouton Envoi vers Master » de la spec) ;
//  - sortie directe : aucune ou une sortie physique 1..64, INDÉPENDANTE de la sortie principale (les deux peuvent être actives) ;
//  - 8 envois : bus de destination, point de captation, niveau (paramètre automatisable), pan, mute.
// Pour qu'aucun cycle ne soit possible par construction, un bus ne peut router (sortie principale ou envoi) que vers un bus
// d'indice SUPÉRIEUR au sien ; toute autre cible est ignorée par le moteur.
#include <array>
#include <cmath>
#include <map>
#include <string>

#include "neptune/core/Parameter.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

namespace neptune::core::mixer
{
constexpr int numTracks = 16;
constexpr int numBuses = 16;
constexpr int numSends = 8;
constexpr int numOutputs = 64; // sorties stéréo physiques déclarées à l'hôte
constexpr int numInserts = 8;  // emplacements d'inserts par piste, par bus et sur le Master
constexpr int numParallels = 32; // pistes parallèles (pool propre, réparties librement dans l'arbre)
constexpr int parentOfParallelBase = numTracks; // identifiant de parent : 0..15 piste, 16..47 piste parallèle

// ---- identifiants de paramètres (stables : ne jamais renuméroter) ----
constexpr ParamId masterTrim = 5;   // dB, -12..+12
constexpr ParamId masterMono = 6;   // bascule : réduction mono
constexpr ParamId masterInsertBypass = 7; // 7..14 : bypass des 8 inserts du master
constexpr ParamId masterInsertBypassId (int slot) { return masterInsertBypass + static_cast<ParamId> (slot); }
constexpr ParamId busBase = 3000;   // bus b -> 3000 + 100.b
constexpr ParamId busStride = 100;
enum BusOffset : ParamId { kBusVolume = 0, kBusPan = 1, kBusMute = 2, kBusSolo = 3, kBusInsertBypass = 10, kBusSendLevel = 20 };
constexpr ParamId busParam (int bus, ParamId offset) { return busBase + busStride * static_cast<ParamId> (bus) + offset; }

// Pistes parallèles (spec §1) : mêmes réglages automatisables qu'une piste (volume, pan, mute, solo, bypass d'inserts, niveaux d'envoi),
// identifiants `5000 + 100.p + offset`.
constexpr ParamId parallelBase = 5000;
constexpr ParamId parallelStride = 100;
enum ParallelOffset : ParamId { kParVolume = 0, kParPan = 1, kParMute = 2, kParSolo = 3, kParInsertBypass = 10, kParSendLevel = 20 };
constexpr ParamId parallelParam (int p, ParamId offset) { return parallelBase + parallelStride * static_cast<ParamId> (p) + offset; }

// Paramètres de l'instrument d'une piste. La piste 0 garde les identifiants historiques 100..106 (projets existants) ; les
// autres pistes les ont dans leur bloc d'identifiants (offsets 30..36).
enum SamplerKind : int { kGain = 0, kTuneSemitones, kTuneCents, kFilter1Cutoff, kFilter1Resonance, kFilter2Cutoff, kFilter2Resonance, kSamplerKinds };
constexpr ParamId samplerParam (int track, SamplerKind kind)
{
    constexpr ParamId legacy[kSamplerKinds] = { sampler::gain, sampler::tuneSemitones, sampler::tuneCents, sampler::filter1Cutoff,
                                                sampler::filter1Resonance, sampler::filter2Cutoff, sampler::filter2Resonance };
    return track == 0 ? legacy[kind] : probe::trackParam (track, 30 + static_cast<ParamId> (kind));
}

// Paramètres automatisables du synthé d'une piste (déclarés pour les 16 pistes, utilisés quand la piste est un Synthé) :
// identifiants `1000 + 100.t + 40 + kind` (blocs libres des pistes), y compris pour la piste 1. Le gain, l'accordage et les coupures /
// résonances des 2 filtres sont ceux de l'instrument (samplerParam).
enum SynthKind : int
{
    kLevelOsc1 = 0, kLevelOsc2, kLevelOsc3, kLevelSub, kLevelNoise,   // %, 0..100
    kFineOsc1, kFineOsc2, kFineOsc3,                                  // cents, +-100
    kPulseOsc1, kPulseOsc2, kPulseOsc3,                               // %, 2..98 (largeur d'impulsion)
    kDetuneOsc1, kDetuneOsc2, kDetuneOsc3,                            // cents, 0..100 (détune d'unisson, voix extrêmes)
    kPitchEnvDepth,                                                   // demi-tons, +-48
    kSynthKinds
};
constexpr ParamId synthParamBase = 40;
constexpr ParamId synthParam (int track, SynthKind kind) { return probe::trackParam (track, synthParamBase + static_cast<ParamId> (kind)); }

// Ajoute au jeu : les paramètres d'instrument des pistes 1..n-1, ceux du master (trim, mono) et ceux des bus.
// (Les paramètres de piste volume/pan/mute/solo/envois viennent de probe::buildPool ; ceux de la piste 0, de sampler::addParams.)
bool addParams (ParameterSet& set, int tracks, int buses, std::string* error = nullptr);

// ---- réglages non automatisables (sauvegardés dans le projet) ----
enum class TapPoint : int { PreInsert = 0, PostInsertPreFader = 1, PostFaderPrePan = 2, PostPan = 3 };
enum class PanMode : int { Balance = 0, True = 1 };

// Un module posé sur un emplacement d'insert : son type ("" = vide) et les valeurs de ses paramètres (id local -> valeur).
// Les valeurs des modules vivants sont recopiées ici par l'adaptateur au moment de sauvegarder.
struct InsertState
{
    std::string type;
    std::map<ParamId, double> values;
    bool empty() const noexcept { return type.empty(); }
    bool operator== (const InsertState& o) const noexcept { return type == o.type && values == o.values; }
};

struct SendSettings
{
    int destination = 0;                 // 0 aucun, 1..numBuses : bus
    TapPoint tap = TapPoint::PostPan;
    double pan = 0.0;                    // -1..1
    bool mute = false;
    bool operator== (const SendSettings& o) const noexcept
    {
        return destination == o.destination && tap == o.tap && pan == o.pan && mute == o.mute;
    }
};

struct Routing
{
    int mainDest = 0;          // 0 : Master ; 1..numBuses : bus
    bool mainEnabled = true;   // « Envoi vers Master » / vers le bus de sortie
    int directOut = 0;         // 0 : aucune ; 1..numOutputs : sortie physique (additive)
    bool operator== (const Routing& o) const noexcept
    {
        return mainDest == o.mainDest && mainEnabled == o.mainEnabled && directOut == o.directOut;
    }
};

struct StripSettings
{
    std::string name;
    int color = 0;                       // indice de palette
    int midiChannel = 0;                 // pistes : 0 = tous, 1..16
    Routing routing;
    PanMode panMode = PanMode::Balance;
    std::array<SendSettings, numSends> sends;
    std::array<InsertState, numInserts> inserts;
    bool slim = false;

    bool operator== (const StripSettings& o) const noexcept
    {
        return name == o.name && color == o.color && midiChannel == o.midiChannel && routing == o.routing
            && panMode == o.panMode && sends == o.sends && inserts == o.inserts && slim == o.slim;
    }
};

// Une piste parallèle : prend le signal de la chaîne de son parent direct (piste ou autre piste parallèle) à un des 4 points de
// captation, a une bande complète (inserts, envois, pan, fader), et se somme en fin de chaîne du parent. Structure en arbre :
// un parent n'est jamais une piste « latérale » ni un grand-parent, donc aucun cycle possible.
struct ParallelSettings
{
    bool active = false;
    int parent = 0;                       // 0..numTracks-1 : piste ; numTracks.. : piste parallèle (indice p + numTracks)
    TapPoint tap = TapPoint::PostPan;     // point de captation dans la chaîne du parent
    StripSettings strip;                  // nom, couleur, envois, sortie directe, pan, inserts ; « mainEnabled » : se somme au parent

    bool operator== (const ParallelSettings& o) const noexcept
    {
        return active == o.active && parent == o.parent && tap == o.tap && strip == o.strip;
    }
};
ParallelSettings sanitised (ParallelSettings p, int index);

struct MasterSettings
{
    int output = 1;                      // sortie physique du master (1..numOutputs)
    PanMode panMode = PanMode::Balance;
    std::array<InsertState, numInserts> inserts;
    bool operator== (const MasterSettings& o) const noexcept { return output == o.output && panMode == o.panMode && inserts == o.inserts; }
};

// Bornes les réglages (indices, cycles interdits : un bus ne route que vers un bus d'indice supérieur).
StripSettings sanitisedStrip (StripSettings s, bool isBus, int index);
MasterSettings sanitised (MasterSettings m);

// ---- traitement ----
// Gains (gauche, droite) appliqués à un signal stéréo pour un pan dans [-1,1].
//  Balance : le canal opposé est atténué (linéaire), rien n'est touché au centre ;
//  Pan vrai : loi à puissance constante, -3 dB par canal au centre, tout à gauche = (1, 0).
inline void panGains (PanMode mode, float pan, float& left, float& right) noexcept
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (mode == PanMode::Balance)
    {
        left = pan > 0.0f ? 1.0f - pan : 1.0f;
        right = pan < 0.0f ? 1.0f + pan : 1.0f;
        return;
    }
    const float theta = (pan + 1.0f) * 0.78539816339f; // (pan + 1) . pi/4
    left = std::cos (theta);
    right = std::sin (theta);
}

// dB -> gain linéaire ; en deçà de `floorDb` (le « -inf » des faders) : silence.
inline float dbToGain (double db, double floorDb = -100.0) noexcept
{
    return db <= floorDb ? 0.0f : static_cast<float> (std::pow (10.0, db / 20.0));
}
} // namespace neptune::core::mixer
