#include "neptune/core/PresetLibrary.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <system_error>

#include <nlohmann/json.hpp>

#include "neptune/core/FileUtil.h"

namespace neptune::core::preset
{
namespace fs = std::filesystem;

namespace
{
constexpr const char* kPresetsDir = "presets";
constexpr const char* kTrashDir = "trash";
constexpr const char* kGridsDir = "grids";

long long nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string stamp (long long epoch)
{
    const std::time_t t = static_cast<std::time_t> (epoch);
    std::tm tm {};
#if defined(_WIN32)
    localtime_s (&tm, &t);
#else
    localtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof buf, "%Y-%m-%d_%Hh%M", &tm);
    return buf;
}

std::string safeLabel (const std::string& name)
{
    std::string s;
    for (unsigned char c : name)
    {
        if (c >= 0x80 || std::isalnum (c)) s += static_cast<char> (c);
        else if (! s.empty() && s.back() != '-') s += '-';
        if (s.size() >= 40) break;
    }
    while (! s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "preset" : s;
}

// Un id vient d'un fichier ou d'un import : il doit servir de nom de fichier sans risque.
bool safeId (const std::string& id)
{
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id)
        if (! (std::isalnum (c) || c == '-' || c == '_')) return false;
    return true;
}
} // namespace

Library::Library (fs::path root, std::vector<Preset> factory) : root_ (std::move (root)), factory_ (std::move (factory))
{
    for (auto& p : factory_)
    {
        p = sanitised (std::move (p));
        p.factory = true;
        p.locked = true; // un preset factory se comporte comme verrouillé
    }
}

bool Library::open (std::vector<std::string>* warnings)
{
    std::error_code ec;
    fs::create_directories (root_ / kPresetsDir, ec);
    if (ec) { if (warnings) warnings->push_back ("cannot create " + root_.string()); return false; }

    std::string text;
    if (fileutil::readAll (root_ / "config.json", text)) config_ = configFromJson (text);
    else fileutil::writeAtomic (root_ / "config.json", toJson (config_));

    banks_ = BankManifest {};
    if (fileutil::readAll (root_ / "banks.json", text)) banks_ = banksFromJson (text);

    user_.clear();
    for (const auto& e : fs::directory_iterator (root_ / kPresetsDir, ec))
    {
        if (ec) break;
        if (! e.is_regular_file() || e.path().extension() != presetExtension) continue;
        std::string body;
        if (! fileutil::readAll (e.path(), body)) { if (warnings) warnings->push_back ("unreadable: " + e.path().filename().string()); continue; }
        bool ok = false;
        Preset p = presetFromJson (body, &ok);
        if (! ok || ! safeId (p.id)) { if (warnings) warnings->push_back ("invalid: " + e.path().filename().string()); continue; }
        if (find (p.id) != nullptr) { if (warnings) warnings->push_back ("duplicate id: " + e.path().filename().string()); continue; }
        user_.push_back (std::move (p));
    }
    std::sort (user_.begin(), user_.end(), [] (const Preset& a, const Preset& b) { return a.name < b.name; });

    grids_.clear();
    ec.clear();
    if (fs::exists (root_ / kGridsDir, ec))
        for (const auto& e : fs::directory_iterator (root_ / kGridsDir, ec))
        {
            if (ec) break;
            if (! e.is_regular_file() || e.path().extension() != ".json") continue;
            std::string body;
            if (fileutil::readAll (e.path(), body)) grids_.push_back (gridFromJson (body));
        }
    purgeTrash();
    return true;
}

std::vector<Preset> Library::allPresets() const
{
    std::vector<Preset> all = factory_;
    all.insert (all.end(), user_.begin(), user_.end());
    return all;
}

const Preset* Library::find (const std::string& id) const
{
    for (const auto& p : user_)
        if (p.id == id) return &p;
    for (const auto& p : factory_)
        if (p.id == id) return &p;
    return nullptr;
}

