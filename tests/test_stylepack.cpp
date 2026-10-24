#include <catch2/catch_test_macros.hpp>

#include <map>

#include "neptune/core/StylePack.h"

using namespace neptune::core::stylepack;

namespace
{
PackInfo run (const std::string& stem, const std::map<std::string, std::string>& files)
{
    std::vector<std::string> names;
    for (const auto& [name, content] : files) names.push_back (name);
    return analyse (stem, names, [&files] (const std::string& e) { const auto it = files.find (e); return it == files.end() ? std::string() : it->second; });
}
}

TEST_CASE ("style pack: image file names are recognised")
{
    CHECK (parseImageName ("off.svg").role == Role::Off);
    CHECK (parseImageName ("ON.PNG").role == Role::On);
    CHECK (parseImageName ("closed.jpg").role == Role::Off);
    CHECK (parseImageName ("open.jpeg").role == Role::On);
    CHECK (parseImageName ("rail.svg").role == Role::Track);
    CHECK (parseImageName ("thumb.png").role == Role::Handle);
    CHECK (parseImageName ("off.gif").role == Role::None);
    CHECK (parseImageName ("readme.txt").role == Role::None);
    CHECK (parseImageName ("off").role == Role::None);

    auto strip = parseImageName ("strip_32.svg");
    CHECK (strip.role == Role::Strip);
    CHECK (strip.frames == 32);
    CHECK (! strip.horizontal);
    strip = parseImageName ("Strip_64_h.png");
    CHECK (strip.frames == 64);
    CHECK (strip.horizontal);
    CHECK (parseImageName ("strip.png").role == Role::Strip);
    CHECK (parseImageName ("strip.png").frames == 0);
    CHECK (parseImageName ("strip_1.png").role == Role::None);       // moins de 2 images
    CHECK (parseImageName ("strip_9999.png").role == Role::None);
    CHECK (parseImageName ("strip_x.png").role == Role::None);
    CHECK (parseImageName ("filmstrip_16.svg").frames == 16);
}

TEST_CASE ("style pack: unsafe entry names are refused")
{
    CHECK (isSafeEntryName ("a/b/off.svg"));
    CHECK (! isSafeEntryName ("../off.svg"));
    CHECK (! isSafeEntryName ("a/../../off.svg"));
    CHECK (! isSafeEntryName ("/etc/passwd"));
    CHECK (! isSafeEntryName ("C:/x.svg"));
    CHECK (! isSafeEntryName ("a\\b.svg"));
    CHECK (! isSafeEntryName (""));
}

TEST_CASE ("style pack: a flat archive is one style named after the pack")
{
    const auto pack = run ("Organ-Classic", { { "off.svg", "" }, { "on.svg", "" }, { "notes.txt", "" } });
    CHECK (pack.id == "organ-classic");
    REQUIRE (pack.styles.size() == 1);
    CHECK (pack.styles[0].name == "Organ-Classic");
    CHECK (pack.styles[0].off == "off.svg");
    CHECK (pack.styles[0].on == "on.svg");
    CHECK (pack.styles[0].strip.empty());
    CHECK (pack.warnings.empty());
}

TEST_CASE ("style pack: folders are styles, manifests give names and frame counts")
{
    const auto pack = run ("mine", {
        { "pack.json", R"({"name":"My pack","author":"Me","license":"CC-BY","version":2,"description":"d","format":1})" },
        { "drawbar/off.svg", "" }, { "drawbar/on.svg", "" },
        { "knobs/strip.png", "" }, { "knobs/style.json", R"({"name":"Big knob","frames":48,"strip_direction":"horizontal"})" },
        { "sliders/track.svg", "" }, { "sliders/handle.svg", "" },
        { "__MACOSX/drawbar/off.svg", "" }, { "drawbar/.DS_Store", "" } });
    CHECK (pack.name == "My pack");
    CHECK (pack.author == "Me");
    CHECK (pack.licence == "CC-BY");
    CHECK (pack.version == "2");
    REQUIRE (pack.styles.size() == 3);
    CHECK (pack.styles[0].folder == "drawbar");   // ordre alphabétique des dossiers
    CHECK (pack.styles[0].name == "drawbar");
    CHECK (pack.styles[1].name == "Big knob");
    CHECK (pack.styles[1].frames == 48);
    CHECK (pack.styles[1].stripHorizontal);
    CHECK (pack.styles[2].track == "sliders/track.svg");
    CHECK (pack.warnings.empty());
}

TEST_CASE ("style pack: warnings for a strip without frames, duplicates, unsafe and empty packs")
{
    auto pack = run ("p", { { "knob/strip.svg", "" } });
    CHECK (pack.styles[0].strip.empty());
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "off.svg", "" }, { "off.png", "" } });
    CHECK (pack.styles[0].off == "off.svg");     // le SVG est préféré
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "../off.svg", "" }, { "on.svg", "" } });
    CHECK (pack.styles[0].off.empty());
    CHECK (pack.styles[0].on == "on.svg");
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "readme.txt", "" } });
    CHECK (pack.styles.empty());
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "pack.json", "not json" }, { "off.svg", "" } });   // JSON illisible : ignoré sans planter
    CHECK (pack.styles.size() == 1);
}

TEST_CASE ("style pack: references round trip")
{
    const auto ref = makeReference ("organ", "drawbar/off.svg");
    CHECK (ref == "pack:organ!drawbar/off.svg");
    std::string id, entry;
    REQUIRE (parseReference (ref, id, entry));
    CHECK (id == "organ");
    CHECK (entry == "drawbar/off.svg");
    CHECK (! parseReference ("C:/file.svg", id, entry));
    CHECK (! parseReference ("pack:organ!../x.svg", id, entry));
    CHECK (! parseReference ("pack:!x.svg", id, entry));
}
