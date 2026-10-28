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
    CHECK (parseImageName ("off.svg").role == "off");
    CHECK (parseImageName ("ON.PNG").role == "on");
    CHECK (parseImageName ("closed.jpg").role == "off");
    CHECK (parseImageName ("open.jpeg").role == "on");
    CHECK (parseImageName ("rail.svg").role == "track");
    CHECK (parseImageName ("thumb.png").role == "handle");
    CHECK (parseImageName ("hover.svg").role == "off_hover");
    CHECK (parseImageName ("on_hover.svg").role == "on_hover");
    CHECK (parseImageName ("inactive.svg").role == "disabled");
    CHECK (parseImageName ("dial.svg").role == "body");
    CHECK (parseImageName ("needle.svg").role == "pointer");
    CHECK (parseImageName ("key_white.svg").role == "key_white");
    CHECK (parseImageName ("black_key_on.png").role == "key_black_on");
    CHECK (parseImageName ("panel.svg").role == "frame");
    CHECK (parseImageName ("off.gif").role.empty());
    CHECK (parseImageName ("readme.txt").role.empty());
    CHECK (parseImageName ("off").role.empty());

    auto strip = parseImageName ("strip_32.svg");
    CHECK (strip.role == "strip");
    CHECK (strip.frames == 32);
    CHECK (! strip.horizontal);
    strip = parseImageName ("Strip_64_h.png");
    CHECK (strip.frames == 64);
    CHECK (strip.horizontal);
    CHECK (parseImageName ("strip.png").role == "strip");
    CHECK (parseImageName ("strip.png").frames == 0);
    CHECK (parseImageName ("strip_1.png").role.empty());       // moins de 2 images
    CHECK (parseImageName ("strip_9999.png").role.empty());
    CHECK (parseImageName ("strip_x.png").role.empty());
    CHECK (parseImageName ("filmstrip_16.svg").frames == 16);
}

TEST_CASE ("style pack: every image role of a style has a recognised file name")
{
    for (const auto& role : neptune::core::facade::imageRoles())
        CHECK (parseImageName (std::string (role.name) + ".svg").role == (std::string (role.name)));
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
    CHECK (pack.styles[0].style.name == "Organ-Classic");
    CHECK (pack.styles[0].style.off == "off.svg");
    CHECK (pack.styles[0].style.on == "on.svg");
    CHECK (pack.styles[0].style.strip.empty());
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
    CHECK (pack.styles[0].style.name == "drawbar");
    CHECK (pack.styles[1].style.name == "Big knob");
    CHECK (pack.styles[1].style.frames == 48);
    CHECK (pack.styles[1].style.stripHorizontal);
    CHECK (pack.styles[2].style.track == "sliders/track.svg");
    CHECK (pack.warnings.empty());
}

TEST_CASE ("style pack: warnings for a strip without frames, duplicates, unsafe and empty packs")
{
    auto pack = run ("p", { { "knob/strip.svg", "" } });
    CHECK (pack.styles[0].style.strip.empty());
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "off.svg", "" }, { "off.png", "" } });
    CHECK (pack.styles[0].style.off == "off.svg");     // le SVG est préféré
    CHECK (! pack.warnings.empty());

    pack = run ("p", { { "../off.svg", "" }, { "on.svg", "" } });
    CHECK (pack.styles[0].style.off.empty());
    CHECK (pack.styles[0].style.on == "on.svg");
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

TEST_CASE ("style pack: style.json carries text, slicing, pointer angles and key colour")
{
    const auto pack = run ("p", {
        { "dial/body.svg", "" }, { "dial/pointer.svg", "" },
        { "dial/style.json", R"({"pointer_angles":[-150,150],"pivot":[50,60],"text":{"colour":"#ffffff","size":55,"position":"bottom","bold":false},"key_lit_colour":"#f0b429"})" },
        { "panel/frame.svg", "" }, { "panel/style.json", R"({"slice":[10,12,14,16],"slice_scale":150})" },
        { "keys/key_white.svg", "" }, { "keys/key_white_on.svg", "" }, { "keys/key_black.svg", "" }, { "keys/key_black_on.svg", "" } });
    REQUIRE (pack.styles.size() == 3);
    const auto& dial = pack.styles[0].style;   // dossiers triés : dial, keys, panel
    CHECK (dial.body == "dial/body.svg");
    CHECK (dial.pointer == "dial/pointer.svg");
    CHECK (dial.angleMin == -150);
    CHECK (dial.angleMax == 150);
    CHECK (dial.pivotY == 60);
    CHECK (dial.textColour == "#ffffff");
    CHECK (dial.textSize == 55);
    CHECK (dial.textPosition == 2);
    CHECK (! dial.textBold);
    CHECK (dial.keyLit == "#f0b429");
    const auto& keys = pack.styles[1].style;
    CHECK (keys.keyWhiteOn == "keys/key_white_on.svg");
    CHECK (keys.keyBlack == "keys/key_black.svg");
    const auto& panel = pack.styles[2].style;
    CHECK (panel.frame == "panel/frame.svg");
    CHECK (panel.sliceLeft == 10);
    CHECK (panel.sliceBottom == 16);
    CHECK (panel.sliceScale == 150);
    CHECK (pack.warnings.empty());
}

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE ("style pack: the two organ packs shipped in assets/ are valid and complete")
{
    namespace fs = std::filesystem;
    const fs::path assets = fs::path (__FILE__).parent_path().parent_path() / "assets" / "style-packs";
    for (const char* id : { "organ-wood-brass", "organ-flat-modern" })
    {
        const fs::path root = assets / id;
        if (! fs::exists (root)) { SUCCEED ("assets not available"); continue; }
        std::map<std::string, std::string> files;
        for (const auto& e : fs::recursive_directory_iterator (root))
        {
            if (! e.is_regular_file()) continue;
            std::ifstream in (e.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            files[fs::relative (e.path(), root).generic_string()] = text.str();
        }
        const auto pack = run (id, files);
        INFO (id);
        CHECK (pack.warnings.empty());
        CHECK (pack.styles.size() == 10);
        for (const auto& s : pack.styles)
        {
            INFO (s.folder);
            using neptune::core::facade::ImageStyle;
            // off et on vont par deux ; curseurs : rail et poignée ; clavier : quatre touches ; potard : corps et aiguille
            CHECK (s.style.off.empty() == s.style.on.empty());
            CHECK (s.style.track.empty() == s.style.handle.empty());
            CHECK (s.style.body.empty() == s.style.pointer.empty());
            CHECK ((! s.style.off.empty() || ! s.style.track.empty() || ! s.style.body.empty() || ! s.style.keyWhite.empty() || ! s.style.frame.empty()));
            if (s.folder == "keys") CHECK ((! s.style.keyWhite.empty() && ! s.style.keyWhiteOn.empty() && ! s.style.keyBlack.empty() && ! s.style.keyBlackOn.empty()));
            if (s.folder == "frame") CHECK (! s.style.frame.empty());
        }
        // aucun SVG ne contient ce que le dessin de JUCE ne sait pas faire
        for (const auto& [name, text] : files)
        {
            if (name.size() < 4 || name.substr (name.size() - 4) != ".svg") continue;
            INFO (name);
            for (const char* banned : { "<text", "<filter", "<style", "<image", "<mask", "<pattern", "<script" }) CHECK (text.find (banned) == std::string::npos);
        }
    }
}
