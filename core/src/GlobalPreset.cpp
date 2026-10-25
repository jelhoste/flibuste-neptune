#include "neptune/core/GlobalPreset.h"

#include <nlohmann/json.hpp>

namespace neptune::core::globalpreset
{
using json = nlohmann::json;

std::string withoutInstanceData (std::string_view stateText)
{
    auto doc = json::parse (stateText.begin(), stateText.end(), nullptr, false);
    if (! doc.is_object()) return std::string (stateText);
    doc.erase ("instance_id");
    doc.erase ("profile_lock");
    doc.erase ("control");
    return doc.dump();
}

std::string editMixer (std::string_view stateText, const std::function<void (MixerState&)>& edit)
{
    auto doc = json::parse (stateText.begin(), stateText.end(), nullptr, false);
    if (! doc.is_object()) return std::string (stateText);
    const auto it = doc.find ("mixer");
    if (it == doc.end() || ! it->is_object()) return std::string (stateText);
    auto mixer = mixerFromJson (it->dump());
    edit (mixer);
    *it = json::parse (toJson (mixer));
    return doc.dump();
}

void forEachFacade (MixerState& m, const std::function<void (facade::Facade&)>& visit)
{
    for (auto& t : m.tracks) visit (t.strip.facade);
}
} // namespace neptune::core::globalpreset
