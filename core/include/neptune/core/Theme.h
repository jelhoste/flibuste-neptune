#pragma once
// Thèmes visuels (palier 0) : jetons de couleur des 4 thèmes de la maquette (design/neptune_ui.html), sans JUCE.
// Les valeurs sont copiées telles quelles de la maquette ; le réglage de contraste 0–4 est le port exact de
// design/nova-contrast.js (même algorithme : luminance relative WCAG + rapport cible par rôle).
// Les identifiants de thème ("dark", "hammond", "light", "hc") sont ceux de settings.json.
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace neptune::core::theme
{
enum class ThemeId { Dark, Hammond, Light, HighContrast };
constexpr int numThemes = 4;

std::optional<ThemeId> themeFromString (std::string_view id); // "dark", "hammond", "light", "hc"
const char* toString (ThemeId id);

enum class KnobStyle { Led, Pot };                    // maquette : KNOB_STYLE
enum class ControlStyle { Glass, Bezel, HighContrast }; // maquette : THEME_CTL (data-ctl)

struct Rgba
{
    std::uint8_t r = 0, g = 0, b = 0;
    double a = 1.0;
    bool operator== (const Rgba& o) const noexcept { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// Un jeton est une couleur unie ou un dégradé vertical (haut -> bas, ex. en-tête du thème bois).
// Le réglage de contraste ignore les dégradés (comme nova-contrast.js).
struct Paint
{
    Rgba from;
    Rgba to;
    bool gradient = false;
};

// Noms CSS de la maquette entre parenthèses : tokenCssName().
enum class Token
{
    BgApp, BgCard, Border, Accent, Accent2, Pink, Pink2, Text, TextDim, TextDim2, Grid, StageBg, Glow1, Glow2,
    PanelBg, Pb, Pbt, HeaderBg, BottomBg, Nav1, Nav2, Navb, Ov, Hring,
    KnobFace, KnobTrack, KnobDot, KnobHot, KnobHot2,
    Band1, Band2, Band3, Handle, LogoInk,
    CtlBd,
    KnobPotFace1, KnobPotFace2, KnobPotFace3, KnobPotRing, KnobPotPointer, KnobPotCenter, // thème bois seulement
    Count
};
constexpr int numTokens = static_cast<int> (Token::Count);
const char* tokenCssName (Token t); // "--bg-app", ...

struct Metrics
{
    int cornerRadius;     // --cr
    int frameRadius;      // --fr
    int pillRadius;       // --pill
    int iconButtonRadius; // --ibr ; -1 = cercle complet (50 %)
    int scrollbarRadius;  // --sbr
    int borderWidth;      // 1, ou 2 en haut contraste
};

struct Palette
{
    std::array<Paint, numTokens> paints {};
    const Paint& paint (Token t) const noexcept { return paints[static_cast<size_t> (t)]; }
    // Couleur unie d'un jeton ; pour un dégradé, sa première couleur.
    const Rgba& color (Token t) const noexcept { return paint (t).from; }
};

struct ThemeDefinition
{
    ThemeId id;
    KnobStyle knob;
    ControlStyle control;
    Metrics metrics;
    Palette palette; // contraste 0
};

const ThemeDefinition& definition (ThemeId id);

// Palette du thème avec le réglage de contraste (0..4, borné). Le thème haut contraste n'est jamais modifié.
Palette withContrast (const ThemeDefinition& def, int level);

// Rapport de contraste WCAG entre deux couleurs opaques (>= 1).
double contrastRatio (const Rgba& a, const Rgba& b) noexcept;
} // namespace neptune::core::theme
