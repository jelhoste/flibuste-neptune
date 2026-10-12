#pragma once
// Estimation de la mémoire d'un instrument et garde-fou « RAM disponible » (palier 1).
// Thread : message/chargement uniquement (lit le disque : en-têtes des fichiers).
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "neptune/core/DiskStreamer.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/SampleLoader.h"

namespace neptune::core::memory
{
// Cache chemin -> en-tête du fichier. Un fichier n'est ouvert qu'une fois (les chemins ne changent pas de contenu pendant
// une session d'édition ; `forget` / `clear` si besoin).
class FileInfoCache
{
public:
    const AudioFileInfo& info (const std::string& path);
    std::uint64_t ramBytes (const std::string& path) { return info (path).ramBytes; }
    void clear() { cache_.clear(); }

private:
    std::unordered_map<std::string, AudioFileInfo> cache_;
};

// Mémoire que l'instrument occupera : somme, pour chaque FICHIER distinct, de ce qui est chargé selon loadedZones()
// (un fichier utilisé par plusieurs zones n'est compté qu'une fois : le cache du chargeur le partage).
// `unreadable` (facultatif) reçoit le nombre de fichiers chargés mais illisibles (comptés pour 0).
std::uint64_t estimateInstrumentBytes (const InstrumentDescription&, FileInfoCache&, int* unreadable = nullptr);

// Tampons circulaires du streaming disque (fixes, par instance du plugin dès qu'un instrument est streamé).
constexpr std::uint64_t streamBufferBytes() noexcept
{
    return static_cast<std::uint64_t> (DiskStreamer::kSlots) * 2u * static_cast<std::uint64_t> (DiskStreamer::kRingFrames) * sizeof (float);
}

// RAM physique disponible maintenant, en octets ; 0 si la plateforme ne sait pas la donner.
// Linux : MemAvailable de /proc/meminfo ; Windows : GlobalMemoryStatusEx (ullAvailPhys).
std::uint64_t availablePhysicalBytes();

enum class Verdict { Ok, Warn, Refuse };

struct Limits
{
    std::uint64_t warnBytes = 0, refuseBytes = 0;
    bool fromAvailable = false; // false : valeurs fixes de repli (RAM disponible inconnue)
};

constexpr double warnFraction = 0.25;    // avertissement au-delà de 25 % de la RAM disponible
constexpr double refuseFraction = 0.50;  // refus au-delà de 50 %
constexpr std::uint64_t fallbackWarnBytes = 1000ull * 1024 * 1024;
constexpr std::uint64_t fallbackRefuseBytes = 3000ull * 1024 * 1024;

// `headroomBytes` = RAM disponible + ce que l'instrument actuel occupe déjà (il est soit conservé, soit remplacé :
// dans les deux cas cette mémoire n'est plus « disponible » mais appartient à l'instrument). 0 = inconnu.
Limits limitsFor (std::uint64_t headroomBytes);
Verdict judge (std::uint64_t instrumentBytes, const Limits&);
} // namespace neptune::core::memory
