#include <catch2/catch_test_macros.hpp>
#include "neptune/core/Theme.h"
#include <string>

using namespace neptune::core::theme;

namespace
{
const ThemeId kAll[] = { ThemeId::Dark, ThemeId::Hammond, ThemeId::Light, ThemeId::HighContrast };
}

TEST_CASE ("theme ids round trip and match settings identifiers")
{
    for (ThemeId id : kAll)
    {
        auto back = themeFromString (toString (id));
        REQUIRE (back.has_value());
        CHECK (*back == id);
    }
    CHECK_FALSE (themeFromString ("neon").has_value());
}

TEST_CASE ("theme values match the mockup")
{
    const auto& dark = definition (ThemeId::Dark);
    CHECK (dark.palette.color (Token::Accent) == Rgba { 0xe8, 0x82, 0x3f, 1.0 });
    CHECK (dark.palette.color (Token::KnobDot) == Rgba { 255, 255, 255, 1.0 });
    CHECK (dark.palette.color (Token::PanelBg).a == 0.62);
    CHECK (dark.palette.color (Token::CtlBd) == dark.palette.color (Token::Border)); // glass : var(--border)

    const auto& h = definition (ThemeId::Hammond);
    CHECK (h.knob == KnobStyle::Pot);
    CHECK (h.control == ControlStyle::Bezel);
    CHECK (h.palette.paint (Token::HeaderBg).gradient);
    CHECK (h.palette.paint (Token::HeaderBg).from == Rgba { 0x2e, 0x22, 0x18, 1.0 });
    CHECK (h.palette.paint (Token::HeaderBg).to == Rgba { 0x22, 0x18, 0x10, 1.0 });
    CHECK (h.palette.color (Token::CtlBd) == Rgba { 0x1a, 0x14, 0x0e, 1.0 });
    CHECK (h.palette.color (Token::KnobFace) == dark.palette.color (Token::KnobFace)); // hérité de :root

    const auto& hc = definition (ThemeId::HighContrast);
    CHECK (hc.metrics.borderWidth == 2);
    CHECK (hc.palette.color (Token::Glow1).a == 0.0); // #0000
    CHECK (definition (ThemeId::Light).knob == KnobStyle::Led);
}

TEST_CASE ("theme token names are unique CSS names")
{
    std::string prev;
    for (int i = 0; i < numTokens; ++i)
    {
        const std::string n = tokenCssName (static_cast<Token> (i));
        REQUIRE (n.rfind ("--", 0) == 0);
        for (int j = 0; j < i; ++j) CHECK (n != tokenCssName (static_cast<Token> (j)));
    }
}

TEST_CASE ("contrast level 0 changes nothing, high contrast theme is never modified")
{
    for (ThemeId id : kAll)
    {
        const auto& d = definition (id);
        const Palette p0 = withContrast (d, 0);
        for (int i = 0; i < numTokens; ++i)
            CHECK (p0.paints[static_cast<size_t> (i)].from == d.palette.paints[static_cast<size_t> (i)].from);
    }
    const auto& hc = definition (ThemeId::HighContrast);
    const Palette p = withContrast (hc, 4);
    for (int i = 0; i < numTokens; ++i)
        CHECK (p.paints[static_cast<size_t> (i)].from == hc.palette.paints[static_cast<size_t> (i)].from);
}

TEST_CASE ("contrast levels are monotonic and reach the target ratio at level 4")
{
    for (ThemeId id : { ThemeId::Dark, ThemeId::Hammond, ThemeId::Light })
    {
        const auto& d = definition (id);
        INFO ("theme " << toString (id));
        double prevText = 0.0, prevDim = 0.0;
        for (int level = 0; level <= 4; ++level)
        {
            const Palette p = withContrast (d, level);
            const Rgba bg = p.color (Token::BgApp); // inchangé
            const double rt = contrastRatio (p.color (Token::Text), bg);
            const double rd = contrastRatio (p.color (Token::TextDim), bg);
            CHECK (rt >= prevText - 1e-9);
            CHECK (rd >= prevDim - 1e-9);
            prevText = rt;
            prevDim = rd;
            CHECK (p.color (Token::BgApp) == d.palette.color (Token::BgApp));
        }
        // Niveau 4 : texte >= 12:1 et texte atténué >= 8:1 sur le fond (cibles de nova-contrast.js).
        const Palette p4 = withContrast (d, 4);
        const Rgba bg = p4.color (Token::BgApp);
        CHECK (contrastRatio (p4.color (Token::Text), bg) >= 12.0 - 0.05);
        CHECK (contrastRatio (p4.color (Token::TextDim), bg) >= 8.0 - 0.05);
        // Le panneau devient opaque au niveau max.
        CHECK (p4.color (Token::PanelBg).a == 1.0);
    }
}

TEST_CASE ("contrast level is clamped")
{
    const auto& d = definition (ThemeId::Dark);
    CHECK (withContrast (d, 99).color (Token::Text) == withContrast (d, 4).color (Token::Text));
    CHECK (withContrast (d, -3).color (Token::Text) == d.palette.color (Token::Text));
}

TEST_CASE ("contrast matches nova-contrast.js on reference values")
{
    // Valeurs relevées en exécutant design/nova-contrast.js (NovaContrast.apply) sur la maquette : 163/163 identiques.
    const Palette d1 = withContrast (definition (ThemeId::Dark), 1);
    CHECK (d1.color (Token::Border) == Rgba { 0x46, 0x43, 0x3f, 1.0 });
    CHECK (d1.color (Token::TextDim) == Rgba { 0xa2, 0x99, 0x91, 1.0 });
    CHECK (d1.color (Token::PanelBg).a == 0.715);
    CHECK (withContrast (definition (ThemeId::Hammond), 3).color (Token::PanelBg).a == 0.912); // 0.9125 arrondi comme toFixed(3)
}
