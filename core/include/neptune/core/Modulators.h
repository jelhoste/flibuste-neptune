#pragma once
// Modulateurs du Sampler (spec §3) : des interrupteurs (ou curseurs) nommés qui activent ou coupent des Groupes, des zones ou toute la piste. Chacun est un
// paramètre automatisable 0..100 % (donc commandé par un CC, OSC, une macro ou l'automation de l'hôte via la couche de contrôle) et peut avoir en plus une
// touche de keyswitch, qui le bascule sans jouer de son. Une zone ne se déclenche que si tous les modulateurs qui la commandent (les siens, ceux de son Groupe et
// ceux de la piste) sont actifs ; un modulateur « continu » règle en plus le volume en direct (fondu progressif).
#include <array>
#include <cstdint>
#include <string>

namespace neptune::core
{
// Jusqu'à 32 par piste : un interrupteur par Groupe (un jeu d'orgue) suffit pour un clavier. Les 16 premiers gardent leurs identifiants d'origine.
constexpr int kModulators = 32;
using ModMask = std::uint32_t;   // un bit par modulateur

struct ModulatorDescription
{
    std::string name;          // libre ; vide : « Modulateur n »
    bool momentary = false;    // touche de keyswitch : faux = bascule (latch), vrai = actif seulement pendant l'appui
    bool continuous = false;   // faux : discret (actif à partir de 50 %) ; vrai : le volume suit la valeur (0..100 %)
    int keyswitchNote = -1;    // -1 : aucune touche ; sinon 0..127 (cette note ne joue pas de son)
    int keyswitchChannel = 0;  // 0 : tous les canaux, 1..16
    int radio = 0;             // 0 : aucun ; sinon groupe d'exclusion : allumer l'un éteint les autres du même groupe (articulations)
    // Orgue (boîte expressive, tremblant), modulateur continu seulement :
    double floorLevel = 0.0;   // niveau minimal (0..1) : le volume va de `floorLevel` (valeur 0) à 1 (valeur 100 %) ; une boîte fermée garde du son
    double tremPeriodMs = 0.0; // > 0 : tremblant, le volume oscille avec cette période tant que le modulateur `tremGate` est actif
    double tremDepth = 0.0;    // profondeur du tremblant (0..1) dans l'orgue d'origine : valeur de départ du curseur ; en jeu, la valeur du modulateur (0..100 %) fait foi
    int tremGate = -1;         // modulateur qui allume le tremblant ; -1 : toujours
    int tremSpeed = -1;        // modulateur qui règle la vitesse du tremblant (50 % = la période de l'orgue, 0 = 4 fois plus lent, 100 % = 4 fois plus vite) ; -1 : période fixe
    bool operator== (const ModulatorDescription& o) const noexcept
    {
        return name == o.name && momentary == o.momentary && continuous == o.continuous && keyswitchNote == o.keyswitchNote
            && keyswitchChannel == o.keyswitchChannel && radio == o.radio && floorLevel == o.floorLevel && tremPeriodMs == o.tremPeriodMs
            && tremDepth == o.tremDepth && tremGate == o.tremGate && tremSpeed == o.tremSpeed;
    }
    bool isDefault() const noexcept { return *this == ModulatorDescription {}; }
};
using ModulatorList = std::array<ModulatorDescription, kModulators>;

// Forme résolue pour le moteur.
struct ModulatorRuntime
{
    bool used = false;         // a une touche de keyswitch ou commande quelque chose
    bool momentary = false, continuous = false;
    int note = -1, channel = 0, radio = 0;
    float floorLevel = 0.0f;
    double tremPeriodMs = 0.0;
    float tremDepth = 0.0f;
    int tremGate = -1;
    int tremSpeed = -1;
};
using ModulatorRuntimes = std::array<ModulatorRuntime, kModulators>;
} // namespace neptune::core
