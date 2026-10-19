#pragma once
// Clé synthétique du compresseur pilotée par des notes MIDI (source « MIDI » du sidechain, plugin séparé).
// Le niveau de la clé est fonction de la vélocité de la note la plus forte encore tenue (sur le canal choisi) : v' = v^k avec v = vélocité / 127 et
// k = 1 + 3 s si s >= 0, k = 1 / (1 - 3 s) si s < 0 (s = forme / 100) ; 0 dBFS à la vélocité 127. Le niveau est constant pendant la note et tombe à zéro
// au relâchement de la dernière note tenue : le relâchement du compresseur fait la rampe. Le signal est continu (constant), il ne passe jamais par les
// filtres de clé. Aucune allocation ; tout se passe sur le fil audio (les atomiques ne servent qu'à l'affichage).
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace neptune::core
{
class MidiKey
{
public:
    // Niveau (amplitude 0..1) pour une vélocité 0..127 et une forme -100..+100 %.
    static float levelFor (int velocity, double shapePercent) noexcept
    {
        if (velocity <= 0) return 0.0f;
        const double s = std::clamp (shapePercent, -100.0, 100.0) * 0.01;
        const double k = s >= 0.0 ? 1.0 + 3.0 * s : 1.0 / (1.0 - 3.0 * s);
        return static_cast<float> (std::pow (std::min (velocity, 127) / 127.0, k));
    }

    // Message MIDI du bloc courant, décalé de `offset` échantillons. Appelé par l'adaptateur (même fil que render).
    void push (int offset, std::uint8_t status, std::uint8_t data1, std::uint8_t data2) noexcept
    {
        const Event e { offset, status, data1, data2 };
        if (count_ < queue_.size()) queue_[count_++] = e;
        else
        {
            // file pleine (rafale improbable) : on applique la file puis l'événement, dans l'ordre, plutôt que de perdre un relâchement
            // (la précision au sample de ces messages est perdue)
            beginBlock();
            apply (e);
        }
    }

    // À appeler avant de pousser les messages d'un bloc : les messages d'un bloc que le module n'a pas rendu (contourné) sont appliqués sans rendu.
    void beginBlock() noexcept
    {
        for (std::size_t i = 0; i < count_; ++i) apply (queue_[i]);
        count_ = 0;
    }

    // Remplit `out` (n échantillons) avec la clé. `channel` : 0 = tous, 1..16. `shapePercent` : -100..+100.
    void render (float* out, int n, int channel, double shapePercent) noexcept
    {
        channel_ = channel;
        shape_ = shapePercent;
        int pos = 0;
        float cur = level();
        for (std::size_t i = 0; i < count_; ++i)
        {
            const int at = std::clamp (queue_[i].offset, pos, n);
            std::fill (out + pos, out + at, cur);
            pos = at;
            apply (queue_[i]);
            cur = level();
        }
        count_ = 0;
        std::fill (out + pos, out + n, cur);
        shown_.store (cur, std::memory_order_relaxed);
        held_.store (anyHeld(), std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        for (auto& c : velocity_) c.fill (0);
        count_ = 0;
        shown_.store (0.0f, std::memory_order_relaxed);
        held_.store (false, std::memory_order_relaxed);
    }

    // Pour l'affichage (n'importe quel fil) : niveau courant de la clé et présence d'une note tenue.
    float shownLevel() const noexcept { return shown_.load (std::memory_order_relaxed); }
    bool noteHeld() const noexcept { return held_.load (std::memory_order_relaxed); }

private:
    struct Event
    {
        int offset;
        std::uint8_t status, data1, data2;
    };

    void apply (const Event& e) noexcept
    {
        const int ch = e.status & 0x0F;
        const int type = e.status & 0xF0;
        if (type == 0x90 && e.data2 > 0) velocity_[static_cast<std::size_t> (ch)][e.data1 & 0x7F] = e.data2 & 0x7F;
        else if (type == 0x80 || type == 0x90) velocity_[static_cast<std::size_t> (ch)][e.data1 & 0x7F] = 0;
        else if (type == 0xB0 && (e.data1 == 123 || e.data1 == 120)) velocity_[static_cast<std::size_t> (ch)].fill (0);   // All notes off, all sound off
    }
    int strongest() const noexcept
    {
        int best = 0;
        for (int ch = 0; ch < 16; ++ch)
        {
            if (channel_ > 0 && ch != channel_ - 1) continue;
            for (const auto v : velocity_[static_cast<std::size_t> (ch)]) best = std::max (best, static_cast<int> (v));
        }
        return best;
    }
    bool anyHeld() const noexcept { return strongest() > 0; }
    float level() const noexcept { return levelFor (strongest(), shape_); }

    std::array<std::array<std::uint8_t, 128>, 16> velocity_ {};   // vélocité de chaque note tenue (0 = relâchée)
    std::array<Event, 512> queue_ {};
    std::size_t count_ = 0;
    int channel_ = 0;
    double shape_ = 0.0;
    std::atomic<float> shown_ { 0.0f };
    std::atomic<bool> held_ { false };
};
} // namespace neptune::core
