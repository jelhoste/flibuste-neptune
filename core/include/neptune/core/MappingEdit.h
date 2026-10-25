#pragma once
// Édition de la matrice notes × vélocité (palier 1, jalon 5) : logique pure, sans interface.
// Une « place » regroupe les zones qui couvrent exactement les mêmes notes/vélocités dans le même Groupe et le même
// déclenchement (le principal et ses alternatives round-robin) : la matrice en dessine un rectangle.
// Toutes les opérations sont des fonctions de (description de départ, delta TOTAL) : un glissé les rappelle à chaque
// mouvement avec la description du début du geste, sans accumuler d'erreur, et se termine en une seule entrée d'annulation.
#include <string>
#include <vector>

#include "neptune/core/Instrument.h"

namespace neptune::core::mapping
{
// Ce que la matrice affiche : les zones d'une catégorie de déclenchement (attaque, « Premier », « Legato » ou relâchement)
// d'un même Groupe nommé (ou sans Groupe). Chaque combinaison présente dans l'instrument est une vue.
struct View
{
    enum class Category { Attack, First, Legato, Release } category = Category::Attack;
    std::string group; // "" = zones sans Groupe
    bool accepts (const ZoneDescription&) const noexcept;
    bool operator== (const View& o) const noexcept { return category == o.category && group == o.group; }
};

// Les vues, dans un ordre stable : les quatre déclenchements sans Groupe (attaque, premier, legato, relâchement : toujours présents, même vides, pour pouvoir y
// placer des zones), puis les Groupes qui ont au moins une zone, par catégorie.
std::vector<View> views (const InstrumentDescription&);

// Place des zones dans une vue (glisser depuis la liste) : la i-ème zone prend la note `firstKey + i` (bornée à 127), toutes les vélocités (1 à 127), le
// déclenchement et le Groupe de la vue ; sa note racine ne change pas. Les indices hors de l'instrument sont ignorés.
InstrumentDescription assignToView (const InstrumentDescription& base, const std::vector<int>& zones, const View& view, int firstKey);
View viewOf (const ZoneDescription&) noexcept;

struct Place
{
    int keyLow = 0, keyHigh = 127, velLow = 1, velHigh = 127;
    std::string group;
    bool onRelease = false;
    TriggerCondition trigger = TriggerCondition::Always;
    std::vector<int> zones;   // indices dans la description, ordre croissant
    bool overlaps = false;    // recouvre une autre place de la même vue (permis, signalé)
};

// Places de la vue, triées par (note basse, vélocité basse). Les chevauchements sont marqués.
std::vector<Place> places (const InstrumentDescription&, const View&);

enum class Edge { KeyLow, KeyHigh, VelLow, VelHigh };

// Déplace les zones (delta en notes et en vélocités), le même pour toutes : borné pour que TOUTES restent dans 0..127 /
// 1..127 (la sélection se déplace d'un bloc, sans écrasement). La note racine reste, sauf si `rootFollows` (elle est alors
// décalée de dKey demi-tons, bornée à 0..127). Rend le delta réellement appliqué dans `appliedKey`/`appliedVel` si non nuls.
InstrumentDescription moveZones (const InstrumentDescription& base, const std::vector<int>& zones, int dKey, int dVel, bool rootFollows,
                                 int* appliedKey = nullptr, int* appliedVel = nullptr);

// Déplace UN bord de toutes les zones indiquées du même delta, borné pour qu'aucune ne s'inverse ni ne sorte de la plage.
// La racine ne change pas. Rend le delta réellement appliqué.
InstrumentDescription resizeEdge (const InstrumentDescription& base, const std::vector<int>& zones, Edge edge, int delta, int* applied = nullptr);

// Ajoute une copie de chacune des zones (même fichier, mêmes réglages) décalée de (dKey, dVel) ; les copies sont ajoutées à
// la fin, dans l'ordre. `newIndices` reçoit leurs indices. La racine des copies reste celle de l'original.
InstrumentDescription duplicateZones (const InstrumentDescription& base, const std::vector<int>& zones, int dKey, int dVel,
                                      std::vector<int>* newIndices = nullptr);

// ---- Groupes (articulations, micros, jeux d'orgue...) : créés, renommés, supprimés depuis la liste des zones ----
// Les noms de Groupe existants : ceux déclarés dans la matrice (dans l'ordre de création) puis ceux des zones qui ne le sont pas (triés), sans doublon.
std::vector<std::string> groupNames (const InstrumentDescription&);
// Nom de Groupe admissible : espaces de bord retirés, au plus 64 octets (coupé sans casser un caractère UTF-8).
std::string cleanGroupName (std::string);
// Met les zones indiquées dans le Groupe `group` ("" = aucun). Un Groupe qui n'a plus de zone perd ses réglages propres, ses emplacements assignés
// et sa mention « non chargé ». Les indices hors de l'instrument sont ignorés.
InstrumentDescription assignGroup (const InstrumentDescription& base, const std::vector<int>& zones, const std::string& group);
// Déclare un Groupe vide (« + Groupe » de la matrice). Faux, et rien ne change, si le nom est vide ou déjà pris.
bool addGroup (InstrumentDescription& d, const std::string& name);
// Renomme un Groupe (zones, réglages propres, emplacements, « non chargé »). Faux, et rien ne change, si le nouveau nom est vide, identique ou déjà pris.
bool renameGroup (InstrumentDescription& d, const std::string& from, const std::string& to);
// Supprime un Groupe : ses zones n'ont plus de Groupe, ses réglages propres et ses emplacements disparaissent. Les zones restent.
InstrumentDescription deleteGroup (const InstrumentDescription& base, const std::string& group);
} // namespace neptune::core::mapping
