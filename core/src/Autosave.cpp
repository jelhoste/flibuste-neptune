#include "neptune/core/Autosave.h"

#include <chrono>
#include <random>
#include <set>
#include <system_error>

#include <nlohmann/json.hpp>

#include "neptune/core/FileUtil.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <signal.h>
 #include <sys/types.h>
 #include <unistd.h>
#endif

namespace neptune::core
{
namespace
{
namespace fs = std::filesystem;

constexpr const char* kFormatTag = "neptune-autosave";
constexpr int kSchemaVersion = 1;
constexpr const char* kAutosaveExt = ".neptuneautosave";
constexpr const char* kRecoveredExt = ".neptunerecovered";

std::mutex& registryMutex() { static std::mutex m; return m; }
std::set<std::string>& registry() { static std::set<std::string> s; return s; }

std::string makeId()
{
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen ((static_cast<std::uint64_t> (rd()) << 32) ^ rd()
                         ^ static_cast<std::uint64_t> (std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string id;
    for (int i = 0; i < 32; ++i)
        id.push_back (hex[gen() & 0xf]);
    return id;
}

std::int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}

struct FileContent
{
    std::string state;
    std::int64_t pid = 0;
    std::int64_t savedAt = 0;
};

std::optional<FileContent> parseFile (const fs::path& p)
{
    std::string text;
    if (! fileutil::readAll (p, text)) return std::nullopt;
    const auto doc = nlohmann::json::parse (text, nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return std::nullopt;
    const auto fmt = doc.find ("format");
    const auto st = doc.find ("state");
    if (fmt == doc.end() || ! fmt->is_string() || fmt->get<std::string>() != kFormatTag) return std::nullopt;
    if (st == doc.end() || ! st->is_string()) return std::nullopt;

    FileContent c;
    c.state = st->get<std::string>();
    if (const auto it = doc.find ("pid"); it != doc.end() && it->is_number_integer()) c.pid = it->get<std::int64_t>();
    if (const auto it = doc.find ("saved_at"); it != doc.end() && it->is_number_integer()) c.savedAt = it->get<std::int64_t>();
    return c;
}

bool endsWith (const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare (s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

std::int64_t AutosaveStore::currentProcessId()
{
#if defined(_WIN32)
    return static_cast<std::int64_t> (GetCurrentProcessId());
#else
    return static_cast<std::int64_t> (getpid());
#endif
}

bool AutosaveStore::isProcessAlive (std::int64_t pid)
{
    if (pid <= 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD> (pid));
    if (h == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED; // existe mais inaccessible
    DWORD code = 0;
    const bool ok = GetExitCodeProcess (h, &code) != 0;
    CloseHandle (h);
    return ok && code == STILL_ACTIVE;
#else
    if (kill (static_cast<pid_t> (pid), 0) == 0) return true;
    return errno == EPERM; // existe mais appartient à un autre utilisateur
#endif
}

bool AutosaveStore::isValidId (std::string_view id) noexcept
{
    if (id.size() != 32) return false;
    for (const char c : id)
        if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

AutosaveStore::AutosaveStore (fs::path directory) : dir_ (std::move (directory))
{
    std::lock_guard<std::mutex> lock (registryMutex());
    do { id_ = makeId(); } while (! registry().insert (id_).second);
}

AutosaveStore::~AutosaveStore()
{
    std::lock_guard<std::mutex> lock (registryMutex());
    registry().erase (id_);
}

fs::path AutosaveStore::autosavePath() const { return dir_ / (id_ + kAutosaveExt); }
fs::path AutosaveStore::recoveredPath() const { return dir_ / (id_ + kRecoveredExt); }

bool AutosaveStore::adoptId (const std::string& candidate)
{
    if (! isValidId (candidate)) return false;
    std::lock_guard<std::mutex> lock (mutex_);
    if (candidate == id_) return true;
    {
        std::lock_guard<std::mutex> reg (registryMutex());
        if (! registry().insert (candidate).second) return false; // déjà portée par une autre instance de ce processus
        registry().erase (id_);
    }
    std::error_code ec;
    fs::remove (autosavePath(), ec); // l'ancien identifiant, aléatoire, n'a plus de raison d'avoir un fichier
    id_ = candidate;
    haveLast_ = false;
    lastWritten_.clear();
    return true;
}

void AutosaveStore::setBaseline (std::string_view stateText)
{
    std::lock_guard<std::mutex> lock (mutex_);
    lastWritten_.assign (stateText);
    haveLast_ = true;
}

bool AutosaveStore::write (std::string_view stateText, std::string* error)
{
    std::lock_guard<std::mutex> lock (mutex_);
    if (haveLast_ && lastWritten_ == stateText) return true;

    nlohmann::json doc;
    doc["format"] = kFormatTag;
    doc["schema_version"] = kSchemaVersion;
    doc["instance_id"] = id_;
    doc["pid"] = currentProcessId();
    doc["saved_at"] = nowSeconds();
    doc["state"] = std::string (stateText);

    if (! fileutil::writeAtomic (autosavePath(), doc.dump (2), error)) return false;
    lastWritten_.assign (stateText);
    haveLast_ = true;
    return true;
}

void AutosaveStore::discard()
{
    std::lock_guard<std::mutex> lock (mutex_);
    std::error_code ec;
    fs::remove (autosavePath(), ec);
    haveLast_ = false;
    lastWritten_.clear();
}

std::optional<RecoveredState> AutosaveStore::takeRecoverable()
{
    std::lock_guard<std::mutex> lock (mutex_);
    std::error_code ec;

    // Un état déjà conservé par une session précédente et jamais résolu par l'utilisateur.
    if (fs::exists (recoveredPath(), ec))
    {
        if (const auto c = parseFile (recoveredPath()))
            return RecoveredState { c->state, c->savedAt };
        fs::remove (recoveredPath(), ec); // illisible : inutilisable
    }

    if (! fs::exists (autosavePath(), ec)) return std::nullopt;
    const auto c = parseFile (autosavePath());
    if (! c)
    {
        fs::remove (autosavePath(), ec);
        return std::nullopt;
    }
    if (c->pid == currentProcessId() || isProcessAlive (c->pid))
        return std::nullopt; // pas orphelin : une instance vivante l'écrit

    fs::rename (autosavePath(), recoveredPath(), ec);
    if (ec) return std::nullopt;
    return RecoveredState { c->state, c->savedAt };
}

void AutosaveStore::resolveRecovered()
{
    std::lock_guard<std::mutex> lock (mutex_);
    std::error_code ec;
    fs::remove (recoveredPath(), ec);
}

int AutosaveStore::purgeStale (const fs::path& directory, int keepDays)
{
    std::error_code ec;
    if (! fs::is_directory (directory, ec)) return 0;

    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours (24 * static_cast<long long> (keepDays));
    int removed = 0;
    for (const auto& entry : fs::directory_iterator (directory, ec))
    {
        if (! entry.is_regular_file (ec)) continue;
        const std::string name = entry.path().filename().string();
        const bool isAuto = endsWith (name, kAutosaveExt);
        const bool isRecovered = endsWith (name, kRecoveredExt);
        if (! isAuto && ! isRecovered) continue;

        if (isAuto)
        {
            const auto c = parseFile (entry.path());
            if (c && (c->pid == currentProcessId() || isProcessAlive (c->pid))) continue; // écrit par une instance vivante
        }

        const auto modified = entry.last_write_time (ec);
        if (ec || modified > limit) continue;
        if (fs::remove (entry.path(), ec)) ++removed;
    }
    return removed;
}
} // namespace neptune::core
