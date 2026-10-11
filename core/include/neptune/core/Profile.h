#pragma once
// Profil de performance Live / Studio (palier 0) : le mécanisme de bascule, sans contenu tant qu'aucun traitement
// n'a de variante basse latence (spec-modules.md « profil de performance global partagé entre instances »).
//
//  - Un réglage global PAR PROCESSUS : toutes les instances du plugin chargées dans le même processus hôte le partagent
//    (variable statique du .clap ; pas besoin de mémoire partagée entre processus). Il est initialisé par la première
//    instance depuis settings.json, puis ne vit que le temps du processus : la bascule n'est jamais réécrite dans
//    settings.json (décision de l'utilisateur, 2026-10-11).
//  - Chaque instance peut se « verrouiller » sur un profil propre ; ce verrou, lui, est sauvegardé dans le projet.
//  - Toute la lecture est sans verrou (atomiques) : utilisable depuis le thread audio.
#include <atomic>
#include <optional>
#include <string_view>

namespace neptune::core
{
enum class PerformanceProfile { Live, Studio };

const char* toString (PerformanceProfile p) noexcept;                       // "live" / "studio"
std::optional<PerformanceProfile> profileFromString (std::string_view s);

class SharedProfile
{
public:
    // Le réglage du processus ; les tests créent leurs propres objets.
    static SharedProfile& process() noexcept;

    PerformanceProfile get() const noexcept { return static_cast<PerformanceProfile> (value_.load (std::memory_order_relaxed)); }
    void set (PerformanceProfile p) noexcept { value_.store (static_cast<int> (p), std::memory_order_relaxed); initialised_.store (true); }

    // Première instance du processus : adopte `initial`. Les suivantes ne changent rien. Retourne true si elle a initialisé.
    bool initialiseOnce (PerformanceProfile initial) noexcept;

private:
    std::atomic<int> value_ { static_cast<int> (PerformanceProfile::Live) };
    std::atomic<bool> initialised_ { false };
};

class InstanceProfile
{
public:
    explicit InstanceProfile (SharedProfile& shared = SharedProfile::process()) noexcept : shared_ (shared) {}

    // Profil à appliquer dans cette instance : son verrou s'il y en a un, sinon le réglage partagé.
    PerformanceProfile effective() const noexcept
    {
        const int l = lock_.load (std::memory_order_relaxed);
        return l >= 0 ? static_cast<PerformanceProfile> (l) : shared_.get();
    }
    std::optional<PerformanceProfile> locked() const noexcept
    {
        const int l = lock_.load (std::memory_order_relaxed);
        if (l < 0) return std::nullopt;
        return static_cast<PerformanceProfile> (l);
    }

    void lockTo (PerformanceProfile p) noexcept { lock_.store (static_cast<int> (p), std::memory_order_relaxed); }
    void lockToCurrent() noexcept { lockTo (effective()); }
    void unlock() noexcept { lock_.store (-1, std::memory_order_relaxed); }
    void setLocked (std::optional<PerformanceProfile> p) noexcept { p ? lockTo (*p) : unlock(); }

    SharedProfile& shared() noexcept { return shared_; }

private:
    SharedProfile& shared_;
    std::atomic<int> lock_ { -1 }; // -1 = non verrouillé
};
} // namespace neptune::core
