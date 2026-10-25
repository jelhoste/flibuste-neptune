#include "neptune/core/TrackExport.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "neptune/core/FacadeTransfer.h"

namespace neptune::core::trackexport
{
using json = nlohmann::json;

namespace
{
constexpr int kFormat = 1;
constexpr std::size_t kMaxParams = 4096;
} // namespace

std::string toJson (const Bundle& b)
{
    MixerState m;
    const int track = std::clamp (b.track, 0, mixer::numTracks - 1);
    m.tracks[static_cast<std::size_t> (track)] = b.state;
    m.tracks[static_cast<std::size_t> (track)].active = true;
    const auto mixerDoc = json::parse (neptune::core::toJson (m), nullptr, false);

    json doc;
    doc["format"] = kFormat;
    doc["track"] = track;
    doc["state"] = mixerDoc["tracks"][static_cast<std::size_t> (track)];
    json params = json::object();
    for (const auto& [id, value] : b.params) params[std::to_string (id)] = value;
    doc["params"] = std::move (params);
    return doc.dump (2);
}

std::optional<Bundle> fromJson (std::string_view text)
{
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (! doc.is_object() || ! doc.contains ("state") || ! doc["state"].is_object()) return std::nullopt;
    Bundle b;
    if (const auto it = doc.find ("track"); it != doc.end() && it->is_number_integer())
        b.track = std::clamp (it->get<int> (), 0, mixer::numTracks - 1);
    json tracks = json::array();
    for (int t = 0; t < b.track; ++t) tracks.push_back (json::object());
    tracks.push_back (doc["state"]);
    json wrapper;
    wrapper["tracks"] = std::move (tracks);
    b.state = mixerFromJson (wrapper.dump()).tracks[static_cast<std::size_t> (b.track)];
    b.state.active = true;
    if (const auto it = doc.find ("params"); it != doc.end() && it->is_object())
        for (const auto& [key, value] : it->items())
        {
            if (b.params.size() >= kMaxParams) break;
            if (! value.is_number() || key.empty() || key.size() > 10 || key.find_first_not_of ("0123456789") != std::string::npos) continue;
            const double v = value.get<double>();
            if (v == v) b.params[static_cast<ParamId> (std::stoul (key))] = v;
        }
    return b;
}

std::vector<ParamId> ownedParams (const ParameterSet& set, int track)
{
    std::vector<ParamId> out;
    if (track < 0 || track >= mixer::numTracks) return out;
    const int other = (track + 1) % mixer::numTracks;
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        const ParamId id = set.spec (i).id;
        if (facade::remapParamTrack (id, track, other) != id) out.push_back (id);
    }
    return out;
}

void remapTo (Bundle& b, int toTrack)
{
    toTrack = std::clamp (toTrack, 0, mixer::numTracks - 1);
    if (toTrack == b.track) return;
    std::map<ParamId, double> moved;
    for (const auto& [id, value] : b.params) moved[facade::remapParamTrack (id, b.track, toTrack)] = value;
    b.params = std::move (moved);
    for (auto& route : b.state.strip.modulation.routes)
        if (! modulation::isInsertDest (route.dest)) route.dest = facade::remapParamTrack (route.dest, b.track, toTrack);
    facade::remapTrack (b.state.strip.facade, b.track, toTrack);
    b.track = toTrack;
}

void forEachFilePath (TrackState& s, const std::function<void (std::string&)>& visit)
{
    for (auto& zone : s.instrument.zones)
        if (! zone.samplePath.empty()) visit (zone.samplePath);
    for (auto& insert : s.strip.inserts)
        if (insert.type == "convolution" && ! insert.text.empty()) visit (insert.text);
}
} // namespace neptune::core::trackexport
