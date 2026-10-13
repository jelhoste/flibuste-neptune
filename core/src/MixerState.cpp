#include "neptune/core/MixerState.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace neptune::core
{
namespace
{
using nlohmann::json;

json stripToJson (const mixer::StripSettings& s)
{
    json o;
    if (! s.name.empty()) o["name"] = s.name;
    if (s.color != 0) o["color"] = s.color;
    o["midi_channel"] = s.midiChannel;
    json r;
    r["main_dest"] = s.routing.mainDest;
    r["main_enabled"] = s.routing.mainEnabled;
    r["direct_out"] = s.routing.directOut;
    o["routing"] = std::move (r);
    if (s.panMode != mixer::PanMode::Balance) o["pan_mode"] = "true";
    if (s.slim) o["slim"] = true;
    json sends = json::array();
    for (const auto& x : s.sends)
    {
        json e;
        e["dest"] = x.destination;
        e["tap"] = static_cast<int> (x.tap);
        e["pan"] = x.pan;
        e["mute"] = x.mute;
        sends.push_back (std::move (e));
    }
    o["sends"] = std::move (sends);
    return o;
}

int intOf (const json& o, const char* key, int def)
{
    const auto it = o.find (key);
    return it != o.end() && it->is_number_integer() ? static_cast<int> (std::clamp<long long> (it->get<long long>(), -1000000, 1000000)) : def;
}

bool boolOf (const json& o, const char* key, bool def)
{
    const auto it = o.find (key);
    return it != o.end() && it->is_boolean() ? it->get<bool>() : def;
}

mixer::StripSettings stripFromJson (const json& o, mixer::StripSettings s, bool isBus, int index)
{
    if (! o.is_object()) return mixer::sanitisedStrip (std::move (s), isBus, index);
    if (const auto it = o.find ("name"); it != o.end() && it->is_string()) s.name = it->get<std::string>();
    s.color = intOf (o, "color", s.color);
    s.midiChannel = intOf (o, "midi_channel", s.midiChannel);
    if (const auto r = o.find ("routing"); r != o.end() && r->is_object())
    {
        s.routing.mainDest = intOf (*r, "main_dest", s.routing.mainDest);
        s.routing.mainEnabled = boolOf (*r, "main_enabled", s.routing.mainEnabled);
        s.routing.directOut = intOf (*r, "direct_out", s.routing.directOut);
    }
    if (const auto it = o.find ("pan_mode"); it != o.end() && it->is_string() && it->get<std::string>() == "true") s.panMode = mixer::PanMode::True;
    s.slim = boolOf (o, "slim", s.slim);
    if (const auto it = o.find ("sends"); it != o.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < s.sends.size(); ++i)
        {
            const auto& e = (*it)[i];
            if (! e.is_object()) continue;
            auto& x = s.sends[i];
            x.destination = intOf (e, "dest", x.destination);
            x.tap = static_cast<mixer::TapPoint> (intOf (e, "tap", static_cast<int> (x.tap)));
            if (const auto p = e.find ("pan"); p != e.end() && p->is_number()) x.pan = p->get<double>();
            x.mute = boolOf (e, "mute", x.mute);
        }
    return mixer::sanitisedStrip (std::move (s), isBus, index);
}
} // namespace

MixerState::MixerState()
{
    for (int t = 0; t < mixer::numTracks; ++t)
        tracks[static_cast<std::size_t> (t)].strip.midiChannel = t == 0 ? 0 : t + 1;
}

std::string toJson (const MixerState& m)
{
    const MixerState defaults;
    json doc;
    doc["selected_track"] = m.selectedTrack;
    json tracks = json::array();
    for (std::size_t t = 0; t < m.tracks.size(); ++t)
    {
        json o = json::object();
        if (! (m.tracks[t] == defaults.tracks[t]))
        {
            o = stripToJson (m.tracks[t].strip);
            if (! m.tracks[t].instrument.empty() || ! (m.tracks[t].instrument == defaults.tracks[t].instrument))
                o["instrument"] = json::parse (toJson (m.tracks[t].instrument));
        }
        tracks.push_back (std::move (o));
    }
    doc["tracks"] = std::move (tracks);
    json buses = json::array();
    for (std::size_t b = 0; b < m.buses.size(); ++b)
        buses.push_back (m.buses[b] == defaults.buses[b] ? json::object() : stripToJson (m.buses[b]));
    doc["buses"] = std::move (buses);
    json master;
    master["output"] = m.master.output;
    if (m.master.panMode != mixer::PanMode::Balance) master["pan_mode"] = "true";
    doc["master"] = std::move (master);
    return doc.dump();
}

MixerState mixerFromJson (std::string_view text)
{
    MixerState m;
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return m;
    m.selectedTrack = std::clamp (intOf (doc, "selected_track", 0), 0, mixer::numTracks - 1);
    if (const auto it = doc.find ("tracks"); it != doc.end() && it->is_array())
        for (std::size_t t = 0; t < it->size() && t < m.tracks.size(); ++t)
        {
            const auto& o = (*it)[t];
            if (! o.is_object()) continue;
            m.tracks[t].strip = stripFromJson (o, m.tracks[t].strip, false, static_cast<int> (t));
            if (const auto ins = o.find ("instrument"); ins != o.end() && ins->is_object())
                m.tracks[t].instrument = instrumentFromJson (ins->dump());
        }
    if (const auto it = doc.find ("buses"); it != doc.end() && it->is_array())
        for (std::size_t b = 0; b < it->size() && b < m.buses.size(); ++b)
            m.buses[b] = stripFromJson ((*it)[b], m.buses[b], true, static_cast<int> (b));
    if (const auto mm = doc.find ("master"); mm != doc.end() && mm->is_object())
    {
        m.master.output = intOf (*mm, "output", m.master.output);
        if (const auto p = mm->find ("pan_mode"); p != mm->end() && p->is_string() && p->get<std::string>() == "true")
            m.master.panMode = mixer::PanMode::True;
        m.master = mixer::sanitised (m.master);
    }
    return m;
}
} // namespace neptune::core
