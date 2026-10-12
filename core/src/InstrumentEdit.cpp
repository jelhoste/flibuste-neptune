#include "neptune/core/InstrumentEdit.h"

namespace neptune::core
{
InstrumentDescription InstrumentEditCommand::header (const InstrumentDescription& d)
{
    InstrumentDescription h = d;
    h.zones.clear();
    return h;
}

InstrumentEditCommand::InstrumentEditCommand (const InstrumentDescription& oldValue, const InstrumentDescription& newValue, Getter get,
                                              Setter set, std::string name, std::string mergeKey)
    : get_ (std::move (get)), set_ (std::move (set)), name_ (std::move (name)), mergeKey_ (std::move (mergeKey))
{
    oldHead_ = header (oldValue);
    newHead_ = header (newValue);
    if (oldValue.zones.size() != newValue.zones.size())
    {
        full_ = true;
        oldFull_ = oldValue;
        newFull_ = newValue;
        return;
    }
    for (std::size_t i = 0; i < oldValue.zones.size(); ++i)
        if (! (oldValue.zones[i] == newValue.zones[i])) changed_.emplace (i, std::make_pair (oldValue.zones[i], newValue.zones[i]));
}

void InstrumentEditCommand::apply (bool forward)
{
    if (full_)
    {
        set_ (forward ? newFull_ : oldFull_);
        return;
    }
    InstrumentDescription d = get_();
    const auto& head = forward ? newHead_ : oldHead_;
    d.roundRobinMode = head.roundRobinMode;
    d.mainRoundRobinOnly = head.mainRoundRobinOnly;
    d.loadReleaseSamples = head.loadReleaseSamples;
    d.unloadedGroups = head.unloadedGroups;
    d.streamFromDisk = head.streamFromDisk;
    d.preloadKB = head.preloadKB;
    for (const auto& [index, pair] : changed_)
        if (index < d.zones.size()) d.zones[index] = forward ? pair.second : pair.first;
    set_ (d);
}

void InstrumentEditCommand::redo() { apply (true); }
void InstrumentEditCommand::undo() { apply (false); }

bool InstrumentEditCommand::mergeWith (const Command& next)
{
    const auto* o = dynamic_cast<const InstrumentEditCommand*> (&next);
    if (o == nullptr || mergeKey_.empty() || o->mergeKey_ != mergeKey_ || o->full_) return false;

    if (full_)
    {
        // « copies puis déplacement » dans un même geste : le déplacement s'applique à la copie complète d'arrivée.
        for (const auto& [index, pair] : o->changed_)
            if (index < newFull_.zones.size()) newFull_.zones[index] = pair.second;
        newFull_.roundRobinMode = o->newHead_.roundRobinMode;
        newFull_.mainRoundRobinOnly = o->newHead_.mainRoundRobinOnly;
        newFull_.loadReleaseSamples = o->newHead_.loadReleaseSamples;
        newFull_.unloadedGroups = o->newHead_.unloadedGroups;
        newFull_.streamFromDisk = o->newHead_.streamFromDisk;
        newFull_.preloadKB = o->newHead_.preloadKB;
        newHead_ = o->newHead_;
        return true;
    }
    for (const auto& [index, pair] : o->changed_)
    {
        auto it = changed_.find (index);
        if (it == changed_.end()) changed_.emplace (index, pair);
        else it->second.second = pair.second;
    }
    newHead_ = o->newHead_;
    return true;
}
} // namespace neptune::core
