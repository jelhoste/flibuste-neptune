#include "neptune/core/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace neptune::core::theme
{
namespace
{
constexpr int kLevels = 4;

const char* const kTokenNames[numTokens] = {
    "--bg-app", "--bg-card", "--border", "--accent", "--accent-2", "--pink", "--pink-2", "--text", "--text-dim", "--text-dim2",
    "--grid", "--stage-bg", "--glow1", "--glow2", "--panel-bg", "--pb", "--pbt", "--header-bg", "--bottom-bg", "--nav1",
    "--nav2", "--navb", "--ov", "--hring", "--knob-face", "--knob-track", "--knob-dot", "--knob-hot", "--knob-hot-2",
    "--band-1", "--band-2", "--band-3", "--handle", "--logo-ink", "--ctl-bd", "--knob-pot-face-1", "--knob-pot-face-2",
    "--knob-pot-face-3", "--knob-pot-ring", "--knob-pot-pointer", "--knob-pot-center",
};

int hexDigit (char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); }

// Valeurs écrites comme dans le CSS de la maquette : #rgb, #rgba, #rrggbb, rgba(r,g,b,a),
// linear-gradient(180deg,#xxxxxx,#xxxxxx). Une valeur illisible est une erreur de programmation (tests).
bool parseColor (std::string_view s, Rgba& out)
{
    if (s.size() >= 4 && s[0] == '#')
    {
        const std::string_view h = s.substr (1);
        int v[8];
        if (h.size() > 8) return false;
        for (size_t i = 0; i < h.size(); ++i)
            if ((v[i] = hexDigit (h[i])) < 0) return false;
        if (h.size() == 3 || h.size() == 4)
        {
            out = { static_cast<std::uint8_t> (v[0] * 17), static_cast<std::uint8_t> (v[1] * 17), static_cast<std::uint8_t> (v[2] * 17),
                    h.size() == 4 ? v[3] * 17 / 255.0 : 1.0 };
            return true;
        }
        if (h.size() == 6)
        {
            out = { static_cast<std::uint8_t> (v[0] * 16 + v[1]), static_cast<std::uint8_t> (v[2] * 16 + v[3]),
                    static_cast<std::uint8_t> (v[4] * 16 + v[5]), 1.0 };
            return true;
        }
        return false;
    }
    if (s.rfind ("rgba(", 0) == 0 && s.back() == ')')
    {
        const std::string body (s.substr (5, s.size() - 6));
        double p[4];
        const char* c = body.c_str();
        for (int i = 0; i < 4; ++i)
        {
            char* end = nullptr;
            p[i] = std::strtod (c, &end);
            if (end == c) return false;
            c = end;
            if (i < 3) { if (*c != ',') return false; ++c; }
        }
        out = { static_cast<std::uint8_t> (p[0]), static_cast<std::uint8_t> (p[1]), static_cast<std::uint8_t> (p[2]), p[3] };
        return true;
    }
    return false;
}

bool parsePaint (std::string_view s, Paint& out)
{
    constexpr std::string_view grad = "linear-gradient(180deg,";
    if (s.rfind (grad, 0) == 0 && s.back() == ')')
    {
        const std::string_view body = s.substr (grad.size(), s.size() - grad.size() - 1);
        const auto comma = body.find (',');
        if (comma == std::string_view::npos) return false;
        out.gradient = true;
        return parseColor (body.substr (0, comma), out.from) && parseColor (body.substr (comma + 1), out.to);
    }
    out.gradient = false;
    if (! parseColor (s, out.from)) return false;
    out.to = out.from;
    return true;
}

struct Css { Token token; const char* value; };

void apply (Palette& p, const std::vector<Css>& list)
{
    for (const auto& c : list)
    {
        Paint paint;
        if (! parsePaint (c.value, paint)) std::abort(); // table interne : jamais atteint (test de cohérence)
        p.paints[static_cast<size_t> (c.token)] = paint;
    }
}

// :root de la maquette = thème sombre ; les autres thèmes en héritent ce qu'ils ne redéfinissent pas.
const std::vector<Css>& rootTokens()
{
    static const std::vector<Css> v = {
        { Token::BgApp, "#0c0b0a" }, { Token::BgCard, "#1a1815" }, { Token::Border, "#2c2823" }, { Token::Accent, "#e8823f" },
        { Token::Accent2, "#f0a06a" }, { Token::Pink, "#d97e78" }, { Token::Pink2, "#e39a94" }, { Token::Text, "#f1ede6" },
        { Token::TextDim, "#9a9088" }, { Token::TextDim2, "#6f665f" }, { Token::Grid, "#322d28" }, { Token::StageBg, "#050505" },
        { Token::Glow1, "rgba(232,130,63,.1)" }, { Token::Glow2, "rgba(217,126,120,.1)" }, { Token::PanelBg, "rgba(28,25,22,.62)" },
        { Token::Pb, "rgba(255,255,255,.06)" }, { Token::Pbt, "rgba(255,255,255,.1)" }, { Token::HeaderBg, "rgba(23,21,19,.5)" },
        { Token::BottomBg, "rgba(23,21,19,.7)" }, { Token::Nav1, "rgba(232,130,63,.18)" }, { Token::Nav2, "rgba(217,126,120,.1)" },
        { Token::Navb, "rgba(232,130,63,.35)" }, { Token::Ov, "rgba(255,255,255,.02)" }, { Token::Hring, "rgba(0,0,0,.35)" },
        { Token::KnobFace, "#0f0d0c" }, { Token::KnobTrack, "#3a3530" }, { Token::KnobDot, "#fff" }, { Token::KnobHot, "#d9483e" },
        { Token::KnobHot2, "#e0645a" }, { Token::Band1, "#e8b84f" }, { Token::Band2, "#c76b9e" }, { Token::Band3, "#7fbfc2" },
        { Token::Handle, "#fff" }, { Token::LogoInk, "#0c0b0a" },
    };
    return v;
}

const std::vector<Css>& hammondTokens()
{
    static const std::vector<Css> v = {
        { Token::BgApp, "#1a120c" }, { Token::BgCard, "#2c241c" }, { Token::Border, "#3a2c20" }, { Token::Accent, "#c45c26" },
        { Token::Accent2, "#e07a3a" }, { Token::Pink, "#a88b52" }, { Token::Pink2, "#c9a86c" }, { Token::Text, "#f0e8da" },
        { Token::TextDim, "#a89880" }, { Token::TextDim2, "#7a6c58" }, { Token::Grid, "#2e241c" }, { Token::StageBg, "#080604" },
        { Token::Glow1, "rgba(196,92,38,.1)" }, { Token::Glow2, "rgba(201,168,108,.1)" }, { Token::PanelBg, "rgba(32,24,18,.65)" },
        { Token::Pb, "rgba(255,220,180,.06)" }, { Token::Pbt, "rgba(255,220,180,.1)" },
        { Token::HeaderBg, "linear-gradient(180deg,#2e2218,#221810)" }, { Token::BottomBg, "linear-gradient(180deg,#2a1e14,#1e1610)" },
        { Token::Nav1, "rgba(196,92,38,.22)" }, { Token::Nav2, "rgba(90,60,30,.18)" }, { Token::Navb, "rgba(196,92,38,.3)" },
        { Token::Ov, "rgba(255,220,180,.02)" },
        { Token::KnobPotFace1, "#4a3c30" }, { Token::KnobPotFace2, "#2a2218" }, { Token::KnobPotFace3, "#1a140e" },
        { Token::KnobPotRing, "#0e0b08" }, { Token::KnobPotPointer, "#e8dcc8" }, { Token::KnobPotCenter, "#1a140e" },
        { Token::CtlBd, "#1a140e" }, // data-ctl="bezel"
    };
    return v;
}

const std::vector<Css>& lightTokens()
{
    static const std::vector<Css> v = {
        { Token::BgApp, "#e4d8c4" }, { Token::BgCard, "#e9dfc9" }, { Token::Border, "#cfc0a2" }, { Token::Accent, "#c97b4a" },
        { Token::Accent2, "#d99566" }, { Token::Pink, "#b98883" }, { Token::Pink2, "#c9a09b" }, { Token::Text, "#362d20" },
        { Token::TextDim, "#7a6f5c" }, { Token::TextDim2, "#a89b83" }, { Token::Grid, "#d8cbb0" }, { Token::StageBg, "#d8cbb0" },
        { Token::Glow1, "rgba(201,123,74,.06)" }, { Token::Glow2, "rgba(185,136,131,.06)" }, { Token::PanelBg, "rgba(233,223,201,.6)" },
        { Token::Pb, "rgba(54,45,32,.08)" }, { Token::Pbt, "rgba(255,248,230,.3)" }, { Token::HeaderBg, "rgba(233,223,201,.5)" },
        { Token::BottomBg, "rgba(228,216,190,.7)" }, { Token::Nav1, "rgba(201,123,74,.16)" }, { Token::Nav2, "rgba(185,136,131,.1)" },
        { Token::Navb, "rgba(201,123,74,.32)" }, { Token::Ov, "rgba(54,45,32,.03)" }, { Token::Hring, "rgba(90,74,50,.2)" },
    };
    return v;
}

const std::vector<Css>& hcTokens()
{
    static const std::vector<Css> v = {
        { Token::BgApp, "#000" }, { Token::BgCard, "#000" }, { Token::Border, "#fff" }, { Token::Accent, "#ffd400" },
        { Token::Accent2, "#ffe066" }, { Token::Pink, "#33c7ff" }, { Token::Pink2, "#7fe0ff" }, { Token::Text, "#fff" },
        { Token::TextDim, "#e6e6e6" }, { Token::TextDim2, "#cfcfcf" }, { Token::Grid, "#fff" }, { Token::StageBg, "#000" },
        { Token::Glow1, "#0000" }, { Token::Glow2, "#0000" }, { Token::PanelBg, "#000" }, { Token::Pb, "#fff" }, { Token::Pbt, "#fff" },
        { Token::HeaderBg, "#000" }, { Token::BottomBg, "#000" }, { Token::Nav1, "rgba(255,212,0,.22)" },
        { Token::Nav2, "rgba(255,183,0,.14)" }, { Token::Navb, "#ffd400" }, { Token::Ov, "#000" }, { Token::Hring, "#000" },
        { Token::KnobFace, "#fff" }, { Token::KnobTrack, "#fff" }, { Token::KnobDot, "#000" },
        { Token::CtlBd, "#fff" }, // data-ctl="hc"
    };
    return v;
}

ThemeDefinition build (ThemeId id)
{
    ThemeDefinition d {};
    d.id = id;
    apply (d.palette, rootTokens());
    switch (id)
    {
        case ThemeId::Dark:
            d.knob = KnobStyle::Led; d.control = ControlStyle::Glass;
            d.metrics = { 14, 18, 20, -1, 5, 1 };
            break;
        case ThemeId::Hammond:
            apply (d.palette, hammondTokens());
            d.knob = KnobStyle::Pot; d.control = ControlStyle::Bezel;
            d.metrics = { 10, 14, 4, 6, 1, 1 };
            break;
        case ThemeId::Light:
            apply (d.palette, lightTokens());
            d.knob = KnobStyle::Led; d.control = ControlStyle::Glass;
            d.metrics = { 14, 18, 20, -1, 5, 1 };
            break;
        case ThemeId::HighContrast:
            apply (d.palette, hcTokens());
            d.knob = KnobStyle::Led; d.control = ControlStyle::HighContrast;
            d.metrics = { 14, 18, 20, -1, 5, 2 };
            break;
    }
    if (d.control == ControlStyle::Glass) d.palette.paints[static_cast<size_t> (Token::CtlBd)] = d.palette.paint (Token::Border); // --ctl-bd: var(--border)
    return d;
}

// --- Contraste : port de design/nova-contrast.js ------------------------------------------------

using Rgb = std::array<double, 3>;

double lin (double c)
{
    c /= 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
}
double lum (const Rgb& c) { return 0.2126 * lin (c[0]) + 0.7152 * lin (c[1]) + 0.0722 * lin (c[2]); }
double ratio (const Rgb& a, const Rgb& b)
{
    const double x = lum (a), y = lum (b);
    return (std::max (x, y) + 0.05) / (std::min (x, y) + 0.05);
}
Rgb over (const Rgba& fg, const Rgb& bg)
{
    return { fg.r * fg.a + bg[0] * (1 - fg.a), fg.g * fg.a + bg[1] * (1 - fg.a), fg.b * fg.a + bg[2] * (1 - fg.a) };
}
Rgb mix (const Rgb& a, const Rgb& b, double t) { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
double minR (const Rgb& c, const std::vector<Rgb>& surfs)
{
    double m = 1e9;
    for (const auto& s : surfs) m = std::min (m, ratio (c, s));
    return m;
}
std::uint8_t byteOf (double v) { return static_cast<std::uint8_t> (std::lround (std::clamp (v, 0.0, 255.0))); }
Rgba toRgba (const Rgb& c) { return { byteOf (c[0]), byteOf (c[1]), byteOf (c[2]), 1.0 }; }

// Plus petit déplacement (vers le blanc OU le noir) qui atteint le rapport visé sur toutes les surfaces ; sinon, le meilleur possible.
Rgb push (const Rgb& col, const std::vector<Rgb>& surfs, double target)
{
    const Rgb dirs[2] = { { 255, 255, 255 }, { 0, 0, 0 } };
    constexpr int N = 128;
    bool found = false;
    double bestT = 0.0;
    Rgb bestC = col;
    for (const auto& d : dirs)
        for (int i = 0; i <= N; ++i)
        {
            const Rgb c = mix (col, d, static_cast<double> (i) / N);
            if (minR (c, surfs) >= target - 1e-9)
            {
                const double t = static_cast<double> (i) / N;
                if (! found || t < bestT) { found = true; bestT = t; bestC = c; }
                break;
            }
        }
    if (found) return bestC;
    Rgb bc = col;
    double br = minR (col, surfs);
    for (const auto& d : dirs)
        for (int i = 0; i <= N; ++i)
        {
            const Rgb c = mix (col, d, static_cast<double> (i) / N);
            const double r = minR (c, surfs);
            if (r > br) { br = r; bc = c; }
        }
    return bc;
}

enum class Surf { All, Panel, Edge, Knob, LogoInk };
struct Role { Token token; double max; Surf surfaces; };

// Mêmes rôles et rapports cibles que ROLES dans nova-contrast.js, limités aux jetons que Neptune définit.
const Role kRoles[] = {
    { Token::Text, 12, Surf::All }, { Token::TextDim, 8, Surf::All }, { Token::TextDim2, 5, Surf::All },
    { Token::Accent, 5, Surf::All }, { Token::Accent2, 5, Surf::All }, { Token::Pink, 5, Surf::All }, { Token::Pink2, 5, Surf::All },
    { Token::KnobHot2, 4.5, Surf::All }, { Token::KnobHot, 3, Surf::Panel },
    { Token::Band1, 3.5, Surf::Panel }, { Token::Band2, 3.5, Surf::Panel }, { Token::Band3, 3.5, Surf::Panel },
    { Token::Handle, 3, Surf::Panel },
    { Token::Border, 3.5, Surf::Edge }, { Token::CtlBd, 3.5, Surf::Edge }, { Token::Pb, 3.5, Surf::Edge }, { Token::Pbt, 3.5, Surf::Edge },
    { Token::Grid, 2.2, Surf::Panel }, { Token::KnobTrack, 3, Surf::Knob },
    { Token::LogoInk, 4.5, Surf::LogoInk }, // posé sur le dégradé accent/pink du logo
};
} // namespace

std::optional<ThemeId> themeFromString (std::string_view id)
{
    if (id == "dark") return ThemeId::Dark;
    if (id == "hammond") return ThemeId::Hammond;
    if (id == "light") return ThemeId::Light;
    if (id == "hc") return ThemeId::HighContrast;
    return std::nullopt;
}

const char* toString (ThemeId id)
{
    switch (id)
    {
        case ThemeId::Dark: return "dark";
        case ThemeId::Hammond: return "hammond";
        case ThemeId::Light: return "light";
        case ThemeId::HighContrast: return "hc";
    }
    return "dark";
}

const char* tokenCssName (Token t) { return kTokenNames[static_cast<size_t> (t)]; }

const ThemeDefinition& definition (ThemeId id)
{
    static const std::array<ThemeDefinition, numThemes> all = { build (ThemeId::Dark), build (ThemeId::Hammond), build (ThemeId::Light),
                                                                 build (ThemeId::HighContrast) };
    return all[static_cast<size_t> (id)];
}

double contrastRatio (const Rgba& a, const Rgba& b) noexcept
{
    return ratio ({ double (a.r), double (a.g), double (a.b) }, { double (b.r), double (b.g), double (b.b) });
}

Palette withContrast (const ThemeDefinition& def, int level)
{
    Palette out = def.palette;
    level = std::clamp (level, 0, kLevels);
    if (level == 0 || def.id == ThemeId::HighContrast) return out;
    const double k = static_cast<double> (level) / kLevels;
    const Palette& base = def.palette;

    const Rgba& bgc = base.color (Token::BgApp);
    const Rgb bg { double (bgc.r), double (bgc.g), double (bgc.b) };
    Rgb panel = bg;

    const Rgba pc = base.color (Token::PanelBg);
    {
        const double a = pc.a + (1 - pc.a) * k;
        if (pc.a < 1)
        {
            // Arrondi à 3 décimales comme Number.toFixed(3) du JS (et non std::round : 0.9125 -> 0.912).
            char buf[32];
            std::snprintf (buf, sizeof buf, "%.3f", a);
            Rgba o = { pc.r, pc.g, pc.b, a >= 0.999 ? 1.0 : std::strtod (buf, nullptr) };
            out.paints[static_cast<size_t> (Token::PanelBg)] = { o, o, false };
        }
        panel = over ({ pc.r, pc.g, pc.b, a }, bg);
    }

    const Rgb card = over (base.color (Token::BgCard), bg);
    const Paint& hp = base.paint (Token::HeaderBg);
    const bool hasHeader = ! hp.gradient;
    const Rgb header = hasHeader ? over (hp.from, bg) : bg;
    const Rgb face = over (base.color (Token::KnobFace), panel);

    std::vector<Rgb> sAll { bg, panel, card };
    if (hasHeader) sAll.push_back (header);
    const std::vector<Rgb> sPanel { panel }, sEdge { panel, bg, card }, sKnob { face, panel };

    auto current = [&] (Token t) -> const Rgba& { return out.color (t); }; // valeur déjà ajustée par un rôle précédent

    for (const auto& role : kRoles)
    {
        const Paint& bp = base.paint (role.token);
        if (bp.gradient) continue;
        const Rgba col = bp.from;
        std::vector<Rgb> derived;
        const std::vector<Rgb>* surfs = nullptr;
        switch (role.surfaces)
        {
            case Surf::All: surfs = &sAll; break;
            case Surf::Panel: surfs = &sPanel; break;
            case Surf::Edge: surfs = &sEdge; break;
            case Surf::Knob: surfs = &sKnob; break;
            case Surf::LogoInk:
                derived = { over (current (Token::Accent), panel), over (current (Token::Pink), panel) };
                surfs = &derived;
                break;
        }
        const Rgb eff = over (col, panel);
        const double cur = minR (eff, *surfs);
        if (role.max <= cur + 1e-6) continue; // déjà assez contrasté
        const double target = role.surfaces == Surf::LogoInk ? role.max : cur + (role.max - cur) * k;
        const Rgba o = toRgba (push (eff, *surfs, target));
        out.paints[static_cast<size_t> (role.token)] = { o, o, false };
    }
    return out;
}
} // namespace neptune::core::theme