std::vector<std::string> Library::allIds() const
{
    std::vector<std::string> ids;
    for (const auto& p : factory_) ids.push_back (p.id);
    for (const auto& p : user_) ids.push_back (p.id);
    return ids;
}

fs::path Library::presetPath (const std::string& id) const { return root_ / kPresetsDir / (id + presetExtension); }

bool Library::writePreset (const Preset& p, std::string* error)
{
    if (! safeId (p.id)) { if (error) *error = "invalid id"; return false; }
    return fileutil::writeAtomic (presetPath (p.id), toJson (p), error);
}

bool Library::save (Preset p, std::string* error)
{
    p = sanitised (std::move (p));
    if (const Preset* known = find (p.id); known != nullptr && known->factory) { if (error) *error = "factory preset is read-only"; return false; }
    for (auto& u : user_)
        if (u.id == p.id)
        {
            if (u.locked) { if (error) *error = "preset is locked"; return false; }
            p.factory = false;
            if (! writePreset (p, error)) return false;
            u = std::move (p);
            return true;
        }
    p.factory = false;
    if (! writePreset (p, error)) return false;
    user_.push_back (std::move (p));
    return true;
}

bool Library::saveAs (const Preset& source, const std::string& newName, Preset* out, std::string* error)
{
    Preset p = source;
    p.id = newId();
    p.name = newName.empty() ? source.name : newName;
    p.locked = false;
    p.factory = false;
    if (source.factory) p.derivedFrom = source.id;
    else if (! source.derivedFrom.empty()) p.derivedFrom = source.derivedFrom;
    p = sanitised (std::move (p));
    if (! writePreset (p, error)) return false;
    user_.push_back (p);
    if (out != nullptr) *out = p;
    return true;
}

bool Library::importPreset (Preset p, Preset* out, std::string* error)
{
    p = sanitised (std::move (p));
    if (! safeId (p.id) || find (p.id) != nullptr) p.id = newId();
    p.factory = false;
    if (! writePreset (p, error)) return false;
    user_.push_back (p);
    if (out != nullptr) *out = p;
    return true;
}

bool Library::updateMetadata (const Preset& edited, std::string* error)
{
    for (auto& u : user_)
        if (u.id == edited.id)
        {
            Preset p = u;
            p.name = edited.name;
            p.type = edited.type;
            p.characteristics = edited.characteristics;
            p.rating = edited.rating;
            p.locked = edited.locked;
            p.customFields = edited.customFields;
            p = sanitised (std::move (p));
            if (! writePreset (p, error)) return false;
            u = std::move (p);
            return true;
        }
    if (error) *error = "unknown or factory preset";
    return false;
}

std::string Library::stepFrom (const std::string& currentId, int direction, const std::function<bool (const Preset&)>& accept) const
{
    if (direction == 0) return {};
    auto usable = [this, &accept] (const std::string& id)
    {
        const auto* p = find (id);
        return p != nullptr && (! accept || accept (*p));
    };
    if (const auto slot = Navigator::locate (banks_, currentId))
    {
        Slot s = *slot;
        // les emplacements dont le preset n'est pas utilisable ici sont sautés comme les vides
        for (int guard = 0; guard < numBanks * programsPerBank; ++guard)
        {
            const auto next = Navigator::step (banks_, s, direction);
            if (! next) return {};
            s = *next;
            const auto id = Navigator::presetAt (banks_, s);
            if (usable (id)) return id;
            if (s == *slot) return {};
        }
        return {};
    }
    std::vector<const Preset*> all;
    const auto everything = allPresets();
    for (const auto* p : filter (everything, {}))
        if (! accept || accept (*p)) all.push_back (p);
    if (all.empty()) return {};
    int index = -1;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i]->id == currentId) index = static_cast<int> (i);
    const int count = static_cast<int> (all.size());
    const int next = index < 0 ? (direction > 0 ? 0 : count - 1) : (index + (direction > 0 ? 1 : -1) + count) % count;
    return all[static_cast<std::size_t> (next)]->id;
}

