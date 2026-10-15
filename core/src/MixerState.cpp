#include "neptune/core/MixerState.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <nlohmann/json.hpp>

namespace neptune::core
{
namespace
{
using nlohmann::json;

json insertsToJson (const std::array<mixer::InsertState, mixer::numInserts>& inserts)
{
    json arr = json::array();
    for (const auto& in : inserts)
    {
        json o = json::object();
        if (! in.empty())
        {
            o["type"] = in.type;
            json values = json::object();
            for (const auto& [id, v] : in.values) values[std::to_string (id)] = v;
            o["values"] = std::move (values);
        }
        arr.push_back (std::move (o));
    }
    return arr;
}

bool anyInsert (const std::array<mixer::InsertState, mixer::numInserts>& inserts)
{
    for (const auto& in : inserts)
        if (! in.empty()) return true;
    return false;
}

void insertsFromJson (const json& o, std::array<mixer::InsertState, mixer::numInserts>& out)
{
    const auto it = o.find ("inserts");
    if (it == o.end() || ! it->is_array()) return;
    for (std::size_t i = 0; i < it->size() && i < out.size(); ++i)
    {
        const auto& e = (*it)[i];
        if (! e.is_object()) continue;
        const auto t = e.find ("type");
        if (t == e.end() || ! t->is_string() || t->get<std::string>().empty() || t->get<std::string>().size() > 32) continue;
        mixer::InsertState st;
        st.type = t->get<std::string>();
        if (const auto v = e.find ("values"); v != e.end() && v->is_object())
            for (const auto& [key, val] : v->items())
            {
                if (! val.is_number() || ! std::isfinite (val.get<double>())) continue;
                const long long id = std::strtoll (key.c_str(), nullptr, 10);
                if (id > 0 && id < 1000000) st.values[static_cast<ParamId> (id)] = val.get<double>();
            }
        out[i] = std::move (st);
    }
}

json stripToJson (const mixer::StripSettings& s)
{
    json o;
    if (! s.name.empty()) o["name"] = s.name;
    if (s.color != 0) o["color"] = s.color;
    o["midi_channel"] = s.midiChannel;
    if (s.midiFilter != midi::FilterDescription()) o["midi_filter"] = json::parse (midi::toJson (s.midiFilter));
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
    if (anyInsert (s.inserts)) o["inserts"] = insertsToJson (s.inserts);
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
    if (const auto mf = o.find ("midi_filter"); mf != o.end() && mf->is_object()) s.midiFilter = midi::filterFromJson (mf->dump());
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
    insertsFromJson (o, s.inserts);
    return mixer::sanitisedStrip (std::move (s), isBus, index);
}
} // namespace

MixerState::MixerState()
{
    for (int t = 0; t < mixer::numTracks; ++t)
        tracks[static_cast<std::size_t> (t)].strip.midiChannel = t == 0 ? 0 : t + 1;
    tracks[0].active = true;
}

MixerState MixerState::freshProject()
{
    MixerState m;
    m.tracks[0].type = TrackType::Synth;
    return m;
}

int MixerState::activeCount() const noexcept
{
    int n = 0;
    for (const auto& t : tracks) n += t.active ? 1 : 0;
    return n;
}

int MixerState::firstInactive() const noexcept
{
    for (std::size_t t = 0; t < tracks.size(); ++t)
        if (! tracks[t].active) return static_cast<int> (t);
    return -1;
}

void MixerState::resetTrack (int track)
{
    if (track < 0 || track >= mixer::numTracks) return;
    const MixerState defaults;
    removeParallelsOf (track); // les pistes parallèles d'une piste retirée disparaissent avec elle
    tracks[static_cast<std::size_t> (track)] = defaults.tracks[static_cast<std::size_t> (track)];
    tracks[static_cast<std::size_t> (track)].active = false;
    fixSelection();
}

int MixerState::firstInactiveParallel() const noexcept
{
    for (std::size_t p = 0; p < parallels.size(); ++p)
        if (! parallels[p].active) return static_cast<int> (p);
    return -1;
}

std::vector<int> MixerState::parallelChildren (int parentId) const
{
    std::vector<int> r;
    for (std::size_t p = 0; p < parallels.size(); ++p)
        if (parallels[p].active && parallels[p].parent == parentId) r.push_back (static_cast<int> (p));
    return r;
}

std::vector<int> MixerState::removeParallelsOf (int parentId)
{
    std::vector<int> removed;
    for (const int child : parallelChildren (parentId))
    {
        auto sub = removeParallel (child);
        removed.insert (removed.end(), sub.begin(), sub.end());
    }
    return removed;
}

std::vector<int> MixerState::removeParallel (int p)
{
    std::vector<int> removed;
    if (p < 0 || p >= mixer::numParallels || ! parallels[static_cast<std::size_t> (p)].active) return removed;
    removed = removeParallelsOf (mixer::numTracks + p);
    parallels[static_cast<std::size_t> (p)] = mixer::ParallelSettings {};
    removed.push_back (p);
    return removed;
}

int MixerState::parallelDepth (int p) const noexcept
{
    int depth = 0;
    int cur = p;
    for (int guard = 0; guard <= mixer::numParallels; ++guard)
    {
        if (cur < 0 || cur >= mixer::numParallels || ! parallels[static_cast<std::size_t> (cur)].active) return 0;
        ++depth;
        const int parent = parallels[static_cast<std::size_t> (cur)].parent;
        if (parent < mixer::numTracks) return tracks[static_cast<std::size_t> (parent)].active ? depth : 0;
        cur = parent - mixer::numTracks;
    }
    return 0; // boucle
}

void MixerState::fixParallels()
{
    for (std::size_t p = 0; p < parallels.size(); ++p)
        if (parallels[p].active && parallelDepth (static_cast<int> (p)) == 0) parallels[p] = mixer::ParallelSettings {};
}

void MixerState::fixSelection() noexcept
{
    selectedTrack = std::clamp (selectedTrack, 0, mixer::numTracks - 1);
    if (tracks[static_cast<std::size_t> (selectedTrack)].active) return;
    for (std::size_t t = 0; t < tracks.size(); ++t)
        if (tracks[t].active) { selectedTrack = static_cast<int> (t); return; }
    tracks[0].active = true; // jamais de mixeur sans piste
    selectedTrack = 0;
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
            o["active"] = m.tracks[t].active; // toujours écrit : sa valeur par défaut dépend de la piste (v5 : déduite du contenu)
            if (m.tracks[t].type != TrackType::Sampler) o["type"] = toString (m.tracks[t].type);
            if (! m.tracks[t].instrument.empty() || ! (m.tracks[t].instrument == defaults.tracks[t].instrument))
                o["instrument"] = json::parse (toJson (m.tracks[t].instrument));
            if (! (m.tracks[t].synth == defaults.tracks[t].synth))
                o["synth"] = json::parse (toJson (m.tracks[t].synth));
            if (! (m.tracks[t].fm == defaults.tracks[t].fm)) o["fm"] = json::parse (fm::toJson (m.tracks[t].fm));
            if (! (m.tracks[t].organ == defaults.tracks[t].organ)) o["organ"] = json::parse (organ::toJson (m.tracks[t].organ));
            if (! m.tracks[t].synthPresetId.empty()) o["synth_preset"] = m.tracks[t].synthPresetId;
        }
        tracks.push_back (std::move (o));
    }
    doc["tracks"] = std::move (tracks);
    {
        json parallels = json::array();
        bool any = false;
        for (const auto& p : m.parallels)
        {
            json o = json::object();
            if (p.active)
            {
                any = true;
                o = stripToJson (p.strip);
                o["active"] = true;
                o["parent"] = p.parent;
                o["tap"] = static_cast<int> (p.tap);
                if (p.followParentVolume) o["follow_parent_volume"] = true;
            }
            parallels.push_back (std::move (o));
        }
        if (any) doc["parallels"] = std::move (parallels);
    }
    json buses = json::array();
    for (std::size_t b = 0; b < m.buses.size(); ++b)
        buses.push_back (m.buses[b] == defaults.buses[b] ? json::object() : stripToJson (m.buses[b]));
    doc["buses"] = std::move (buses);
    json master;
    master["output"] = m.master.output;
    if (m.master.panMode != mixer::PanMode::Balance) master["pan_mode"] = "true";
    if (m.master.maxVoices != mixer::MasterSettings::defaultMaxVoices) master["max_voices"] = m.master.maxVoices;
    if (anyInsert (m.master.inserts)) master["inserts"] = insertsToJson (m.master.inserts);
    doc["master"] = std::move (master);
    return doc.dump();
}

