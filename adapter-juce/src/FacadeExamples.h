#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

// Modèles et exemples de styles d'images de la Façade : des dessins SVG simples (modifiables dans n'importe quel éditeur de dessin)
// qui montrent les proportions et la disposition attendues, rangés comme un pack (un dossier par style). Écrits dans un dossier au choix
// de l'utilisateur, avec pack.json et SPECIFICATION.md ; le dossier peut être zippé tel quel en .neptunestyle.
namespace facade_examples
{
struct File
{
    juce::String name, content;
};

constexpr int stripFrames = 32;   // images de la bande de potard d'exemple
constexpr int stripFrameSize = 64;

inline juce::String svgHead (int w, int h, const juce::String& note)
{
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + juce::String (w) + "\" height=\"" + juce::String (h) + "\" viewBox=\"0 0 " + juce::String (w) + " " + juce::String (h) + "\">\n"
         + "  <!-- " + note + " -->\n";
}

inline std::vector<File> files (bool french)
{
    std::vector<File> out;
    auto note = [french] (const char* en, const char* fr) { return juce::String::fromUTF8 (french ? fr : en); };

    out.push_back ({ "drawbar-tab/off.svg", svgHead (120, 40, note ("TEMPLATE - drawbar tab, closed (off). 120 x 40, ratio 3:1. Keep the same size and ratio for on.svg; Neptune scales it to the widget.",
                                                                  "MODELE - plaquette de tirant, fermee (eteint). 120 x 40, rapport 3:1. Gardez la meme taille et le meme rapport pour on.svg ; Neptune l'adapte au widget."))
        + "  <rect x=\"2\" y=\"2\" width=\"116\" height=\"36\" rx=\"6\" fill=\"#d9d2bd\" stroke=\"#3a3a3a\" stroke-width=\"2\"/>\n"
          "  <rect x=\"8\" y=\"8\" width=\"104\" height=\"24\" rx=\"3\" fill=\"#b9b19a\"/>\n"
          "  <line x1=\"20\" y1=\"20\" x2=\"100\" y2=\"20\" stroke=\"#6a6450\" stroke-width=\"3\"/>\n</svg>\n" });
    out.push_back ({ "drawbar-tab/on.svg", svgHead (120, 40, note ("TEMPLATE - drawbar tab, open (on). Same size as the closed one.", "MODELE - plaquette de tirant, ouverte (allume). Meme taille que la fermee."))
        + "  <rect x=\"6\" y=\"6\" width=\"112\" height=\"32\" rx=\"6\" fill=\"#5a5546\"/>\n"
          "  <rect x=\"2\" y=\"2\" width=\"112\" height=\"32\" rx=\"6\" fill=\"#f1ead6\" stroke=\"#3a3a3a\" stroke-width=\"2\"/>\n"
          "  <rect x=\"8\" y=\"8\" width=\"100\" height=\"20\" rx=\"3\" fill=\"#d9d2bd\"/>\n"
          "  <line x1=\"20\" y1=\"18\" x2=\"96\" y2=\"18\" stroke=\"#b3261e\" stroke-width=\"4\"/>\n</svg>\n" });

    out.push_back ({ "slider-horizontal/track.svg", svgHead (300, 24, note ("TEMPLATE - horizontal slider rail, 300 x 24. The handle slides along it; the 9 marks suit a drawbar with 9 notches.",
                                                          "MODELE - rail de curseur horizontal, 300 x 24. La poignee glisse dessus ; les 9 reperes conviennent a un tirant a 9 crans."))
        + "  <rect x=\"0\" y=\"0\" width=\"300\" height=\"24\" rx=\"12\" fill=\"#2a2a2a\"/>\n"
          "  <rect x=\"4\" y=\"8\" width=\"292\" height=\"8\" rx=\"4\" fill=\"#0e0e0e\"/>\n" + [] { juce::String ticks; for (int i = 0; i < 9; ++i) ticks << "  <line x1=\"" << juce::String (12.0 + i * 34.5, 1) << "\" y1=\"2\" x2=\"" << juce::String (12.0 + i * 34.5, 1) << "\" y2=\"6\" stroke=\"#8a8a8a\" stroke-width=\"2\"/>\n"; return ticks; }()
        + "</svg>\n" });
    out.push_back ({ "slider-horizontal/handle.svg", svgHead (24, 48, note ("TEMPLATE - horizontal slider handle, 24 x 48 (ratio 1:2). Its height is the height of the slider.",
                                                            "MODELE - poignee de curseur horizontal, 24 x 48 (rapport 1:2). Sa hauteur est celle du curseur."))
        + "  <rect x=\"1\" y=\"1\" width=\"22\" height=\"46\" rx=\"5\" fill=\"#c8c0a8\" stroke=\"#333333\" stroke-width=\"2\"/>\n"
          "  <line x1=\"6\" y1=\"18\" x2=\"18\" y2=\"18\" stroke=\"#555555\" stroke-width=\"2\"/>\n"
          "  <line x1=\"6\" y1=\"24\" x2=\"18\" y2=\"24\" stroke=\"#555555\" stroke-width=\"2\"/>\n"
          "  <line x1=\"6\" y1=\"30\" x2=\"18\" y2=\"30\" stroke=\"#555555\" stroke-width=\"2\"/>\n</svg>\n" });
    out.push_back ({ "slider-vertical/track.svg", svgHead (24, 300, note ("TEMPLATE - vertical slider rail, 24 x 300 (the horizontal one turned).", "MODELE - rail de curseur vertical, 24 x 300 (l'horizontal tourne)."))
        + "  <rect x=\"0\" y=\"0\" width=\"24\" height=\"300\" rx=\"12\" fill=\"#2a2a2a\"/>\n"
          "  <rect x=\"8\" y=\"4\" width=\"8\" height=\"292\" rx=\"4\" fill=\"#0e0e0e\"/>\n" + [] { juce::String ticks; for (int i = 0; i < 9; ++i) ticks << "  <line x1=\"2\" y1=\"" << juce::String (12.0 + i * 34.5, 1) << "\" x2=\"6\" y2=\"" << juce::String (12.0 + i * 34.5, 1) << "\" stroke=\"#8a8a8a\" stroke-width=\"2\"/>\n"; return ticks; }()
        + "</svg>\n" });
    out.push_back ({ "slider-vertical/handle.svg", svgHead (48, 24, note ("TEMPLATE - vertical slider handle, 48 x 24 (ratio 2:1). Its width is the width of the slider.",
                                                                    "MODELE - poignee de curseur vertical, 48 x 24 (rapport 2:1). Sa largeur est celle du curseur."))
        + "  <rect x=\"1\" y=\"1\" width=\"46\" height=\"22\" rx=\"5\" fill=\"#c8c0a8\" stroke=\"#333333\" stroke-width=\"2\"/>\n"
          "  <line x1=\"18\" y1=\"6\" x2=\"18\" y2=\"18\" stroke=\"#555555\" stroke-width=\"2\"/>\n"
          "  <line x1=\"24\" y1=\"6\" x2=\"24\" y2=\"18\" stroke=\"#555555\" stroke-width=\"2\"/>\n"
          "  <line x1=\"30\" y1=\"6\" x2=\"30\" y2=\"18\" stroke=\"#555555\" stroke-width=\"2\"/>\n</svg>\n" });

    out.push_back ({ "button/off.svg", svgHead (96, 40, note ("TEMPLATE - button, off. 96 x 40. Same size for on.svg.", "MODELE - bouton, eteint. 96 x 40. Meme taille pour on.svg."))
        + "  <rect x=\"2\" y=\"2\" width=\"92\" height=\"36\" rx=\"18\" fill=\"#4a4a4a\" stroke=\"#222222\" stroke-width=\"2\"/>\n"
          "  <circle cx=\"20\" cy=\"20\" r=\"7\" fill=\"#2a2a2a\"/>\n</svg>\n" });
    out.push_back ({ "button/on.svg", svgHead (96, 40, note ("TEMPLATE - button, on.", "MODELE - bouton, allume."))
        + "  <rect x=\"2\" y=\"2\" width=\"92\" height=\"36\" rx=\"18\" fill=\"#e0a030\" stroke=\"#222222\" stroke-width=\"2\"/>\n"
          "  <circle cx=\"20\" cy=\"20\" r=\"7\" fill=\"#fff3c4\"/>\n</svg>\n" });

    // Etats supplementaires du bouton : souris dessus et non lie.
    out.push_back ({ "button/off_hover.svg", svgHead (96, 40, note ("TEMPLATE - button, off, mouse over. Same size as off.svg.", "MODELE - bouton, eteint, souris dessus. Meme taille que off.svg."))
        + "  <rect x=\"2\" y=\"2\" width=\"92\" height=\"36\" rx=\"18\" fill=\"#5e5e5e\" stroke=\"#222222\" stroke-width=\"2\"/>\n"
          "  <circle cx=\"20\" cy=\"20\" r=\"7\" fill=\"#3a3a3a\"/>\n</svg>\n" });
    out.push_back ({ "button/on_hover.svg", svgHead (96, 40, note ("TEMPLATE - button, on, mouse over.", "MODELE - bouton, allume, souris dessus."))
        + "  <rect x=\"2\" y=\"2\" width=\"92\" height=\"36\" rx=\"18\" fill=\"#f0b84a\" stroke=\"#222222\" stroke-width=\"2\"/>\n"
          "  <circle cx=\"20\" cy=\"20\" r=\"7\" fill=\"#fff8dc\"/>\n</svg>\n" });
    out.push_back ({ "button/disabled.svg", svgHead (96, 40, note ("TEMPLATE - button not linked to anything (shown in Play mode).", "MODELE - bouton lie a rien (affiche en mode Jeu)."))
        + "  <rect x=\"2\" y=\"2\" width=\"92\" height=\"36\" rx=\"18\" fill=\"#2c2c2c\" stroke=\"#444444\" stroke-width=\"2\" stroke-dasharray=\"4 3\"/>\n</svg>\n" });
    out.push_back ({ "button/style.json", "{\n  \"name\": \"Button with states\",\n  \"text\": { \"colour\": \"#ffffff\", \"size\": 45, \"position\": \"center\", \"bold\": true }\n}\n" });
    out.push_back ({ "drawbar-tab/style.json", "{\n  \"name\": \"Drawbar tab\",\n  \"text\": { \"colour\": \"#2a2418\", \"size\": 42, \"position\": \"center\", \"bold\": true }\n}\n" });

    // Potard en deux couches : un corps fixe et une aiguille que Neptune fait tourner.
    out.push_back ({ "knob-layered/body.svg", svgHead (64, 64, note ("TEMPLATE - knob body (fixed layer), 64 x 64. Same size as pointer.svg. The colours {{accent}}, {{text}}, {{bg}}... follow the Neptune theme.",
                                                                       "MODELE - corps du potard (couche fixe), 64 x 64. Meme taille que pointer.svg. Les couleurs {{accent}}, {{text}}, {{bg}}... suivent le theme de Neptune."))
        + "  <circle cx=\"32\" cy=\"32\" r=\"28\" fill=\"#3a3a3a\" stroke=\"{{border}}\" stroke-width=\"2\"/>\n"
          "  <circle cx=\"32\" cy=\"32\" r=\"20\" fill=\"#2a2a2a\"/>\n</svg>\n" });
    out.push_back ({ "knob-layered/pointer.svg", svgHead (64, 64, note ("TEMPLATE - knob pointer (turned layer), 64 x 64, drawn pointing UP (0 degrees); Neptune turns it around its centre between -135 and +135 degrees.",
                                                                          "MODELE - aiguille du potard (couche tournee), 64 x 64, dessinee vers le HAUT (0 degre) ; Neptune la tourne autour de son centre entre -135 et +135 degres."))
        + "  <line x1=\"32\" y1=\"32\" x2=\"32\" y2=\"10\" stroke=\"{{accent}}\" stroke-width=\"4\" stroke-linecap=\"round\"/>\n</svg>\n" });
    out.push_back ({ "knob-layered/style.json", "{\n  \"name\": \"Knob, two layers\",\n  \"pointer_angles\": [-135, 135],\n  \"pivot\": [50, 50]\n}\n" });

    // Clavier : les touches (la forme du dessin donne la forme de la touche).
    out.push_back ({ "keyboard/key_white.svg", svgHead (22, 110, note ("TEMPLATE - white key at rest, 22 x 110 (stretched to each key). Rounded lower corners.", "MODELE - touche blanche au repos, 22 x 110 (etiree sur chaque touche). Coins inferieurs arrondis."))
        + "  <path d=\"M1 0 H21 V102 Q21 109 14 109 H8 Q1 109 1 102 Z\" fill=\"#f4f0e4\" stroke=\"#555555\" stroke-width=\"1.5\"/>\n</svg>\n" });
    out.push_back ({ "keyboard/key_white_on.svg", svgHead (22, 110, note ("TEMPLATE - white key pressed or played.", "MODELE - touche blanche enfoncee ou jouee."))
        + "  <path d=\"M1 0 H21 V104 Q21 109 16 109 H6 Q1 109 1 104 Z\" fill=\"#e0b04a\" stroke=\"#555555\" stroke-width=\"1.5\"/>\n</svg>\n" });
    out.push_back ({ "keyboard/key_black.svg", svgHead (13, 66, note ("TEMPLATE - black key at rest, 13 x 66.", "MODELE - touche noire au repos, 13 x 66."))
        + "  <path d=\"M0.5 0 H12.5 V60 Q12.5 65 8 65 H5 Q0.5 65 0.5 60 Z\" fill=\"#1a1a1a\" stroke=\"#000000\" stroke-width=\"1\"/>\n</svg>\n" });
    out.push_back ({ "keyboard/key_black_on.svg", svgHead (13, 66, note ("TEMPLATE - black key pressed or played.", "MODELE - touche noire enfoncee ou jouee."))
        + "  <path d=\"M0.5 0 H12.5 V62 Q12.5 65 9 65 H4 Q0.5 65 0.5 62 Z\" fill=\"#b8862b\" stroke=\"#000000\" stroke-width=\"1\"/>\n</svg>\n" });
    out.push_back ({ "keyboard/style.json", "{\n  \"name\": \"Keyboard keys\",\n  \"key_lit_colour\": \"#f0b429\"\n}\n" });

    // Cadre de zone decoupe en neuf zones : les coins gardent leur taille, le reste s'etire.
    out.push_back ({ "panel/frame.svg", svgHead (120, 80, note ("TEMPLATE - zone frame, 120 x 80, cut in nine parts by style.json: slice = [20, 20, 20, 20] means 20 % of the width/height on each side. The four corners keep their size; the edges and the centre stretch.",
                                                                  "MODELE - cadre de zone, 120 x 80, decoupe en neuf zones par style.json : slice = [20, 20, 20, 20] veut dire 20 % de la largeur/hauteur de chaque cote. Les quatre coins gardent leur taille ; les bords et le centre s'etirent."))
        + "  <rect x=\"2\" y=\"2\" width=\"116\" height=\"76\" rx=\"14\" fill=\"#262626\" stroke=\"{{accent}}\" stroke-width=\"3\"/>\n</svg>\n" });
    out.push_back ({ "panel/style.json", "{\n  \"name\": \"Zone frame\",\n  \"slice\": [20, 20, 20, 20],\n  \"slice_scale\": 100,\n  \"text\": { \"colour\": \"#cccccc\", \"position\": \"left\", \"bold\": true }\n}\n" });

    // Voyant : eteint / allume (le vert suit la couleur d'accent du theme).
    out.push_back ({ "lamp/off.svg", svgHead (40, 40, note ("TEMPLATE - lamp, off. 40 x 40.", "MODELE - voyant, eteint. 40 x 40."))
        + "  <circle cx=\"20\" cy=\"20\" r=\"14\" fill=\"#1c1c1c\" stroke=\"#555555\" stroke-width=\"2\"/>\n</svg>\n" });
    out.push_back ({ "lamp/on.svg", svgHead (40, 40, note ("TEMPLATE - lamp, on: {{accent}} follows the theme.", "MODELE - voyant, allume : {{accent}} suit le theme."))
        + "  <circle cx=\"20\" cy=\"20\" r=\"18\" fill=\"{{accent}}\" fill-opacity=\"0.25\"/>\n"
          "  <circle cx=\"20\" cy=\"20\" r=\"14\" fill=\"{{accent}}\" stroke=\"#555555\" stroke-width=\"2\"/>\n</svg>\n" });

    // Bande de potard : stripFrames images 64 x 64 empilees verticalement, de la position minimale (en haut) a la maximale (en bas).
    {
        const int total = stripFrames * stripFrameSize;
        juce::String svg = svgHead (stripFrameSize, total, note ("TEMPLATE - knob strip: 32 frames of 64 x 64 stacked vertically (frame 1 at the top = minimum, last frame at the bottom = maximum). Set 'Strip frames' to the number of frames in the Styles window.",
                                                                 "MODELE - bande de potard : 32 images de 64 x 64 empilees verticalement (la 1re en haut = minimum, la derniere en bas = maximum). Reglez 'Images de la bande' dans la fenetre Styles."));
        for (int i = 0; i < stripFrames; ++i)
        {
            const double angle = (-135.0 + 270.0 * i / (stripFrames - 1)) * 3.14159265358979 / 180.0;
            const double cy = i * stripFrameSize + 32.0;
            const double x2 = 32.0 + 22.0 * std::sin (angle), y2 = cy - 22.0 * std::cos (angle);
            svg << "  <circle cx=\"32\" cy=\"" << juce::String (cy, 1) << "\" r=\"28\" fill=\"#3a3a3a\" stroke=\"#9a9a9a\" stroke-width=\"2\"/>\n"
                << "  <line x1=\"32\" y1=\"" << juce::String (cy, 1) << "\" x2=\"" << juce::String (x2, 2) << "\" y2=\"" << juce::String (y2, 2) << "\" stroke=\"#f0b429\" stroke-width=\"4\" stroke-linecap=\"round\"/>\n";
        }
        svg << "</svg>\n";
        out.push_back ({ "knob/strip_32.svg", svg });
    }

    out.push_back ({ "pack.json", "{\n  \"format\": 1,\n  \"name\": \"Neptune examples\",\n  \"author\": \"Neptune\",\n  \"licence\": \"Same licence as Neptune (AGPL-3.0)\",\n  \"version\": \"1.0\",\n  \"description\": \"Templates: edit the SVG files, then zip the folder as a .neptunestyle pack.\"\n}\n" });
    return out;
}

} // namespace facade_examples
