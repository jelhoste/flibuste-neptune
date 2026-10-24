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
// Zone : conteneur (cadre avec titre) qui possède les widgets posés dedans et, au choix, les range en ligne, en colonne ou en matrice.
// Keyboard : clavier virtuel lié à un canal MIDI de la piste (un manuel d'orgue = un clavier) ; allume les notes jouées sur ce canal.
// Slider : curseur (tirant) horizontal ou vertical lié à un paramètre ; crans automatiques sur un paramètre à pas.
enum class WidgetType { Knob = 0, Button = 1, Label = 2, Menu = 3, Action = 4, Curve = 5, Zone = 6, Keyboard = 7, Slider = 8 };
constexpr int numWidgetTypes = 9;
constexpr int maxStyles = 64;
constexpr int maxStripFrames = 512;

// Style d'images (PNG, JPG ou SVG), réutilisable par plusieurs widgets. Chaque widget n'utilise que ce qui le concerne ; une image absente
// laisse le dessin d'origine.
//  - Bouton, Action : `off` et `on` (deux états, par exemple un tirant fermé et un tirant ouvert).
//  - Curseur : `track` (le rail) et `handle` (la poignée).
//  - Potard : `strip`, une bande de `frames` images (empilées verticalement, ou côte à côte si `stripHorizontal`), une par position.
struct ImageStyle
{
    int id = 0;
    std::string name;
    std::string off, on, track, handle, strip;
    int frames = 0;
    bool stripHorizontal = false;

    bool operator== (const ImageStyle& o) const noexcept
    {
        return id == o.id && name == o.name && off == o.off && on == o.on && track == o.track && handle == o.handle && strip == o.strip
            && frames == o.frames && stripHorizontal == o.stripHorizontal;
    }
};
constexpr int zonePad = 10;          // marge intérieure d'une zone
constexpr int zoneTitleHeight = 24;  // bandeau du titre (quand la zone a un titre)

// Rangement du contenu d'une zone. `align` : sur l'axe transversal (ligne : haut / centre / bas ; colonne : gauche / centre / droite ;
// matrice : dans la cellule, sur les deux axes). `justify` : sur l'axe principal (début / centre / fin / répartition égale).
// Cercle : le contenu est réparti à angles égaux sur un cercle, dans le sens horaire ; `justify` donne la place du premier
// (0 à gauche, 1 en haut, 2 à droite, 3 en bas).
enum class ZoneLayout { Free = 0, Row = 1, Column = 2, Grid = 3, Circle = 4 };
constexpr int numZoneLayouts = 5;
enum class CircleStart { Left = 0, Top = 1, Right = 2, Bottom = 3 };
enum class CrossAlign { Start = 0, Centre = 1, End = 2 };
enum class Justify { Start = 0, Centre = 1, End = 2, Spread = 3 };
constexpr int maxZoneGap = 64;
constexpr int maxZoneColumns = 12;
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
    int parent = 0;                 // identifiant de la zone qui contient ce widget, 0 : aucune
    int layout = 0;                 // Zone : ZoneLayout
    int align = 0, justify = 0;     // Zone : CrossAlign, Justify
    int gap = 8;                    // Zone : espace entre les widgets rangés
    int columns = 2;                // Zone en matrice : nombre de colonnes
    int channel = 1;                // Clavier : canal MIDI 1..16
    int firstNote = 36;             // Clavier : première touche affichée à gauche (do) ; le nombre de touches suit la largeur
    int hold = 0;                   // Clavier : 1, une touche cliquée reste enfoncée jusqu'au clic suivant
    int velocity = 100;             // Clavier : vélocité envoyée, 1..127
    int style = 0;                  // identifiant d'un ImageStyle de la Façade, 0 : aucun
    int vertical = 0;               // Curseur : 1, vertical

    bool operator== (const Widget& o) const noexcept
    {
        return id == o.id && type == o.type && x == o.x && y == o.y && w == o.w && h == o.h && param == o.param && text == o.text && snapshot == o.snapshot
            && parent == o.parent && layout == o.layout && align == o.align && justify == o.justify && gap == o.gap && columns == o.columns
            && channel == o.channel && firstNote == o.firstNote && hold == o.hold && velocity == o.velocity
            && style == o.style && vertical == o.vertical;
    }
};

struct Facade
{
    std::string image;              // chemin du fichier image (PNG / JPG) ; vide : fond uni
    ImageFit fit = ImageFit::Fit;
    std::vector<Widget> widgets;    // l'ordre est l'ordre d'empilement (le dernier est dessus)
    std::vector<ImageStyle> styles; // bibliothèque de styles d'images

    bool operator== (const Facade& o) const noexcept { return image == o.image && fit == o.fit && widgets == o.widgets && styles == o.styles; }
    bool operator!= (const Facade& o) const noexcept { return ! (*this == o); }

    // Identifiant libre pour un nouveau widget.
    int freeId() const noexcept;
    int freeStyleId() const noexcept;
    const ImageStyle* findStyle (int id) const noexcept;
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
void arrangeGrid (Facade&, const std::set<int>& ids, int columns);
// Répartit les widgets à angles égaux sur un cercle (sens horaire) dont le centre et le rayon viennent de l'espace qu'ils occupent.
// `refId` (un des widgets) va à la place `start` ; les autres suivent dans l'ordre où ils sont déjà autour du centre.
void arrangeCircle (Facade&, const std::set<int>& ids, int refId, CircleStart start);           // matrice : pas régulier, dans l'ordre d'empilement

// ---- Zones ----
// Tous les widgets contenus (directement ou non) dans la zone `id`.
std::set<int> descendants (const Facade&, int id);
// Rattache chaque widget de `touched` à la plus petite zone qui contient son centre (0 si aucune) ; si une zone a bougé ou changé de
// taille, les widgets libres ou déjà à elle sont réexaminés. Les autres rattachements ne changent pas.
void assignParents (Facade&, const std::set<int>& touched);
// Range le contenu de chaque zone ayant une disposition (ligne, colonne, matrice) ; déplace les zones imbriquées avec leur contenu.
void applyLayouts (Facade&);
// assignParents puis applyLayouts : à appeler après toute modification de géométrie.
void arrange (Facade&, const std::set<int>& touched);
// Déplace le widget (et le contenu d'une zone) de dx, dy.
void moveWidgets (Facade&, const std::set<int>& ids, int dx, int dy);
// Copie les widgets (avec le contenu des zones) décalés de `offset` ; rend les nouveaux identifiants.
std::set<int> duplicateWidgets (Facade&, const std::set<int>& ids, int offset);
// Donne à tous les widgets du même type que `id` la taille de ce widget.
void matchSize (Facade&, int id);

// Bornes appliquées : widgets dans le canevas et pas trop petits, identifiants uniques, textes courts, nombre limité.
Facade sanitised (Facade f);

std::string toJson (const Facade&);
Facade fromJson (std::string_view text); // lecture tolérante : champ invalide ou absent -> défaut ; bornes appliquées
} // namespace neptune::core::facade
