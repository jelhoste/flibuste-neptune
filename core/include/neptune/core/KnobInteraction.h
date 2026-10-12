#pragma once
// Interaction du potard générique (palier 0) : machine à états pure, sans JUCE, pilotée par des événements
// « pointeur » (souris ou doigt), molette et clavier horodatés. L'adaptateur traduit les événements de la
// fenêtre et branche les actions (début/fin de geste, valeur, remise au défaut, modale).
//
// Gestes (décision de l'utilisateur, 2026-10-12) :
//   glisser (vertical)                 règle la valeur ; Shift ou bouton droit maintenu et déplacé = réglage fin
//   double-clic / double-tap           remet la valeur par défaut
//   appui long (clic gauche maintenu,  ouvre la modale (champ texte + slider) ; clic droit relâché sans bouger
//   doigt maintenu) ou clic droit      ouvre aussi la modale
//   molette                            pas de valeur ; Shift = pas fin
//   clavier                            flèches, PageUp/PageDown, Début/Fin ; Entrée = modale, Suppr = défaut
// Au tactile, pas de réglage fin au glissé : il passe par la modale.
#include <functional>

namespace neptune::core::ui
{
enum class PointerKind { Mouse, Touch };
enum class MouseButton { Left, Right };
enum class Key { Up, Down, Left, Right, PageUp, PageDown, Home, End, Enter, Delete };

// Toutes les valeurs de réglage au même endroit, pour les ajuster en testant. Seule la sensibilité du glissé
// vient de la maquette (0,006 par pixel) ; les autres sont des valeurs de départ à affiner à l'usage.
struct KnobConfig
{
    double dragPixelsForFullRange = 1.0 / 0.006; // maquette neptune_ui.html : v += dy * .006
    double fineRatio = 0.1;                      // le réglage fin multiplie le déplacement par ce facteur
    double wheelStep = 0.02;                     // par cran (normalisé)
    double wheelFineStep = 0.002;
    double wheelIdleMs = 400;                    // un train de crans = un seul geste d'annulation
    double keyStep = 0.01;
    double keyFineStep = 0.001;
    double keyCoarseStep = 0.1;                  // PageUp / PageDown
    double longPressMs = 500;
    double moveSlopMouse = 3;                    // en pixels : en deçà, ce n'est pas un glissé
    double moveSlopTouch = 8;
    double doubleTapMs = 400;
    double doubleTapRadiusMouse = 6;
    double doubleTapRadiusTouch = 24;
    double minTouchTarget = 44;                  // zone cliquable minimale, en pixels logiques
};

// Taille de la zone sensible : jamais plus petite que la cible tactile minimale, même si le dessin est plus petit.
constexpr double effectiveHitSize (double visualSize, const KnobConfig& c) { return visualSize < c.minTouchTarget ? c.minTouchTarget : visualSize; }

struct KnobCallbacks
{
    std::function<void()> beginGesture;
    std::function<void (double normalised)> setNormalised;
    std::function<void()> endGesture;
    std::function<void()> resetToDefault;
    std::function<void()> openModal;
};

class KnobInteraction
{
public:
    explicit KnobInteraction (KnobConfig config = {}) : cfg_ (config) {}

    void setCallbacks (KnobCallbacks cb) { cb_ = std::move (cb); }
    const KnobConfig& config() const noexcept { return cfg_; }

    // Valeur normalisée courante (à rappeler quand elle change par ailleurs : hôte, annulation, modale).
    void setValue (double normalised) noexcept { value_ = clamp01 (normalised); }
    double value() const noexcept { return value_; }
    // Nombre de pas d'un paramètre discret (0 = continu) : les flèches et la molette avancent d'un pas.
    void setSteps (int steps) noexcept { steps_ = steps < 0 ? 0 : steps; }

    // Temps en millisecondes (horloge monotone), coordonnées en pixels, y vers le bas.
    void pointerDown (MouseButton button, PointerKind kind, bool shift, double x, double y, double timeMs);
    void pointerMove (bool shift, double x, double y, double timeMs);
    void pointerUp (double timeMs);
    void pointerCancel(); // perte de capture : termine proprement un geste en cours
    void tick (double timeMs); // à appeler régulièrement (≈ 30 Hz) : appui long, fin de train de molette
    void wheel (double notches, bool shift, double timeMs); // > 0 : vers le haut = augmente
    bool key (Key key, bool shift);                          // true si la touche est utilisée

    bool gestureOpen() const noexcept { return dragging_ || wheelOpen_; }

private:
    enum class State { Idle, Pressed, Dragging, Consumed };

    static double clamp01 (double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
    void emitBegin() { if (cb_.beginGesture) cb_.beginGesture(); }
    void emitEnd() { if (cb_.endGesture) cb_.endGesture(); }
    void emitValue (double v);
    void closeWheelGesture();
    void oneShot (double newValue);
    double slop() const noexcept { return kind_ == PointerKind::Touch ? cfg_.moveSlopTouch : cfg_.moveSlopMouse; }

    KnobConfig cfg_;
    KnobCallbacks cb_;
    double value_ = 0.0;
    int steps_ = 0;

    State state_ = State::Idle;
    MouseButton button_ = MouseButton::Left;
    PointerKind kind_ = PointerKind::Mouse;
    double downX_ = 0, downY_ = 0, downTime_ = 0;
    bool secondPress_ = false;
    bool dragging_ = false;
    bool fineActive_ = false;
    double anchorY_ = 0, anchorValue_ = 0;

    bool haveLastTap_ = false;
    double lastTapTime_ = 0, lastTapX_ = 0, lastTapY_ = 0;

    bool wheelOpen_ = false;
    double lastWheelTime_ = 0;
};
} // namespace neptune::core::ui
