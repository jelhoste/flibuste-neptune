#pragma once
// Disposition de l'enveloppe à l'écran (palier 1) : une seule courbe continue, dont chaque segment a une largeur qui
// reflète sa durée de façon approximative (échelle logarithmique : 10 ms, 100 ms et 1 s se distinguent sans qu'un decay long
// écrase le reste). Un segment désactivé ou à 0 ms a une largeur nulle : le segment suivant démarre tout de suite.
// Calcul pur, sans interface : testé comme le reste du cœur.
//
// Pendant un glissé on fige l'échelle (frozenScale) : ce qui est avant le segment touché ne bouge pas, seule sa fin suit
// le pointeur, ce qui suit se décale sans être redimensionné ; au relâchement on recalcule pour tout faire tenir.
#include "neptune/core/Envelope.h"

namespace neptune::core
{
constexpr double envelopeMinSegmentPx = 8.0;     // largeur minimale d'un segment actif de durée non nulle
constexpr double envelopeMaxPxPerUnit = 30.0;    // échelle maximale : une enveloppe courte ne s'étire pas
constexpr double envelopePlateauUnits = 2.0;     // largeur du plateau de sustain, en unités
constexpr double envelopeMinPlateauPx = 24.0;
constexpr double envelopeTimeConstantMs = 8.0;   // u(t) = ln(1 + t / 8 ms)

struct EnvelopeLayout
{
    // Segments : 0 délai, 1 attaque, 2 hold, 3 decay, 4 relâchement. Bornes en pixels depuis le bord gauche.
    double x0[5] {}, x1[5] {};
    bool active[5] {};          // activé ET de durée non nulle (donc de largeur non nulle)
    double plateauX0 = 0.0, plateauX1 = 0.0; // sustain, entre le decay et le relâchement
    double endX = 0.0;          // fin de la courbe
    double scale = 0.0;         // pixels par unité ; à figer pendant un glissé
};

// `frozenScale` > 0 : utilise cette échelle telle quelle (la courbe peut alors dépasser `availableWidth`).
EnvelopeLayout computeEnvelopeLayout (const EnvelopeDescription& e, double availableWidth, double frozenScale = 0.0) noexcept;

// Durée (ms) d'un segment dont le point de fin est à `widthPx` de son début, à l'échelle `scale`.
// En deçà de la moitié de la largeur minimale : 0 ms.
double envelopeTimeFromWidth (double widthPx, double scale) noexcept;

// Variation d'une durée en la faisant glisser de `dxPx` pixels (étiquettes) : 20 px par unité logarithmique.
double envelopeScrubTime (double ms, double dxPx) noexcept;
} // namespace neptune::core
