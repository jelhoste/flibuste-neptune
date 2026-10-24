#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>
#include "StyleLibrary.h"
#include "neptune/core/Facade.h"
#include "neptune/core/FacadeTransfer.h"

// Façade portable (fichier `.neptunefacade`, une archive zip) : la mise en page, le fond, les images locales et les packs de styles utilisés,
// pour qu'elle se charge telle quelle sur une autre piste, un autre projet ou une autre machine. La disposition interne est faite pour
// être reprise telle quelle dans un futur export d'instrument complet :
//   facade.json                {"format":1,"track":<piste d'origine>,"facade":{...}}
//   assets/<n>_<nom>           images locales (fond, styles), désignées dans facade.json par « asset:assets/... »
//   packs/<identifiant>.neptunestyle   les packs de styles référencés
namespace facade_bundle
{
namespace fc = neptune::core::facade;
namespace sp = neptune::core::stylepack;

struct SaveReport
{
    bool ok = false;
    int images = 0, packs = 0, missing = 0;
};

inline SaveReport save (fc::Facade f, int sourceTrack, const juce::File& destination)
{
    SaveReport report;
    juce::ZipFile::Builder builder;
    std::set<std::string> packs;
    std::map<std::string, std::string> done;   // ancien chemin -> nouveau
    fc::forEachImagePath (f, [&] (std::string& path)
    {
        const std::string old = path;
        if (const auto it = done.find (old); it != done.end()) { path = it->second; return; }
        std::string packId, entry;
        if (sp::parseReference (old, packId, entry))
        {
            packs.insert (packId);   // la référence reste ; le pack est copié plus bas
            done[old] = old;
            return;
        }
        juce::MemoryBlock bytes;
        if (! stylelib::readBytes (old, bytes) || bytes.getSize() == 0) { ++report.missing; done[old] = old; return; }
        const auto fileName = juce::File (juce::String::fromUTF8 (old.c_str())).getFileName();
        const auto name = "assets/" + juce::String (++report.images) + "_" + fileName.replaceCharacters (" \\/:", "____");
        builder.addEntry (new juce::MemoryInputStream (bytes, true), 9, name, juce::Time::getCurrentTime());
        path = ("asset:" + name).toStdString();
        done[old] = path;
    });
    for (const auto& id : packs)
    {
        juce::MemoryBlock bytes;
        if (! stylelib::packArchiveBytes (id, bytes)) { ++report.missing; continue; }
        builder.addEntry (new juce::MemoryInputStream (bytes, true), 9, "packs/" + juce::String (id) + ".neptunestyle", juce::Time::getCurrentTime());
        ++report.packs;
    }
    const juce::String json = "{\"format\":1,\"track\":" + juce::String (sourceTrack) + ",\"facade\":" + juce::String::fromUTF8 (fc::toJson (f).c_str()) + "}";
    builder.addEntry (new juce::MemoryInputStream (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9, "facade.json", juce::Time::getCurrentTime());

    destination.deleteFile();
    juce::FileOutputStream out (destination);
    report.ok = out.openedOk() && builder.writeToStream (out, nullptr);
    return report;
}

struct LoadReport
{
    bool ok = false;
    juce::String error;
    int images = 0, packsInstalled = 0;
};

// Lit un fichier Façade pour la piste `targetTrack` : les widgets sont rebranchés sur cette piste, les images embarquées sont extraites sous
// `assetsRoot` (un sous-dossier par contenu), les packs absents de la bibliothèque sont installés (un pack déjà installé n'est jamais écrasé).
inline LoadReport load (const juce::File& file, int targetTrack, const juce::File& assetsRoot, fc::Facade& out)
{
    LoadReport report;
    juce::ZipFile zip (file);
    const int jsonIndex = zip.getIndexOfFileName ("facade.json");
    if (jsonIndex < 0) { report.error = "Not a Facade file (facade.json is missing)."; return report; }
    const std::string text = stylelib::zipText (zip, "facade.json");
    // facade.json peut être plus gros que la limite des petits textes : relecture complète
    std::string full = text;
    if (full.empty())
    {
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (jsonIndex));
        if (stream != nullptr) full = stream->readEntireStreamAsString().toStdString();
    }
    const auto doc = nlohmann::json::parse (full, nullptr, false);
    if (! doc.is_object() || ! doc.contains ("facade") || ! doc["facade"].is_object()) { report.error = "The Facade file is unreadable."; return report; }
    const int fromTrack = doc.contains ("track") && doc["track"].is_number_integer() ? doc["track"].get<int>() : targetTrack;

    out = fc::fromJson (doc["facade"].dump());
    fc::remapTrack (out, fromTrack, targetTrack);

    // images embarquées : extraites dans un dossier propre à ce contenu, puis chemins réécrits
    const auto hash = juce::String::toHexString (static_cast<juce::int64> (juce::String (full).hashCode64()));
    const auto dir = assetsRoot.getChildFile ("facade-" + hash);
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename.toStdString();
        if (! sp::isSafeEntryName (name)) continue;
        if (name.rfind ("assets/", 0) == 0 && name.back() != '/')
        {
            const auto target = dir.getChildFile (juce::String::fromUTF8 (name.c_str()));
            if (! target.existsAsFile())
            {
                target.getParentDirectory().createDirectory();
                const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (i));
                if (stream != nullptr)
                {
                    juce::FileOutputStream o (target);
                    if (o.openedOk()) { o.writeFromInputStream (*stream, -1); ++report.images; }
                }
            }
        }
        else if (name.rfind ("packs/", 0) == 0 && juce::String (name).endsWithIgnoreCase (".neptunestyle"))
        {
            const auto id = juce::File (juce::String::fromUTF8 (name.c_str())).getFileNameWithoutExtension().toLowerCase().toStdString();
            if (stylelib::findSource (id) != juce::File() || ! stylelib::directory().createDirectory().wasOk()) continue;   // déjà installé : on garde le sien
            const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (i));
            if (stream == nullptr) continue;
            juce::FileOutputStream o (stylelib::directory().getChildFile (juce::String (id) + ".neptunestyle"));
            if (o.openedOk() && o.writeFromInputStream (*stream, -1) > 0) ++report.packsInstalled;
        }
    }
    fc::forEachImagePath (out, [&dir] (std::string& path)
    {
        if (path.rfind ("asset:", 0) == 0) path = dir.getChildFile (juce::String::fromUTF8 (path.c_str() + 6)).getFullPathName().toStdString();
    });
    report.ok = true;
    return report;
}
} // namespace facade_bundle