// ---------------------------------------------------------------------------------------------------- corbeille

bool Library::moveToTrash (const std::string& id, std::string* error)
{
    const auto it = std::find_if (user_.begin(), user_.end(), [&id] (const Preset& p) { return p.id == id; });
    if (it == user_.end()) { if (error) *error = "unknown or factory preset"; return false; }
    const long long now = nowSeconds();
    const auto folder = stamp (now) + "_" + safeLabel (it->name);
    fs::path dir = root_ / kTrashDir / folder;
    for (int n = 2; fs::exists (dir); ++n) dir = root_ / kTrashDir / (folder + "-" + std::to_string (n));
    if (! fileutil::writeAtomic (dir / (id + presetExtension), toJson (*it), error)) return false;
    nlohmann::json meta;
    meta["deletedAt"] = now;
    meta["presetId"] = id;
    meta["name"] = it->name;
    if (! fileutil::writeAtomic (dir / "meta.json", meta.dump (2), error)) return false;
    std::error_code ec;
    fs::remove (presetPath (id), ec);
    if (ec) { if (error) *error = ec.message(); return false; }
    user_.erase (it);
    if (banks_.forget (id) > 0) saveBanks();
    purgeTrash();
    return true;
}

std::vector<TrashEntry> Library::trash() const
{
    std::vector<TrashEntry> out;
    std::error_code ec;
    if (! fs::exists (root_ / kTrashDir, ec)) return out;
    for (const auto& e : fs::directory_iterator (root_ / kTrashDir, ec))
    {
        if (ec) break;
        if (! e.is_directory()) continue;
        std::string body;
        if (! fileutil::readAll (e.path() / "meta.json", body)) continue;
        const auto meta = nlohmann::json::parse (body, nullptr, false);
        if (meta.is_discarded() || ! meta.is_object()) continue;
        TrashEntry t;
        t.folder = e.path().filename().string();
        t.label = t.folder;
        t.presetId = meta.value ("presetId", std::string());
        t.name = meta.value ("name", std::string());
        t.deletedAt = meta.value ("deletedAt", 0LL);
        if (safeId (t.presetId)) out.push_back (std::move (t));
    }
    std::sort (out.begin(), out.end(), [] (const TrashEntry& a, const TrashEntry& b) { return a.deletedAt > b.deletedAt; });
    return out;
}

bool Library::restore (const TrashEntry& t, std::string* error)
{
    if (! safeId (t.presetId) || t.folder.empty() || t.folder.find_first_of ("/\\") != std::string::npos) { if (error) *error = "invalid entry"; return false; }
    const fs::path dir = root_ / kTrashDir / t.folder;
    std::string body;
    if (! fileutil::readAll (dir / (t.presetId + presetExtension), body)) { if (error) *error = "missing file"; return false; }
    Preset p = presetFromJson (body);
    if (! importPreset (std::move (p), nullptr, error)) return false;
    std::error_code ec;
    fs::remove_all (dir, ec);
    return true;
}

int Library::purgeTrash (long long now)
{
    if (now < 0) now = nowSeconds();
    auto entries = trash(); // plus récentes d'abord
    int removed = 0;
    std::error_code ec;
    const long long limit = static_cast<long long> (config_.trashRetentionDays) * 86400;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const bool tooOld = now - entries[i].deletedAt > limit;
        const bool tooMany = static_cast<int> (i) >= config_.trashMaxEntries;
        if (! tooOld && ! tooMany) continue;
        fs::remove_all (root_ / kTrashDir / entries[i].folder, ec);
        ++removed;
    }
    return removed;
}

bool Library::emptyTrash()
{
    std::error_code ec;
    for (const auto& t : trash()) fs::remove_all (root_ / kTrashDir / t.folder, ec);
    return ! ec;
}

// ---------------------------------------------------------------------------------------------------- banques

bool Library::saveBanks (std::string* error) { return fileutil::writeAtomic (root_ / "banks.json", toJson (banks_), error); }

