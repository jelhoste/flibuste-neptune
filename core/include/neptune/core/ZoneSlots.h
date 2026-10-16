#pragma once
// Emplacements assignables du Sampler (spec : « emplacements assignables par piste »). Une piste Sampler a 16 emplacements,
// chacun avec un paramètre automatisable (SamplerSlot dans Mixer.h, valeur -100..+100 %, neutre à 0). L'assignation (quelle zone ou
// quel Groupe, quel paramètre) est sauvegardée avec l'instrument (InstrumentDescription::slots).
//
// La valeur d'un emplacement est RELATIVE à la zone visée, neutre à 0 : attaque, décroissance et relâchement sont multipliés
// (x0,1 à x10), le sustain est multiplié (x0 à x2), le niveau est décalé (+-24 dB), l'accordage est décalé (+-100 cents).
// Les valeurs sont prises au début de chaque note (le relâchement : au Note Off).
#include <array>
#include <string>
#include <string_view>

namespace neptune::core
{
constexpr int kZoneSlots = 16;

enum class SlotParam { None = 0, Attack, Decay, Sustain, Release, Level, Tune };
constexpr int kSlotParams = 7;
const char* toString (SlotParam) noexcept;                 // "none" / "attack" / "decay" / "sustain" / "release" / "level" / "tune"
bool slotParamFromString (std::string_view, SlotParam& out);

struct SlotAssignment
{
    SlotParam param = SlotParam::None;
    bool isGroup = false;     // faux : une zone (indice dans la description) ; vrai : toutes les zones d'un Groupe (par son nom)
    int zone = -1;            // indice de la zone dans InstrumentDescription::zones
    std::string group;        // nom du Groupe
    bool assigned() const noexcept { return param != SlotParam::None && (isGroup ? ! group.empty() : zone >= 0); }
    bool operator== (const SlotAssignment& o) const noexcept
    {
        return param == o.param && isGroup == o.isGroup && zone == o.zone && group == o.group;
    }
};
using SlotAssignments = std::array<SlotAssignment, kZoneSlots>;

// Forme résolue pour le moteur (dans Instrument) : le Groupe est remplacé par son identifiant interne.
struct SlotRuntime
{
    SlotParam param = SlotParam::None;
    bool isGroup = false;
    int zoneIndex = -1;   // indice dans la description (Zone::sourceIndex)
    int groupId = 0;      // Zone::groupId
};
using SlotRuntimes = std::array<SlotRuntime, kZoneSlots>;

// Effet cumulé des emplacements sur une zone.
struct SlotMods
{
    float attack = 1.0f, decay = 1.0f, sustain = 1.0f, release = 1.0f; // facteurs
    float gain = 1.0f;                                                 // gain linéaire
    float tuneCents = 0.0f;
    bool any = false;
};

// `amounts` : les 16 valeurs des emplacements, -1..+1. Une zone peut être visée par plusieurs emplacements (facteurs multipliés,
// décalages additionnés).
SlotMods resolveSlotMods (const SlotRuntimes&, const float* amounts, int zoneSourceIndex, int zoneGroupId) noexcept;
} // namespace neptune::core
