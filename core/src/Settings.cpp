#include "neptune/core/Settings.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "neptune/core/FileUtil.h"

namespace neptune::core::settings
{
namespace
{
constexpr const char* kFormatTag = "neptune-settings";

void warn (std::vector<std::string>* w, std::string msg)
{
    if (w) w->push_back (std::move (msg));
}

bool inList (const std::string& v, std::initializer_list<const char*> list)
{
    return std::any_of (list.begin(), list.end(), [&] (const char* s) { return v == s; });
}

// Lit un entier borné ; hors bornes ou mauvais type -> défaut.
int readInt (const nlohmann::json& o, const char* key, int def, int lo, int hi, std::vector<std::string>* w)
{
    const auto it = o.find (key);
    if (it == o.end()) return def;
    if (! it->is_number_integer())
    {
        warn (w, std::string (key) + ": expected an integer, using default");
        return def;
    }
    const long long v = it->get<long long>();
    if (v < lo || v > hi)
    {
        warn (w, std::string (key) + ": out of range, using default");
        return def;
    }
    return static_cast<int> (v);
}

std::string readString (const nlohmann::json& o, const char* key, const std::string& def, std::vector<std::string>* w)
{
    const auto it = o.find (key);
    if (it == o.end()) return def;
    if (! it->is_string())
    {
        warn (w, std::string (key) + ": expected a string, using default");
        return def;
    }
    return it->get<std::string>();
}
} // namespace

std::string toJson (const GlobalSettings& s)
{
    nlohmann::json o;
    o["format"] = kFormatTag;
    o["schema_version"] = currentSchemaVersion;
    o["language"] = s.language;
    o["theme"] = s.theme;
    o["contrast"] = s.contrast;
    o["performance_profile"] = s.profile == PerformanceProfile::Studio ? "studio" : "live";
    o["samples_dir"] = s.samplesDir;
    o["presets_dir"] = s.presetsDir;
    o["ir_dir"] = s.irDir;
    o["export_dir"] = s.exportDir;
    o["autosave_interval_seconds"] = s.autosaveIntervalSeconds;
    o["autosave_keep_days"] = s.autosaveKeepDays;
    return o.dump (2);
}

GlobalSettings fromJson (std::string_view text, std::vector<std::string>* warnings)
{
    GlobalSettings s; // valeurs par défaut
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, /*allow_exceptions*/ false);
    if (doc.is_discarded() || ! doc.is_object())
    {
        warn (warnings, "settings file is not a valid JSON object, using defaults");
        return s;
    }

    const auto fmt = doc.find ("format");
    if (fmt == doc.end() || ! fmt->is_string() || fmt->get<std::string>() != kFormatTag)
    {
        warn (warnings, "settings file is not a Neptune settings file, using defaults");
        return s;
    }

    const auto ver = doc.find ("schema_version");
    if (ver != doc.end() && ver->is_number_integer() && ver->get<long long>() > currentSchemaVersion)
        warn (warnings, "settings written by a newer version of Neptune: unknown entries are ignored");

    const std::string lang = readString (doc, "language", s.language, warnings);
    if (! lang.empty() && lang.size() <= 16) s.language = lang;
    else warn (warnings, "language: invalid, using default");

    const std::string theme = readString (doc, "theme", s.theme, warnings);
    if (inList (theme, { "dark", "light", "hc", "hammond" })) s.theme = theme;
    else warn (warnings, "theme: unknown, using default");

    s.contrast = readInt (doc, "contrast", s.contrast, 0, 4, warnings);

    const std::string profile = readString (doc, "performance_profile", "live", warnings);
    if (profile == "live") s.profile = PerformanceProfile::Live;
    else if (profile == "studio") s.profile = PerformanceProfile::Studio;
    else warn (warnings, "performance_profile: unknown, using default");

    s.samplesDir = readString (doc, "samples_dir", s.samplesDir, warnings);
    s.presetsDir = readString (doc, "presets_dir", s.presetsDir, warnings);
    s.irDir = readString (doc, "ir_dir", s.irDir, warnings);
    s.exportDir = readString (doc, "export_dir", s.exportDir, warnings);

    int interval = readInt (doc, "autosave_interval_seconds", s.autosaveIntervalSeconds, 0, 3600, warnings);
    if (interval != 0 && interval < 5)
    {
        warn (warnings, "autosave_interval_seconds: below 5, using default");
        interval = s.autosaveIntervalSeconds;
    }
    s.autosaveIntervalSeconds = interval;
    s.autosaveKeepDays = readInt (doc, "autosave_keep_days", s.autosaveKeepDays, 1, 365, warnings);
    return s;
}

GlobalSettings loadFile (const std::filesystem::path& path, std::vector<std::string>* warnings)
{
    std::string text;
    if (! fileutil::readAll (path, text))
        return {};
    return fromJson (text, warnings);
}

bool saveFile (const std::filesystem::path& path, const GlobalSettings& s, std::string* error)
{
    return fileutil::writeAtomic (path, toJson (s), error);
}
} // namespace neptune::core::settings
