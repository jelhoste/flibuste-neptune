#include "neptune/core/InstrumentSlot.h"

namespace neptune::core
{
InstrumentSlot::~InstrumentSlot()
{
    delete pending_.exchange (nullptr);
    delete active_;
    collect();
}

void InstrumentSlot::publish (std::shared_ptr<const Instrument> instrument)
{
    // Si l'audio n'a pas encore pris le précédent, c'est nous qui le détruisons : il ne l'a jamais vu.
    delete pending_.exchange (new Holder { std::move (instrument) });
}

void InstrumentSlot::collect()
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

bool InstrumentSlot::adoptPending() noexcept
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
} // namespace neptune::core
