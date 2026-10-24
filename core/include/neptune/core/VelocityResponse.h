#pragma once
// Réponse de vélocité de l'instrument et fondu enchaîné entre couches de vélocité (palier 1, jalon 5).
//
// 1) VelocityCurve : remappe la vélocité reçue (0..1) AVANT le choix des couches et le volume.
//    - `shape` ∈ [-1, 1] : 0 = linéaire ; < 0 douce (les petites vélocités sonnent plus fort), > 0 dure ; sortie = entrée^(2^(3·shape)).
//    - `points` (facultatif) : points (x, y) ∈ ]0,1[ × [0,1] ; s'il y en a, la courbe passe par eux (et par (0,0) et (1,1)) en
//      interpolation monotone de Fritsch-Carlson (jamais de dépassement ni de recul) et `shape` est ignoré.
//    Toujours monotone croissante et bornée : la table de 128 entrées qui sert à l'audio ne peut pas produire de valeur folle.
// 2) Fondu entre couches : voir computeLayerFades.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neptune::core
{
//    - `floor` et `ceiling` : la sortie de la courbe (0..1) est ramenée dans [floor, ceiling] (sortie = floor + (ceiling - floor)·courbe) ;
//      avec floor = 0,2 la plus petite vélocité reçue donne déjà 0,2 et toute la dynamique est remappée entre 0,2 et ceiling.
//    - `smooth` : faux, la courbe passe par les points en segments droits (angles nets) ; vrai, interpolation monotone lisse.
struct VelocityCurve
{
    double shape = 0.0;
    std::vector<std::pair<double, double>> points;
    double floor = 0.0;
    double ceiling = 1.0;
    bool smooth = true;

    bool isLinear() const noexcept { return points.empty() && shape == 0.0 && floor == 0.0 && ceiling == 1.0; }
    bool operator== (const VelocityCurve& o) const noexcept
    {
        return shape == o.shape && points == o.points && floor == o.floor && ceiling == o.ceiling && smooth == o.smooth;
    }
    static constexpr double minSpan = 0.05; // écart minimal entre le plancher et le plafond

    static constexpr std::size_t maxPoints = 16;
    static constexpr double minPointGap = 0.02; // écart minimal entre deux points, et avec 0 / 1
};

// Valeurs bornées : forme dans [-1,1], points triés, espacés, y croissants et dans [0,1] ; NaN rejetés.
VelocityCurve sanitised (VelocityCurve c);

// Texte JSON (lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées).
std::string toJson (const VelocityCurve&);
VelocityCurve velocityCurveFromJson (std::string_view);

// Sortie de la courbe pour une entrée dans [0,1] (bornée à [0,1]).
double evaluate (const VelocityCurve&, double x) noexcept;

// Table pour le temps réel : entrée k/127 -> sortie, k = 0..127.
using VelocityLut = std::array<float, 128>;
VelocityLut buildLut (const VelocityCurve&);
// Lecture avec interpolation linéaire ; velocity dans [0,1].
float lookup (const VelocityLut&, float velocity) noexcept;

// Préréglages de forme (les mêmes que dans l'interface).
enum class VelocityPreset { Linear, Soft, VerySoft, Hard, VeryHard };
constexpr int numVelocityPresets = 5;
VelocityCurve presetCurve (VelocityPreset);

// ---------------------------------------------------------------------------------------------------------------------
// Fondu enchaîné entre couches de vélocité.
//
// Les zones d'une même FAMILLE (mêmes notes, même Groupe, même déclenchement) forment des couches classées par vélocité.
// Pour chaque couche P, sa voisine inférieure L est la couche la plus proche telle que L.velLow < P.velLow et
// L.velHigh < P.velHigh (donc « jointive » ou « qui chevauche partiellement ») ; de même la voisine supérieure U.
//  - chevauchement explicite (L.velHigh >= P.velLow) : sur toute la zone commune, L descend de 1 à 0 et P monte de 0 à 1 ;
//  - jointives (L.velHigh + 1 == P.velLow) : si `width` > 0, fondu centré sur la frontière, de demi-largeur
//    min(width/2, largeur de L / 2, largeur de P / 2) ;
//  - couches imbriquées ou de même étendue (round-robin) : aucun fondu, les deux jouent à pleine puissance.
// Les vélocités sont continues : la zone [low, high] couvre [low - 0,5 ; high + 0,5].
struct LayerKey
{
    int keyLow = 0, keyHigh = 127, velLow = 1, velHigh = 127;
    int group = 0;            // identifiant de Groupe (0 = aucun) ; deux Groupes différents ne se fondent jamais
    bool onRelease = false;
    int variant = 0;          // condition de déclenchement : les couches de conditions différentes ne se fondent jamais
};

struct LayerFade
{
    float activeLo = 0.0f, activeHi = 0.0f; // la couche joue dans [activeLo, activeHi]
    bool hasIn = false, hasOut = false;
    float inStart = 0.0f, inEnd = 0.0f;     // montée 0 -> 1
    float outStart = 0.0f, outEnd = 0.0f;   // descente 1 -> 0
};

std::vector<LayerFade> computeLayerFades (const std::vector<LayerKey>& zones, int width);

// Gain de la couche à la vélocité continue `v` (1..127) : 0 hors de [activeLo, activeHi]. `linear` : fondu linéaire en
// amplitude ; sinon puissance constante (sin/cos : somme des carrés = 1).
float layerGain (const LayerFade&, float v, bool linear) noexcept;
} // namespace neptune::core
