#include <catch2/catch_test_macros.hpp>
#include "neptune/core/I18n.h"
#include <set>
#include <string>

using namespace neptune::core::i18n;

TEST_CASE ("i18n default language is English")
{
    Translator t;
    CHECK (t.language() == "en");
    CHECK (t.tr ("recovery.restore") == "Restore");
}

TEST_CASE ("i18n French and fallbacks")
{
    Translator fr ("fr");
    CHECK (fr.language() == "fr");
    CHECK (fr.tr ("recovery.restore") == "Restaurer");
    // Langue inconnue -> anglais ; clé inconnue -> la clé elle-même.
    CHECK (Translator ("klingon").language() == "en");
    CHECK (Translator ("klingon").tr ("recovery.restore") == "Restore");
    CHECK (fr.tr ("no.such.key") == "no.such.key");
}

TEST_CASE ("i18n substitution")
{
    CHECK (substitute ("a {0} b {1}", { "X", "Y" }) == "a X b Y");
    CHECK (substitute ("{1} then {0}", { "A", "B" }) == "B then A");
    CHECK (substitute ("{2} stays, {x} stays, {", { "A" }) == "{2} stays, {x} stays, {");
    CHECK (Translator ("fr").format ("latency.set", { "64" }) == "Latence 64");
}

TEST_CASE ("i18n tables are consistent")
{
    const auto langs = languages();
    std::set<std::string_view> keys;
    for (const auto& e : allEntries())
    {
        INFO ("key: " << e.key);
        CHECK (keys.insert (e.key).second);                 // pas de doublon
        REQUIRE (e.text.size() == langs.size());
        REQUIRE_FALSE (e.text[0].empty());                  // l'anglais est obligatoire
        // Mêmes trous dans toutes les traductions fournies.
        auto holes = [] (std::string_view s) {
            std::set<std::string> h;
            for (size_t i = 0; i < s.size(); ++i)
                if (s[i] == '{')
                    if (auto c = s.find ('}', i); c != std::string_view::npos) h.insert (std::string (s.substr (i, c - i + 1)));
            return h;
        };
        for (size_t l = 1; l < e.text.size(); ++l)
            if (! e.text[l].empty()) CHECK (holes (e.text[l]) == holes (e.text[0]));
    }
    for (const auto& l : langs) CHECK (isSupported (l.code));
    CHECK_FALSE (isSupported ("xx"));
}
