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
//  - sortie directe : aucune ou une sortie physique 1..32, INDÉPENDANTE de la sortie principale (les deux peuvent être actives) ;
//  - 8 envois : bus de destination, point de captation, niveau (paramètre automatisable), pan, mute.
// Pour qu'aucun cycle ne soit possible par construction, un bus ne peut router (sortie principale ou envoi) que vers un bus
// d'indice SUPÉRIEUR au sien ; toute autre cible est ignorée par le moteur.
#include <array>
#include <cmath>
#include <map>
#include <string>

#include "neptune/core/MidiFilter.h"
#include "neptune/core/Parameter.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

namespace neptune::core::mixer
{
constexpr int numTracks = 16;
constexpr int numBuses = 16;
constexpr int numSends = 8;
constexpr int numOutputs = 32; // sorties stéréo physiques déclarées à l'hôte
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

// Paramètres automatisables d'une piste FM (module FM de type DX7) : identifiants `1000 + 100.t + 60 + kind`. Le gain et l'accordage
// sont ceux de l'instrument (samplerParam).
enum FmKind : int
{
    kFmBrightness = 0,                       // %, -100..100 : niveau des modulateurs (±12 dB)
    kFmEnvSpeed,                             // %, -100..100 : vitesse des enveloppes (prise au début de chaque note)
    kFmFeedbackDrive,                        // %, 0..100 : feedback ajouté
    kFmLevelOp1, kFmLevelOp2, kFmLevelOp3, kFmLevelOp4, kFmLevelOp5, kFmLevelOp6, // pas DX7, -99..+99 (OP1..OP6)
    kFmUnisonDetune,                         // cents, 0..100
    kFmUnisonSpread,                         // %, 0..100
    kFmKinds
};
constexpr ParamId fmParamBase = 60;
constexpr ParamId fmParam (int track, FmKind kind) { return probe::trackParam (track, fmParamBase + static_cast<ParamId> (kind)); }
bool addFmParams (ParameterSet& set, int track, const std::string& prefix, std::string* error = nullptr);

// Réglages FM supplémentaires : identifiants `1000 + 100.t + 85 + kind` (7 par piste). Comme pour le synthé (SynthEnvKind), le moteur
// lit ces paramètres et la description FM garde les mêmes valeurs (fm::extraParamValues / withLiveParams).
enum FmExtraKind : int
{
    kFmLfoSpeed = 0, kFmLfoPmd, kFmLfoAmd,    // 0..99 (vitesse, profondeur de vibrato, profondeur de trémolo du LFO)
    kFmBendRange,                             // 0..12 demi-tons
    kFmWheelPitch, kFmWheelAmp,               // 0..15 (sensibilités de la molette)
    kFmGlide,                                 // ms, 0..2000 (mono et legato)
    kFmExtraKinds
};
constexpr ParamId fmExtraParamBase = 85;
constexpr ParamId fmExtraParam (int track, FmExtraKind kind) { return probe::trackParam (track, fmExtraParamBase + static_cast<ParamId> (kind)); }

// Enveloppes, glissando et équilibre des filtres du synthé : identifiants `1000 + 100.t + 71 + kind` (14 paramètres). Ces valeurs
// sont la référence du moteur ; la description du synthé (JSON) garde les mêmes valeurs pour l'éditeur et la sauvegarde (voir
// analogsynth::seedEnvelopeParams / withLiveParams).
enum SynthEnvKind : int
{
    kAmpAttack = 0, kAmpDecay, kAmpSustain, kAmpRelease,        // ms, ms, %, ms
    kFilter1Attack, kFilter1Decay, kFilter1Sustain, kFilter1Release,
    kFilter2Attack, kFilter2Decay, kFilter2Sustain, kFilter2Release,
    kSynthGlide,                                                // ms, 0..2000 (mono et legato)
    kFilterBalance,                                             // %, 0..100 (mode parallèle : 0 filtre 1, 100 filtre 2)
    kSynthEnvKinds
};
constexpr ParamId synthEnvParamBase = 71;
constexpr ParamId synthEnvParam (int track, SynthEnvKind kind)
{
    return probe::trackParam (track, synthEnvParamBase + static_cast<ParamId> (kind));
}

// Paramètres automatisables d'une piste Orgue (moteur de setBfree) : identifiants `9000 + 100.t + kind`. Le gain et le volume sont
// ceux de l'instrument (samplerParam / probe::kVolume).
enum OrganKind : int
{
    kOrgUpper0 = 0,                 // 9 tirettes du supérieur : 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' (0..8)
    kOrgLower0 = 9,                 // 9 tirettes de l'inférieur
    kOrgPedal16 = 18, kOrgPedal8,   // 2 tirettes du pédalier : 16' et 8'
    kOrgPercOn = 20, kOrgPercSoft, kOrgPercFast, kOrgPercThird, // percussion : marche, douce, rapide, 3e harmonique
    kOrgVibMode = 24, kOrgVibUpper, kOrgVibLower,               // vibrato / chorus : mode (−, V1, C1, V2, C2, V3, C3), routage
    kOrgLeslieSpeed = 27, kOrgLeslieMix,                        // Leslie : chorale / tremolo / frein, dry-wet (%)
    kOrgDriveOn = 29, kOrgDriveInput, kOrgDriveCharacter, kOrgDriveOutput, // préampli à lampes (%)
    kOrgReverb = 33,                // réverbération à ressorts (%)
    kOrgVibDepth = 34,              // profondeur du vibrato / chorus (%, 50 : réglage d'origine)
    kOrganKinds
};
constexpr ParamId organParamBase = 9000;
constexpr ParamId organParam (int track, int kind) { return organParamBase + probe::trackStride * static_cast<ParamId> (track) + static_cast<ParamId> (kind); }
bool addOrganParams (ParameterSet& set, int track, const std::string& prefix, std::string* error = nullptr);

// Déclare les 15 paramètres du synthé d'une piste (`prefix` : début du nom affiché, ex. "Track 2 "). Sert aussi au plugin AnalogSynth.
bool addSynthParams (ParameterSet& set, int track, const std::string& prefix, std::string* error = nullptr);

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
    midi::FilterDescription midiFilter;  // pistes : filtre MIDI avancé (en plus du canal), éteint par défaut
    Routing routing;
    PanMode panMode = PanMode::Balance;
    std::array<SendSettings, numSends> sends;
    std::array<InsertState, numInserts> inserts;
    bool slim = false;