bool Library::setBank (const Bank& b)
{
    if (b.index < 0 || b.index >= numBanks) return false;
    banks_.ensure (b.index) = b;
    return true;
}

bool Library::assign (int bank, int program, const std::string& presetId, std::string* error)
{
    if (bank < 0 || bank >= numBanks || program < 0 || program >= programsPerBank) { if (error) *error = "out of range"; return false; }
    if (! presetId.empty() && find (presetId) == nullptr) { if (error) *error = "unknown preset"; return false; }
    banks_.ensure (bank).presets[static_cast<std::size_t> (program)] = presetId;
    return saveBanks (error);
}

BundleManifest Library::exportManifest (int bank, std::vector<Preset>* presetsOut) const
{
    BundleManifest m;
    const Bank* b = banks_.find (bank);
    if (b == nullptr) return m;
    m.bankName = displayName (*b);
    m.bankCustomFields = b->customFields;
    for (const auto& id : b->presets)
    {
        if (id.empty()) continue;
        const Preset* p = find (id);
        if (p == nullptr || std::find (m.presetOrder.begin(), m.presetOrder.end(), id) != m.presetOrder.end()) continue;
        m.presetOrder.push_back (id);
        if (presetsOut != nullptr) presetsOut->push_back (*p);
    }
    return m;
}

int Library::importBundle (const BundleManifest& m, std::vector<Preset> presets, int targetBank, std::string* error)
{
    if (targetBank < 0 || targetBank >= numBanks) { if (error) *error = "out of range"; return 0; }
    Bank& b = banks_.ensure (targetBank);
    int placed = 0;
    int next = 0;
    auto place = [&] (const std::string& id) -> bool
    {
        while (next < programsPerBank && ! b.presets[static_cast<std::size_t> (next)].empty()) ++next;
        if (next >= programsPerBank) return false;
        b.presets[static_cast<std::size_t> (next++)] = id;
        return true;
    };
    // L'ordre du manifeste fait foi ; les presets du bundle absents de la liste suivent.
    std::vector<Preset> ordered;
    for (const auto& id : m.presetOrder)
        for (auto& p : presets)
            if (p.id == id) { ordered.push_back (std::move (p)); p.id.clear(); break; }
    for (auto& p : presets)
        if (! p.id.empty()) ordered.push_back (std::move (p));
    for (auto& p : ordered)
    {
        Preset stored;
        if (! importPreset (std::move (p), &stored, error)) continue;
        if (! place (stored.id)) break;
        ++placed;
    }
    saveBanks();
    return placed;
}

bool Library::setConfig (const LibraryConfig& c, std::string* error)
{
    config_ = c;
    return fileutil::writeAtomic (root_ / "config.json", toJson (config_), error);
}

// ---------------------------------------------------------------------------------------------------- grilles

Grid& Library::newGrid (const std::string& name)
{
    Grid g;
    g.gridId = newId();
    g.name = name;
    grids_.push_back (std::move (g));
    return grids_.back();
}

bool Library::saveGrid (const Grid& g, std::string* error)
{
    if (! safeId (g.gridId)) { if (error) *error = "invalid id"; return false; }
    if (! fileutil::writeAtomic (root_ / kGridsDir / (g.gridId + ".json"), toJson (g), error)) return false;
    for (auto& e : grids_)
        if (e.gridId == g.gridId) { e = g; return true; }
    grids_.push_back (g);
    return true;
}

bool Library::deleteGrid (const std::string& gridId)
{
    if (! safeId (gridId)) return false;
    std::error_code ec;
    fs::remove (root_ / kGridsDir / (gridId + ".json"), ec);
    grids_.erase (std::remove_if (grids_.begin(), grids_.end(), [&gridId] (const Grid& g) { return g.gridId == gridId; }), grids_.end());
    return true;
}

std::vector<std::string> Library::gridProblems (const Grid& g) const { return g.missing (allIds()); }
} // namespace neptune::core::preset
