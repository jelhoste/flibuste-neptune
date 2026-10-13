#pragma once
// Remise d'un objet immuable (instrument, patch de synthé...) du thread qui le construit vers le thread audio, sans verrou
// ni allocation côté audio, et sans que le thread audio ne libère jamais de mémoire : l'ancien objet est rendu au thread de
// message (collect()) qui le détruit.
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>

namespace neptune::core
{
template <typename T>
class HandoffSlot
{
public:
    HandoffSlot() = default;
    ~HandoffSlot()
    {
        delete pending_.exchange (nullptr);
        delete active_;
        collect();
    }
    HandoffSlot (const HandoffSlot&) = delete;
    HandoffSlot& operator= (const HandoffSlot&) = delete;

    // --- thread de message (un seul producteur à la fois) ---
    void publish (std::shared_ptr<const T> object)
    {
        // Si l'audio n'a pas encore pris le précédent, c'est nous qui le détruisons : il ne l'a jamais vu.
        delete pending_.exchange (new Holder { std::move (object) });
    }

    void collect() // détruit les objets rendus par le thread audio
    {
        auto tail = retireTail_.load (std::memory_order_relaxed);
        const auto head = retireHead_.load (std::memory_order_acquire);
        while (tail != head)
        {
            delete retired_[tail % kRingSize].load (std::memory_order_relaxed);
            ++tail;
        }
        retireTail_.store (tail, std::memory_order_release);
    }

    // --- thread audio ---
    // Retourne true si un nouvel objet vient d'être adopté ; current() le désigne alors.
    bool adoptPending() noexcept
    {
        // Pas de place pour rendre l'ancien : on attend le prochain bloc (le thread de message collecte).
        const auto head = retireHead_.load (std::memory_order_relaxed);
        if (active_ != nullptr && head - retireTail_.load (std::memory_order_acquire) >= kRingSize) return false;

        Holder* fresh = pending_.exchange (nullptr, std::memory_order_acq_rel);
        if (fresh == nullptr) return false;

        if (active_ != nullptr)
        {
            retired_[head % kRingSize].store (active_, std::memory_order_relaxed);
            retireHead_.store (head + 1, std::memory_order_release);
        }
        active_ = fresh;
        return true;
    }
    const T* current() const noexcept { return active_ ? active_->object.get() : nullptr; }

private:
    struct Holder { std::shared_ptr<const T> object; };
    static constexpr std::size_t kRingSize = 16;

    std::atomic<Holder*> pending_ { nullptr };
    Holder* active_ = nullptr; // propriété du thread audio

    std::array<std::atomic<Holder*>, kRingSize> retired_ {};
    std::atomic<std::size_t> retireHead_ { 0 }; // écrit par le thread audio
    std::atomic<std::size_t> retireTail_ { 0 }; // écrit par le thread de message
};
} // namespace neptune::core