MixerState mixerFromJson (std::string_view text)
{
    MixerState m;
    const MixerState defaults;
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return m;
    m.selectedTrack = std::clamp (intOf (doc, "selected_track", 0), 0, mixer::numTracks - 1);
    if (const auto it = doc.find ("tracks"); it != doc.end() && it->is_array())
        for (std::size_t t = 0; t < it->size() && t < m.tracks.size(); ++t)
        {
            const auto& o = (*it)[t];
            if (! o.is_object()) continue;
            auto& tr = m.tracks[t];
            tr.strip = stripFromJson (o, tr.strip, false, static_cast<int> (t));
            if (const auto it = o.find ("type"); it != o.end() && it->is_string()) trackTypeFromString (it->get<std::string>(), tr.type);
            if (const auto ins = o.find ("instrument"); ins != o.end() && ins->is_object())
                tr.instrument = instrumentFromJson (ins->dump());
            if (const auto sy = o.find ("synth"); sy != o.end() && sy->is_object())
                tr.synth = synthFromJson (sy->dump());
            if (const auto fx = o.find ("fm"); fx != o.end() && fx->is_object())
                tr.fm = fm::fmFromJson (fx->dump());
            if (const auto og = o.find ("organ"); og != o.end() && og->is_object())
                tr.organ = organ::organFromJson (og->dump());
            if (const auto sp = o.find ("synth_preset"); sp != o.end() && sp->is_string() && sp->get<std::string>().size() <= 64)
                tr.synthPresetId = sp->get<std::string>();
            // « active » absent (projets v5, antérieurs aux pistes actives) : la piste 1 et toute piste qui a du contenu
            // (instrument, nom, routage...) sont actives, les autres sont masquées.
            if (const auto it = o.find ("active"); it != o.end() && it->is_boolean()) tr.active = it->get<bool>();
            else
            {
                const TrackState& def = defaults.tracks[t];
                tr.active = t == 0 || ! tr.instrument.empty() || ! (tr.strip == def.strip) || tr.type != TrackType::Sampler;
            }
        }
    if (const auto it = doc.find ("parallels"); it != doc.end() && it->is_array())
        for (std::size_t p = 0; p < it->size() && p < m.parallels.size(); ++p)
        {
            const auto& o = (*it)[p];
            if (! o.is_object() || ! boolOf (o, "active", false)) continue;
            auto& ps = m.parallels[p];
            ps.active = true;
            ps.parent = intOf (o, "parent", 0);
            ps.tap = static_cast<mixer::TapPoint> (intOf (o, "tap", static_cast<int> (ps.tap)));
            ps.followParentVolume = boolOf (o, "follow_parent_volume", false);
            ps.strip = stripFromJson (o, ps.strip, false, static_cast<int> (p));
            ps = mixer::sanitised (std::move (ps), static_cast<int> (p));
        }
    if (const auto it = doc.find ("buses"); it != doc.end() && it->is_array())
        for (std::size_t b = 0; b < it->size() && b < m.buses.size(); ++b)
            m.buses[b] = stripFromJson ((*it)[b], m.buses[b], true, static_cast<int> (b));
    if (const auto mm = doc.find ("master"); mm != doc.end() && mm->is_object())
    {
        m.master.output = intOf (*mm, "output", m.master.output);
        m.master.maxVoices = intOf (*mm, "max_voices", m.master.maxVoices);
        if (const auto p = mm->find ("pan_mode"); p != mm->end() && p->is_string() && p->get<std::string>() == "true")
            m.master.panMode = mixer::PanMode::True;
        insertsFromJson (*mm, m.master.inserts);
        m.master = mixer::sanitised (m.master);
    }
    m.fixParallels();
    m.fixSelection();
    return m;
}
} // namespace neptune::core
