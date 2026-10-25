#pragma once
#include <juce_core/juce_core.h>
#include <set>
#include <string>
#include "FacadeBundle.h"
#include "neptune/core/GlobalPreset.h"

// Bibliothèque des presets globaux de Neptune : un fichier `.neptuneset` par preset (zip, voir core/GlobalPreset.h) dans le dossier de
// configuration. Les samples ne sont PAS copiés dans le fichier (chemins conservés, comme dans le projet) ; les images et packs de styles
// des Façades le sont.
namespace global_presets
{
namespace gp = neptune::core::globalpreset;

inline juce::File folder (const juce::File& settingsDir) { return settingsDir.getChildFile ("GlobalPresets"); }

inline juce::String safeName (juce::String name)
{
    name = name.trim().replaceCharacters ("\\/:*?\"<>|", "_________");
    return name.substring (0, 80).trim();
}

inline juce::File fileFor (const juce::File& settingsDir, const juce::String& name) { return folder (settingsDir).getChildFile (safeName (name) + ".neptuneset"); }

// Les presets, par ordre alphabétique (sans l'extension).
inline juce::Array<juce::File> list (const juce::File& settingsDir)
{
    auto files = folder (settingsDir).findChildFiles (juce::File::findFiles, false, "*.neptuneset");
    files.sort();
    return files;
}

struct SaveReport
{
    bool ok = false;
    int images = 0, packs = 0, missing = 0;
};

inline SaveReport save (const std::string& stateText, const juce::File& destination)
{
    SaveReport report;
    juce::ZipFile::Builder builder;
    facade_bundle::SaveReport images;
    std::set<std::string> packs;
    const auto text = gp::editMixer (gp::withoutInstanceData (stateText), [&] (neptune::core::MixerState& m)
    {
        gp::forEachFacade (m, [&] (neptune::core::facade::Facade& f) { facade_bundle::addEntries (builder, f, images, &packs); });
    });
    report.images = images.images;
    report.packs = images.packs;
    report.missing = images.missing;
    builder.addEntry (new juce::MemoryInputStream (text.data(), text.size(), true), 9, "state.json", juce::Time::getCurrentTime());
    destination.getParentDirectory().createDirectory();
    destination.deleteFile();
    juce::FileOutputStream out (destination);
    report.ok = out.openedOk() && builder.writeToStream (out, nullptr);
    return report;
}

// Rend le texte d'état prêt à être chargé (images extraites sous `assetsRoot`, chemins résolus), ou vide avec `error`.
inline std::string load (const juce::File& file, const juce::File& assetsRoot, juce::String& error)
{
    juce::ZipFile zip (file);
    const int index = zip.getIndexOfFileName ("state.json");
    if (index < 0) { error = "Not a global preset (state.json is missing)."; return {}; }
    std::string text;
    {
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
        if (stream != nullptr) text = stream->readEntireStreamAsString().toStdString();
    }
    if (text.empty()) { error = "The global preset is unreadable."; return {}; }
    const auto dir = assetsRoot.getChildFile ("preset-" + juce::String::toHexString (static_cast<juce::int64> (juce::String (text).hashCode64())));
    int files = 0, packsInstalled = 0;
    facade_bundle::extractEntries (zip, dir, files, packsInstalled, false);
    return gp::editMixer (text, [&dir] (neptune::core::MixerState& m)
    {
        gp::forEachFacade (m, [&dir] (neptune::core::facade::Facade& f) { facade_bundle::resolveAssets (f, dir); });
    });
}
} // namespace global_presets
