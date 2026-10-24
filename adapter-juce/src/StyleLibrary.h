#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "neptune/core/Facade.h"
#include "neptune/core/StylePack.h"

// Bibliothèque de packs de styles d'images de la Façade (docs/specification-styles-facade.md) : un dossier de fichiers `.neptunestyle`
// (archives zip, lues sur place sans rien décompresser) et de dossiers de même structure. Un style de la Façade désigne ses images par
// une référence `pack:<identifiant>!<entrée>` (voir neptune::core::stylepack::makeReference) ou par un chemin de fichier ordinaire.
namespace stylelib
{
namespace sp = neptune::core::stylepack;

inline juce::File& directoryRef()
{
    static juce::File dir;
    return dir;
}

// Dossier des packs : à régler une fois (le dossier de configuration de Neptune, sous-dossier « styles »).
inline void setDirectory (const juce::File& dir) { directoryRef() = dir; }
inline juce::File directory() { return directoryRef(); }

struct Pack
{
    sp::PackInfo info;
    juce::File source;   // le fichier .neptunestyle ou le dossier
    bool isZip = false;
};

inline std::string lowerName (const juce::String& s) { return s.toLowerCase().toStdString(); }

// Lit un petit texte d'une archive (pack.json, style.json) ; vide si absent ou trop gros.
inline std::string zipText (juce::ZipFile& zip, const std::string& name)
{
    const int index = zip.getIndexOfFileName (juce::String::fromUTF8 (name.c_str()));
    if (index < 0) return {};
    const auto* entry = zip.getEntry (index);
    if (entry == nullptr || entry->uncompressedSize > 65536) return {};
    const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
    return stream != nullptr ? stream->readEntireStreamAsString().toStdString() : std::string();
}

inline bool analyseSource (const juce::File& source, Pack& out)
{
    out.source = source;
    if (source.isDirectory())
    {
        out.isZip = false;
        std::vector<std::string> entries;
        for (const auto& f : source.findChildFiles (juce::File::findFiles, true))
            entries.push_back (f.getRelativePathFrom (source).replaceCharacter ('\\', '/').toStdString());
        out.info = sp::analyse (source.getFileName().toStdString(), entries, [&source] (const std::string& name)
        {
            const auto f = source.getChildFile (juce::String::fromUTF8 (name.c_str()));
            return f.existsAsFile() && f.getSize() <= 65536 ? f.loadFileAsString().toStdString() : std::string();
        });
        return true;
    }
    if (! source.existsAsFile()) return false;
    juce::ZipFile zip (source);
    if (zip.getNumEntries() <= 0) return false;
    out.isZip = true;
    std::vector<std::string> entries;
    for (int i = 0; i < zip.getNumEntries(); ++i) entries.push_back (zip.getEntry (i)->filename.toStdString());
    out.info = sp::analyse (source.getFileNameWithoutExtension().toStdString(), entries, [&zip] (const std::string& name) { return zipText (zip, name); });
    return true;
}

// Tous les packs de la bibliothèque, par ordre alphabétique.
inline std::vector<Pack> scan()
{
    std::vector<Pack> packs;
    const auto dir = directory();
    if (! dir.isDirectory()) return packs;
    for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        const bool archive = f.existsAsFile() && f.hasFileExtension ("neptunestyle");
        if (! archive && ! f.isDirectory()) continue;
        Pack pack;
        if (analyseSource (f, pack)) packs.push_back (std::move (pack));
    }
    std::sort (packs.begin(), packs.end(), [] (const Pack& a, const Pack& b) { return a.info.name < b.info.name; });
    return packs;
}

// Le fichier .neptunestyle ou le dossier d'un pack de la bibliothèque, par identifiant ; File() si absent.
inline juce::File findSource (const std::string& packId)
{
    const auto dir = directory();
    if (! dir.isDirectory()) return {};
    for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (f.isDirectory() && lowerName (f.getFileName()) == packId) return f;
        if (f.existsAsFile() && f.hasFileExtension ("neptunestyle") && lowerName (f.getFileNameWithoutExtension()) == packId) return f;
    }
    return {};
}

// Les octets d'un pack sous forme d'archive .neptunestyle (un pack-dossier est zippé à la volée). Faux si le pack est absent.
inline bool packArchiveBytes (const std::string& packId, juce::MemoryBlock& out)
{
    const auto source = findSource (packId);
    if (source == juce::File()) return false;
    if (source.existsAsFile()) return source.loadFileAsData (out);
    juce::ZipFile::Builder builder;
    for (const auto& f : source.findChildFiles (juce::File::findFiles, true))
    {
        juce::MemoryBlock bytes;
        if (f.loadFileAsData (bytes)) builder.addEntry (new juce::MemoryInputStream (bytes, true), 9, f.getRelativePathFrom (source).replaceCharacter ('\\', '/'), f.getLastModificationTime());
    }
    juce::MemoryOutputStream stream (out, false);
    return builder.writeToStream (stream, nullptr);
}

