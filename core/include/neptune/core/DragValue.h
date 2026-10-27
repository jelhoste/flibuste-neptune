#pragma once
// Valeur numérique à « pill » glissable (souris ou doigt) avec saisie au clavier après un double-clic : la logique pure, sans JUCE.
//  - glisser horizontalement règle la valeur (toute la largeur de la pill = toute l'étendue, Maj = réglage fin) ;
//  - double-clic / double-tap ouvre un champ texte ; la saisie accepte la virgule ou le point, un espace et une unité derrière le nombre.
// L'étendue peut être linéaire ou logarithmique (fréquences : le glissé reste régulier de 0,01 à 50 Hz), avec un pas ou continue.
#include <string>

namespace neptune::core::dragvalue
{
struct Range
{
    double min = 0.0, max = 1.0;
    bool log = false;          // échelle logarithmique (exige min > 0)
    double step = 0.0;         // 0 : continu ; sinon la valeur est arrondie au pas le plus proche (à partir de min)
    int decimals = 2;          // chiffres après la virgule à l'affichage
    double def = 0.0;          // valeur par défaut (informative : l'interface peut la proposer)
};

constexpr double kFineRatio = 0.1;   // le réglage fin (Maj) multiplie le déplacement par ce facteur

double clamp (const Range&, double value) noexcept;            // borne et arrondit au pas
double toNormalised (const Range&, double value) noexcept;     // valeur -> 0..1 (bornée)
double fromNormalised (const Range&, double n) noexcept;       // 0..1 -> valeur (arrondie au pas)
std::string format (const Range&, double value);               // « 12,5 » : valeur bornée, `decimals` chiffres, point décimal
// Lit une saisie : « 1,5 », « 1.5 Hz », « -20 % »... Faux si aucun nombre ; sinon `out` reçoit la valeur bornée et arrondie.
bool parse (const Range&, const std::string& text, double& out);
// Valeur après un déplacement horizontal de `dx` pixels depuis `startNormalised` (la valeur de départ en 0..1) ; `pixelsFullRange` : la largeur qui
// couvre toute l'étendue (au moins 20).
double dragged (const Range&, double startNormalised, double dx, double pixelsFullRange, bool fine) noexcept;
// Un cran de molette : +1 / -1 (fin : dixième). Le pas de l'étendue, ou un centième de l'étendue si elle est continue.
double wheel (const Range&, double value, int notches, bool fine) noexcept;
} // namespace neptune::core::dragvalue
