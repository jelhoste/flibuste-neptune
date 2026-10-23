#pragma once
// La Façade (spec §15) : interface personnalisée d'une piste. Une image de fond et des widgets posés librement, chacun lié à un
// paramètre existant (couche Paramètre générique) ; aucun nouveau moteur, seulement une vue alternative. La Façade fait partie des
// réglages de bande de la piste (StripSettings) : sauvegardée dans le projet, annulable comme les autres réglages de bande.
#include <string>
#include <string_view>
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

enum class WidgetType { Knob = 0, Button = 1, Label = 2 };
constexpr int numWidgetTypes = 3;

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

    bool operator== (const Widget& o) const noexcept
    {
        return id == o.id && type == o.type && x == o.x && y == o.y && w == o.w && h == o.h && param == o.param && text == o.text;
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

// Bornes appliquées : widgets dans le canevas et pas trop petits, identifiants uniques, textes courts, nombre limité.
Facade sanitised (Facade f);

std::string toJson (const Facade&);
Facade fromJson (std::string_view text); // lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées
} // namespace neptune::core::facade
