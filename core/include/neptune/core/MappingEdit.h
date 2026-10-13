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

// Les vues qui ont au moins une zone, dans un ordre stable : l'attaque sans Groupe d'abord (toujours présente), puis par Groupe
// et par catégorie.
std::vector<View> views (const InstrumentDescription&);
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
} // namespace neptune::core::mapping
