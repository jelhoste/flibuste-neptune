#include "neptune/core/StylePack.h"

#include <algorithm>
#include <cctype>
#include <map>

#include <nlohmann/json.hpp>

namespace neptune::core::stylepack
{
using nlohmann::json;

namespace
{
std::string lower (std::string_view s)
{
    std::string out (s);
    std::transform (out.begin(), out.end(), out.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
    return out;
}

std::string baseName (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

std::string dirName (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return slash == std::string::npos ? std::string() : path.substr (0, slash);
}

int rank (const std::string& entry)   // préférence quand deux formats existent pour le même rôle : svg, puis png, puis jpg
{
    const auto ext = lower (entry.substr (entry.find_last_of ('.') + 1));
    return ext == "svg" ? 0 : ext == "png" ? 1 : 2;
}

std::string text (const json& o, const char* key)
{
    const auto it = o.find (key);
    if (it == o.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number()) return it->dump();
    return {};
}

std::string clipped (std::string s, std::size_t n = 200)
{
    if (s.size() > n) s.resize (n);
    return s;
}

json parseJson (const std::string& s)
{
    if (s.empty()) return json::object();
    const auto o = json::parse (s, nullptr, false);
    return o.is_object() ? o : json::object();
}
} // namespace

ImageName parseImageName (std::string_view fileName)
{
    ImageName out;
    const std::string name = lower (fileName);
    const auto dot = name.find_last_of ('.');
    if (dot == std::string::npos) return out;
    const std::string ext = name.substr (dot + 1);
    if (ext != "svg" && ext != "png" && ext != "jpg" && ext != "jpeg") return out;
    const std::string stem = name.substr (0, dot);

    static const std::map<std::string, std::string> words = {
        { "off", "off" }, { "closed", "off" }, { "released", "off" },
        { "on", "on" }, { "open", "on" }, { "pressed", "on" },
        { "off_hover", "off_hover" }, { "hover", "off_hover" }, { "closed_hover", "off_hover" }, { "released_hover", "off_hover" },
        { "on_hover", "on_hover" }, { "open_hover", "on_hover" }, { "pressed_hover", "on_hover" },
        { "disabled", "disabled" }, { "inactive", "disabled" },
        { "track", "track" }, { "rail", "track" }, { "groove", "track" },
        { "handle", "handle" }, { "thumb", "handle" },
        { "body", "body" }, { "dial", "body" },
        { "pointer", "pointer" }, { "needle", "pointer" },
        { "key_white", "key_white" }, { "white_key", "key_white" }, { "key_white_on", "key_white_on" }, { "white_key_on", "key_white_on" },
        { "key_black", "key_black" }, { "black_key", "key_black" }, { "key_black_on", "key_black_on" }, { "black_key_on", "key_black_on" },
        { "frame", "frame" }, { "panel", "frame" } };
    if (const auto it = words.find (stem); it != words.end()) { out.role = it->second; return out; }

    // strip, strip_32, strip_32_h (aussi « filmstrip »)
    std::string rest;
    if (stem.rfind ("filmstrip", 0) == 0) rest = stem.substr (9);
    else if (stem.rfind ("strip", 0) == 0) rest = stem.substr (5);
    else return out;
    out.role = "strip";
    if (rest.empty()) return out;
    if (rest.size() < 2 || rest[0] != '_') return ImageName {};
    rest = rest.substr (1);
    if (rest.size() > 2 && rest.compare (rest.size() - 2, 2, "_h") == 0) { out.horizontal = true; rest.resize (rest.size() - 2); }
    if (rest.empty() || ! std::all_of (rest.begin(), rest.end(), [] (unsigned char c) { return std::isdigit (c) != 0; }) || rest.size() > 4) return ImageName {};
    out.frames = std::stoi (rest);
    if (out.frames < 2 || out.frames > 512) return ImageName {};
    return out;
}

bool isSafeEntryName (std::string_view name)
{
    if (name.empty() || name.size() > 512 || name.front() == '/') return false;
    if (name.find ('\\') != std::string_view::npos || name.find (':') != std::string_view::npos) return false;
    std::size_t pos = 0;
    while (pos <= name.size())
    {
        const auto next = name.find ('/', pos);
        const auto part = name.substr (pos, next == std::string_view::npos ? std::string_view::npos : next - pos);
        if (part == "..") return false;
        if (next == std::string_view::npos) break;
        pos = next + 1;
    }
    return true;
}

PackInfo analyse (std::string_view fileStem, const std::vector<std::string>& entriesIn, const std::function<std::string (const std::string&)>& readText)
{
    PackInfo pack;
    pack.id = lower (fileStem);
    pack.name = std::string (fileStem);

    std::vector<std::string> entries;
    for (const auto& e : entriesIn)
    {
        if (e.empty() || e.back() == '/') continue;                                   // dossier
        if (e.rfind ("__MACOSX/", 0) == 0 || baseName (e).front() == '.') continue;   // débris des archiveurs
        if (! isSafeEntryName (e)) { pack.warnings.push_back ("Entry ignored (unsafe path): " + clipped (e)); continue; }
        entries.push_back (e);
    }

    if (std::find (entries.begin(), entries.end(), "pack.json") != entries.end())
    {
        const auto o = parseJson (readText ("pack.json"));
        if (const auto n = text (o, "name"); ! n.empty()) pack.name = clipped (n, 64);
        pack.author = clipped (text (o, "author"));
        pack.licence = clipped (text (o, "licence").empty() ? text (o, "license") : text (o, "licence"));
        pack.version = clipped (text (o, "version"), 32);
        pack.description = clipped (text (o, "description"), 400);
        if (const auto f = o.find ("format"); f != o.end() && f->is_number_integer() && f->get<int>() > formatVersion)
            pack.warnings.push_back ("This pack uses a newer format version than this Neptune understands; some of it may be ignored.");
    }

    // dossier -> images reconnues
    struct Found { std::string entry; ImageName image; };
    std::map<std::string, std::vector<Found>> byFolder;
    for (const auto& e : entries)
    {
        const auto image = parseImageName (baseName (e));
        if (! image.role.empty()) byFolder[dirName (e)].push_back ({ e, image });
    }

    for (auto& [folder, found] : byFolder)
    {
        if (pack.styles.size() >= maxStylesPerPack) { pack.warnings.push_back ("More than 64 styles in the pack: the others are ignored."); break; }
        PackStyle ps;
        ps.folder = folder;
        auto& st = ps.style;
        st.name = folder.empty() ? pack.name : baseName (folder);
        const auto sidecar = folder.empty() ? std::string ("style.json") : folder + "/style.json";
        const bool hasJson = std::find (entries.begin(), entries.end(), sidecar) != entries.end();
        const auto meta = hasJson ? parseJson (readText (sidecar)) : json::object();
        if (const auto n = text (meta, "name"); ! n.empty()) st.name = clipped (n, 64);

        ImageName strip;
        for (const auto& f : found)
            for (const auto& role : facade::imageRoles())
            {
                if (f.image.role != role.name) continue;
                auto& slot = st.*(role.field);
                if (! slot.empty())
                {
                    if (rank (f.entry) < rank (slot)) { slot = f.entry; if (f.image.role == "strip") strip = f.image; }
                    pack.warnings.push_back ("Style \"" + st.name + "\": several files for the same image, one kept (SVG is preferred).");
                    continue;
                }
                slot = f.entry;
                if (f.image.role == "strip") strip = f.image;
            }

        auto intPair = [&meta] (const char* key, int& a, int& b)
        {
            const auto it = meta.find (key);
            if (it != meta.end() && it->is_array() && it->size() == 2 && (*it)[0].is_number() && (*it)[1].is_number())
            { a = static_cast<int> ((*it)[0].get<double>()); b = static_cast<int> ((*it)[1].get<double>()); }
        };
        auto intOf = [] (const json& o, const char* key, int& v) { const auto it = o.find (key); if (it != o.end() && it->is_number()) v = static_cast<int> (it->get<double>()); };

        if (! st.strip.empty())
        {
            st.frames = strip.frames;
            st.stripHorizontal = strip.horizontal;
            if (const auto f = meta.find ("frames"); f != meta.end() && f->is_number_integer()) st.frames = std::clamp (f->get<int>(), 0, 512);
            if (const auto d = text (meta, "strip_direction"); ! d.empty()) st.stripHorizontal = lower (d) == "horizontal";
            if (st.frames < 2)
            {
                pack.warnings.push_back ("Style \"" + st.name + "\": a knob strip needs its number of frames (name it strip_32.svg, or set \"frames\" in style.json); the strip is ignored.");
                st.strip.clear();
                st.frames = 0;
                st.stripHorizontal = false;
            }
        }
        intPair ("pointer_angles", st.angleMin, st.angleMax);
        intPair ("pivot", st.pivotX, st.pivotY);
        if (const auto it = meta.find ("slice"); it != meta.end() && it->is_array() && it->size() == 4)
        {
            int* v[4] = { &st.sliceLeft, &st.sliceTop, &st.sliceRight, &st.sliceBottom };
            for (std::size_t i = 0; i < 4; ++i) if ((*it)[i].is_number()) *v[i] = static_cast<int> ((*it)[i].get<double>());
        }
        intOf (meta, "slice_scale", st.sliceScale);
        st.keyLit = clipped (text (meta, "key_lit_colour"), 16);
        if (const auto t = meta.find ("text"); t != meta.end() && t->is_object())
        {
            st.textColour = clipped (text (*t, "colour").empty() ? text (*t, "color") : text (*t, "colour"), 16);
            intOf (*t, "size", st.textSize);
            const auto position = lower (text (*t, "position"));
            if (position == "top") st.textPosition = 1;
            else if (position == "bottom") st.textPosition = 2;
            else if (position == "left") st.textPosition = 3;
            else if (position == "right") st.textPosition = 4;
            if (const auto b = t->find ("bold"); b != t->end() && b->is_boolean()) st.textBold = b->get<bool>();
        }
        st = facade::sanitised ([&st] { facade::Facade f; f.styles.push_back (st); return f; }()).styles.front();   // mêmes bornes que dans la Façade
        pack.styles.push_back (std::move (ps));
    }
    if (pack.styles.empty()) pack.warnings.push_back ("No style found: put off.svg / on.svg, track.svg / handle.svg or strip_32.svg in the pack (see the specification).");
    return pack;
}

std::string makeReference (std::string_view packId, std::string_view entry)
{
    return "pack:" + std::string (packId) + "!" + std::string (entry);
}

bool parseReference (std::string_view reference, std::string& packId, std::string& entry)
{
    if (reference.rfind ("pack:", 0) != 0) return false;
    const auto bang = reference.find ('!');
    if (bang == std::string_view::npos || bang <= 5 || bang + 1 >= reference.size()) return false;
    packId = std::string (reference.substr (5, bang - 5));
    entry = std::string (reference.substr (bang + 1));
    return isSafeEntryName (entry);
}
} // namespace neptune::core::stylepack
