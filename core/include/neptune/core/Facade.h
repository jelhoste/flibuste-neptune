#pragma once
// La Façade (spec §15) : interface personnalisée d'une piste. Une image de fond et des widgets posés librement, chacun lié à un
// paramètre existant (couche Paramètre générique) ; aucun nouveau moteur, seulement une vue alternative. La Façade fait partie des
// réglages de bande de la piste (StripSettings) : sauvegardée dans le projet, annulable comme les autres réglages de bande.
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neptune/core/Parameter.h"

namespace neptune::core::facade
{
// Taille logique du canevas : la vue le met à l'échelle de la place disponible.
constexpr int canvasWidth = 1000;
constexpr int canvasHeight = 600;
constexpr int maxWidgets = 200;
constexpr int minWidgetSize = 16;
constexpr std::size_t maxTextLength = 64;
constexpr std::size_t maxImagePath = 1024;

// Menu : liste de valeurs d'un paramètre à choix ou à pas. Action : instantané de valeurs (plusieurs paramètres restaurés d'un coup).
// Curve : courbe de transfert de vélocité de la piste (Sampler).
enum class WidgetType { Knob = 0, Button = 1, Label = 2, Menu = 3, Action = 4, Curve = 5 };
constexpr int numWidgetTypes = 6;
constexpr std::size_t maxSnapshot = 64;

// Ajustement de l'image de fond au canevas.
enum class ImageFit { Fit = 0, Stretch = 1, Fill = 2 }; // ajuster (bandes), étirer, remplir (rogner)
constexpr int numImageFits = 3;

struct Widget
{
    int id = 0;                     // unique dans la Façade (sélection, annulation)
    WidgetType type = WidgetType::Knob;
    int x = 0, y = 0, w = 80, h = 80; // dans le canevas
    ParamId param = 0;              // 0 : pas de paramètre (étiquette seule, ou widget non lié)
    std::string text;               // étiquette
    std::vector<std::pair<ParamId, double>> snapshot; // Action : valeurs réelles (non normalisées) à restaurer

    bool operator== (const Widget& o) const noexcept
    {
        return id == o.id && type == o.type && x == o.x && y == o.y && w == o.w && h == o.h && param == o.param && text == o.text && snapshot == o.snapshot;
    }
};

struct Facade
{
    std::string image;              // chemin du fichier image (PNG / JPG) ; vide : fond uni
    ImageFit fit = ImageFit::Fit;
    std::vector<Widget> widgets;    // l'ordre est l'ordre d'empilement (le dernier est dessus)

    bool operator== (const Facade& o) const noexcept { return image == o.image && fit == o.fit && widgets == o.widgets; }
    bool operator!= (const Facade& o) const noexcept { return ! (*this == o); }

    // Identifiant libre pour un nouveau widget.
    int freeId() const noexcept;
    const Widget* find (int id) const noexcept;
    Widget* find (int id) noexcept;
};

// Dimensions par défaut d'un nouveau widget du type donné.
Widget defaultWidget (WidgetType type, int id, int x, int y);

// Outils de mise en page sur un ensemble de widgets (identifiants). Sans effet sous deux widgets (aligner, répartir) ou si la
// sélection est vide ; le résultat reste dans le canevas.
enum class Align { Left, Right, Top, Bottom, CentreH, CentreV };
void alignWidgets (Facade&, const std::set<int>& ids, Align);
void distributeWidgets (Facade&, const std::set<int>& ids, bool horizontal); // espaces égaux entre widgets (≥ 3)
void arrangeGrid (Facade&, const std::set<int>& ids, int columns);           // matrice : pas régulier, dans l'ordre d'empilement

// Bornes appliquées : widgets dans le canevas et pas trop petits, identifiants uniques, textes courts, nombre limité.
Facade sanitised (Facade f);

std::string toJson (const Facade&);
Facade fromJson (std::string_view text); // lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées
} // namespace neptune::core::facade