// L'image existe-t-elle (fichier présent, ou entrée présente dans un pack installé) ? Ne lit pas l'image.
inline bool exists (const std::string& path)
{
    if (path.empty()) return false;
    std::string packId, entry;
    if (! sp::parseReference (path, packId, entry)) return juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile();
    const auto source = findSource (packId);
    if (source == juce::File()) return false;
    if (source.isDirectory()) return source.getChildFile (juce::String::fromUTF8 (entry.c_str())).existsAsFile();
    juce::ZipFile zip (source);
    return zip.getIndexOfFileName (juce::String::fromUTF8 (entry.c_str())) >= 0;
}

// Octets d'une image désignée par une référence `pack:` ou par un chemin de fichier ordinaire. Faux si introuvable.
inline bool readBytes (const std::string& path, juce::MemoryBlock& out)
{
    if (path.empty()) return false;
    std::string packId, entry;
    if (! sp::parseReference (path, packId, entry))
        return juce::File (juce::String::fromUTF8 (path.c_str())).loadFileAsData (out);
    const auto dir = directory();
    if (! dir.isDirectory()) return false;
    for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
    {
        if (lowerName (f.isDirectory() ? f.getFileName() : f.getFileNameWithoutExtension()) != packId) continue;
        if (f.isDirectory())
            return f.getChildFile (juce::String::fromUTF8 (entry.c_str())).loadFileAsData (out);
        if (! f.hasFileExtension ("neptunestyle")) continue;
        juce::ZipFile zip (f);
        const int index = zip.getIndexOfFileName (juce::String::fromUTF8 (entry.c_str()));
        if (index < 0) return false;
        const std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (index));
        if (stream == nullptr) return false;
        stream->readIntoMemoryBlock (out);
        return out.getSize() > 0;
    }
    return false;
}

// Copie un fichier .neptunestyle dans la bibliothèque (il remplace un pack de même nom). Rend le nom du pack installé, ou "" avec `error`.
inline juce::String install (const juce::File& archive, juce::String& error, int* styleCount = nullptr)
{
    Pack pack;
    if (! archive.existsAsFile() || ! analyseSource (archive, pack) || ! pack.isZip) { error = "Not a readable .neptunestyle archive."; return {}; }
    if (pack.info.styles.empty())
    {
        error = pack.info.warnings.empty() ? "No style found in the pack." : juce::String::fromUTF8 (pack.info.warnings.front().c_str());
        return {};
    }
    const auto dir = directory();
    if (! dir.createDirectory().wasOk()) { error = "Cannot create the packs folder."; return {}; }
    const auto target = dir.getChildFile (archive.getFileNameWithoutExtension() + ".neptunestyle");
    if (target != archive && ! archive.copyFileTo (target)) { error = "Cannot copy the pack into the library."; return {}; }
    if (styleCount != nullptr) *styleCount = static_cast<int> (pack.info.styles.size());
    return juce::String::fromUTF8 (pack.info.name.c_str());
}

// Écrit un style dans une archive .neptunestyle (à la racine : off, on, track, handle, strip_N) avec pack.json et style.json.
inline bool exportStyle (const neptune::core::facade::ImageStyle& style, const juce::File& destination)
{
    juce::ZipFile::Builder builder;
    int added = 0;
    auto add = [&builder, &added] (const std::string& source, const juce::String& stem)
    {
        if (source.empty()) return;
        juce::MemoryBlock bytes;
        if (! readBytes (source, bytes) || bytes.getSize() == 0) return;
        const auto ext = juce::File (juce::String::fromUTF8 (source.c_str())).getFileExtension().toLowerCase();   // « .svg », « .png »...
        builder.addEntry (new juce::MemoryInputStream (bytes, true), 9, stem + (ext.isEmpty() ? juce::String (".png") : ext), juce::Time::getCurrentTime());
        ++added;
    };
    add (style.off, "off");
    add (style.on, "on");
    add (style.track, "track");
    add (style.handle, "handle");
    if (style.frames > 1) add (style.strip, "strip_" + juce::String (style.frames) + (style.stripHorizontal ? "_h" : ""));
    if (added == 0) return false;

    auto text = [] (const juce::String& s) { return new juce::MemoryInputStream (s.toRawUTF8(), s.getNumBytesAsUTF8(), true); };
    juce::DynamicObject::Ptr pack (new juce::DynamicObject());
    pack->setProperty ("format", sp::formatVersion);
    pack->setProperty ("name", juce::String::fromUTF8 (style.name.c_str()));
    builder.addEntry (text (juce::JSON::toString (juce::var (pack.get()))), 9, "pack.json", juce::Time::getCurrentTime());
    juce::DynamicObject::Ptr meta (new juce::DynamicObject());
    meta->setProperty ("name", juce::String::fromUTF8 (style.name.c_str()));
    if (style.frames > 1) meta->setProperty ("frames", style.frames);
    builder.addEntry (text (juce::JSON::toString (juce::var (meta.get()))), 9, "style.json", juce::Time::getCurrentTime());

    destination.deleteFile();
    juce::FileOutputStream out (destination);
    if (! out.openedOk()) return false;
    return builder.writeToStream (out, nullptr);
}
} // namespace stylelib
