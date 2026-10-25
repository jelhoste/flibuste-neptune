#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <string>
#include "FacadeBundle.h"
#include "neptune/core/TrackExport.h"

// Piste complète portable (fichier `.neptunetrack`, une archive zip) : voir core/TrackExport.h pour la disposition. Les fichiers audio de
// l'instrument et l'IR de la convolution sont copiés dans samples/, la Façade garde la disposition de `.neptunefacade` (assets/, packs/).
namespace track_bundle
{
namespace te = neptune::core::trackexport;

struct SaveReport
{
    bool ok = false;
    int samples = 0, images = 0, packs = 0, missing = 0;
};

inline SaveReport save (te::Bundle b, const juce::File& destination)
{
    SaveReport report;
    juce::ZipFile::Builder builder;
    std::map<std::string, std::string> done;   // ancien chemin -> nouvelle désignation
    te::forEachFilePath (b.state, [&] (std::string& path)
    {
        const std::string old = path;
        if (const auto it = done.find (old); it != done.end()) { path = it->second; return; }
        const juce::File file (juce::String::fromUTF8 (old.c_str()));
        if (! file.existsAsFile()) { ++report.missing; done[old] = old; return; }
        const auto name = "samples/" + juce::String (++report.samples) + "_" + file.getFileName().replaceCharacters (" \\/:", "____");
        builder.addEntry (new juce::FileInputStream (file), 1, name, juce::Time::getCurrentTime());
        path = ("asset:" + name).toStdString();
        done[old] = path;
    });
    facade_bundle::SaveReport facadeReport;
    facade_bundle::addEntries (builder, b.state.strip.facade, facadeReport);
    report.images = facadeReport.images;
    report.packs = facadeReport.packs;
    report.missing += facadeReport.missing;

    const juce::String json = juce::String::fromUTF8 (te::toJson (b).c_str());
    builder.addEntry (new juce::MemoryInputStream (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9, "track.json", juce::Time::getCurrentTime());

    destination.deleteFile();
    juce::FileOutputStream out (destination);
    report.ok = out.openedOk() && builder.writeToStream (out, nullptr);
    return report;
}

struct LoadReport
{
    bool ok = false;
    juce::String error;
    int files = 0, packsInstalled = 0;
};

// Lit une piste exportée : les fichiers embarqués sont extraits sous `assetsRoot/track-<empreinte>` (un sous-dossier par contenu), les
// chemins de l'instrument, de l'IR et de la Façade désignent ces copies, les packs de styles absents sont installés.
inline LoadReport load (const juce::File& file, const juce::File& assetsRoot, te::Bundle& out)
{
    LoadReport report;
    juce::ZipFile zip (file);
    const int jsonIndex = zip.getIndexOfFileName ("track.json");
    if (jsonIndex < 0) { report.error = "Not a track file (track.json is missing)."; return report; }
    std::string full;
    {
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (jsonIndex));
        if (stream != nullptr) full = stream->readEntireStreamAsString().toStdString();
    }
    auto parsed = te::fromJson (full);
    if (! parsed) { report.error = "The track file is unreadable."; return report; }
    out = std::move (*parsed);

    const auto dir = assetsRoot.getChildFile ("track-" + juce::String::toHexString (static_cast<juce::int64> (juce::String (full).hashCode64())));
    facade_bundle::extractEntries (zip, dir, report.files, report.packsInstalled, true);
    te::forEachFilePath (out.state, [&dir] (std::string& path)
    {
        if (path.rfind ("asset:", 0) == 0 && neptune::core::stylepack::isSafeEntryName (path.substr (6)))
            path = dir.getChildFile (juce::String::fromUTF8 (path.c_str() + 6)).getFullPathName().toStdString();
    });
    facade_bundle::resolveAssets (out.state.strip.facade, dir);
    report.ok = true;
    return report;
}
} // namespace track_bundle
