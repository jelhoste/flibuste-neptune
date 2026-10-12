#pragma once
// Commande d'annulation pour les modifications de l'instrument (zones, réglages de chargement) qui ne garde que ce qui a
// changé : un glissé sur quelques zones d'un piano de plusieurs milliers n'en copie pas la description entière à chaque
// entrée d'historique. Une modification qui change le nombre de zones garde une copie complète (rare : import, suppression).
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "neptune/core/Instrument.h"
#include "neptune/core/Undo.h"

namespace neptune::core
{
class InstrumentEditCommand : public Command
{
public:
    using Getter = std::function<InstrumentDescription()>;
    using Setter = std::function<void (const InstrumentDescription&)>;

    // `oldValue` -> `newValue`. `get` rend la description courante, `set` la remplace (hors historique).
    InstrumentEditCommand (const InstrumentDescription& oldValue, const InstrumentDescription& newValue, Getter get, Setter set,
                           std::string name, std::string mergeKey = {});

    void redo() override;
    void undo() override;
    std::string name() const override { return name_; }
    bool mergeWith (const Command& next) override;

    bool isFull() const noexcept { return full_; }
    std::size_t changedZoneCount() const noexcept { return changed_.size(); }

private:
    static InstrumentDescription header (const InstrumentDescription&); // tout sauf les zones
    void apply (bool forward);

    bool full_ = false;
    InstrumentDescription oldFull_, newFull_;                     // si le nombre de zones change
    std::map<std::size_t, std::pair<ZoneDescription, ZoneDescription>> changed_; // indice -> (avant, après)
    InstrumentDescription oldHead_, newHead_;                     // zones vides
    Getter get_;
    Setter set_;
    std::string name_, mergeKey_;
};
} // namespace neptune::core
