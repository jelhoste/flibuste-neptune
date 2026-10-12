#pragma once
// Remise d'un Instrument du thread qui le construit vers le thread audio, sans verrou ni allocation côté audio,
// et sans que le thread audio ne libère jamais de mémoire : l'ancien instrument est rendu au thread de message
// (collect()) qui le détruit.
#include <atomic>
#include <array>
#include <memory>

#include "neptune/core/Instrument.h"

namespace neptune::core
{
class InstrumentSlot
{
public:
    InstrumentSlot() = default;
    ~InstrumentSlot();
    InstrumentSlot (const InstrumentSlot&) = delete;
    InstrumentSlot& operator= (const InstrumentSlot&) = delete;

    // --- thread de message (un seul producteur à la fois) ---
    void publish (std::shared_ptr<const Instrument> instrument);
    void collect(); // détruit les instruments rendus par le thread audio

    // --- thread audio ---
    // Retourne true si un nouvel instrument vient d'être adopté ; current() le désigne alors.
    bool adoptPending() noexcept;
    const Instrument* current() const noexcept { return active_ ? active_->instrument.get() : nullptr; }

private:
    struct Holder { std::shared_ptr<const Instrument> instrument; };
    static constexpr std::size_t kRingSize = 16;

    std::atomic<Holder*> pending_ { nullptr };
    Holder* active_ = nullptr; // propriété du thread audio

    std::array<std::atomic<Holder*>, kRingSize> retired_ {};
    std::atomic<std::size_t> retireHead_ { 0 }; // écrit par le thread audio
    std::atomic<std::size_t> retireTail_ { 0 }; // écrit par le thread de message
};
} // namespace neptune::core