    bool operator== (const StripSettings& o) const noexcept
    {
        return name == o.name && color == o.color && midiChannel == o.midiChannel && midiFilter == o.midiFilter && routing == o.routing
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
    // Le fader du parent commande aussi celui de cette piste : son signal est multiplié par le gain de fader du parent. N'a d'effet que
    // pour une captation AVANT le fader (points 0 et 1) : après le fader, le signal contient déjà le volume du parent.
    bool followParentVolume = false;

    bool operator== (const ParallelSettings& o) const noexcept
    {
        return active == o.active && parent == o.parent && tap == o.tap && strip == o.strip && followParentVolume == o.followParentVolume;
    }
};
ParallelSettings sanitised (ParallelSettings p, int index);

struct MasterSettings
{
    int output = 1;                      // sortie physique du master (1..numOutputs)
    PanMode panMode = PanMode::Balance;
    int maxVoices = defaultMaxVoices;    // limite globale de voix, toutes pistes confondues (1..maxVoicesLimit)
    std::array<InsertState, numInserts> inserts;
    static constexpr int defaultMaxVoices = 256, maxVoicesLimit = 1024;
    bool operator== (const MasterSettings& o) const noexcept
    {
        return output == o.output && panMode == o.panMode && maxVoices == o.maxVoices && inserts == o.inserts;
    }
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
