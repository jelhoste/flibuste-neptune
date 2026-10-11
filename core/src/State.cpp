#include "neptune/core/State.h"

#include <charconv>
#include <cmath>
#include <vector>

#include "neptune/core/StateMigration.h"

namespace neptune::core::state
{
namespace
{
constexpr const char* kFormatTag = "neptune-state";

LoadResult fail (Status status, std::string message, int version = 0)
{
    LoadResult r;
    r.status = status;
    r.message = std::move (message);
    r.fileVersion = version;
    return r;
}
} // namespace

const MigrationTable& registeredMigrations()
{
    static const MigrationTable table; // version 1 : rien à migrer
    return table;
}

bool migrate (nlohmann::json& doc, int from, int to, const MigrationTable& table, std::string* error)
{
    for (int v = from; v < to; ++v)
    {
        const auto it = table.find (v);
        if (it == table.end())
        {
            if (error) *error = "no migration from schema version " + std::to_string (v);
            return false;
        }
        if (! it->second (doc))
        {
            if (error) *error = "migration from schema version " + std::to_string (v) + " failed";
            return false;
        }
        doc["schema_version"] = v + 1;
    }
    return true;
}

std::string save (const ParameterSet& set)
{
    nlohmann::json params = nlohmann::json::object();
    for (std::size_t i = 0; i < set.size(); ++i)
        params[std::to_string (set.spec (i).id)] = set.getAt (i);

    nlohmann::json doc;
    doc["format"] = kFormatTag;
    doc["schema_version"] = currentSchemaVersion;
    doc["parameters"] = std::move (params);
    return doc.dump (2);
}

LoadResult load (ParameterSet& set, std::string_view text)
{
    auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, /*allow_exceptions*/ false);
    if (doc.is_discarded() || ! doc.is_object())
        return fail (Status::InvalidJson, "state is not a valid JSON object");

    const auto fmt = doc.find ("format");
    if (fmt == doc.end() || ! fmt->is_string() || fmt->get<std::string>() != kFormatTag)
        return fail (Status::NotNeptune, "JSON document is not a Neptune state");

    const auto ver = doc.find ("schema_version");
    if (ver == doc.end() || ! ver->is_number_integer() || ver->get<long long>() < 1)
        return fail (Status::InvalidJson, "missing or invalid schema_version");

    const long long raw = ver->get<long long>();
    if (raw > currentSchemaVersion)
        return fail (Status::TooNew,
                     "state was written by a newer version of Neptune (schema " + std::to_string (raw)
                         + ", this version reads up to " + std::to_string (currentSchemaVersion) + ")",
                     static_cast<int> (raw));
    const int version = static_cast<int> (raw);

    if (version < currentSchemaVersion)
    {
        std::string err;
        if (! migrate (doc, version, currentSchemaVersion, registeredMigrations(), &err))
            return fail (Status::MigrationFailed, err, version);
    }

    const auto params = doc.find ("parameters");
    if (params == doc.end() || ! params->is_object())
        return fail (Status::InvalidJson, "missing \"parameters\" object", version);

    // Passe 1 : tout valider et résoudre les index, sans toucher à `set`.
    struct Entry { std::size_t index; double value; };
    std::vector<Entry> entries;
    entries.reserve (params->size());
    LoadResult result;
    result.fileVersion = version;

    for (const auto& [key, value] : params->items())
    {
        std::uint32_t id = 0;
        const char* const first = key.data();
        const char* const last = first + key.size();
        const auto parsed = std::from_chars (first, last, id);
        const std::size_t index = (parsed.ec == std::errc() && parsed.ptr == last && ! key.empty())
                                      ? set.indexOf (id)
                                      : ParameterSet::npos;
        if (index == ParameterSet::npos || ! value.is_number() || ! std::isfinite (value.get<double>()))
        {
            ++result.ignored;
            continue;
        }
        entries.push_back ({ index, value.get<double>() });
    }

    // Passe 2 : appliquer.
    set.resetAll();
    for (const auto& e : entries)
        set.setAt (e.index, e.value);
    result.applied = static_cast<int> (entries.size());
    return result;
}
} // namespace neptune::core::state
