#include "neptune/core/Synth.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <nlohmann/json.hpp>

#include "neptune/core/DescriptionJson.h"

namespace neptune::core
{
const char* toString (TrackType t) noexcept { return t == TrackType::AudioIn ? "audio_in" : t == TrackType::Organ ? "organ" : t == TrackType::Fm ? "fm" : t == TrackType::Synth ? "synth" : "sampler"; }

bool trackTypeFromString (std::string_view s, TrackType& out)
{
    if (s == "sampler") { out = TrackType::Sampler; return true; }
    if (s == "synth") { out = TrackType::Synth; return true; }
    if (s == "fm") { out = TrackType::Fm; return true; }
    if (s == "organ") { out = TrackType::Organ; return true; }
    if (s == "audio_in") { out = TrackType::AudioIn; return true; }
    return false;
}

const char* toString (DetuneCurve c) noexcept { return c == DetuneCurve::Supersaw ? "supersaw" : "linear"; }
const char* toString (SubWave w) noexcept { return w == SubWave::Triangle ? "triangle" : w == SubWave::Square ? "square" : "sine"; }
const char* toString (NoiseType n) noexcept { return n == NoiseType::Pink ? "pink" : "white"; }

namespace
{
nlohmann::json filterBlockJson (const FilterBlock& b)
{
    nlohmann::json o;
    o["filter1"] = detail::filterJson (b.filter[0]);
    o["filter2"] = detail::filterJson (b.filter[1]);
    o["parallel"] = b.parallel;
    o["balance"] = b.balance;
    return o;
}

FilterBlock filterBlockFrom (const nlohmann::json& o, FilterBlock b)
{
    if (! o.is_object()) return b;
    if (const auto it = o.find ("filter1"); it != o.end() && it->is_object()) b.filter[0] = detail::filterFrom (*it);
    if (const auto it = o.find ("filter2"); it != o.end() && it->is_object()) b.filter[1] = detail::filterFrom (*it);
    if (const auto it = o.find ("parallel"); it != o.end() && it->is_boolean()) b.parallel = it->get<bool>();
    b.balance = detail::jsonNum (o, "balance", b.balance);
    return b;
}

bool boolOf (const nlohmann::json& o, const char* k, bool def)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_boolean() ? it->get<bool>() : def;
}

std::string strOf (const nlohmann::json& o, const char* k)
{
    const auto it = o.find (k);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}
} // namespace

SynthDescription sanitised (SynthDescription d)
{
    auto fin = [] (double v, double def) { return std::isfinite (v) ? v : def; };
    for (auto& o : d.osc)
    {
        o.wave = std::clamp (o.wave, 0, static_cast<int> (kNumWaves) - 1);
        o.octave = std::clamp (o.octave, SynthDescription::minOctave, SynthDescription::maxOctave);
        o.semitone = std::clamp (o.semitone, -12, 12);
        o.unison = std::clamp (o.unison, 1, maxUnison);
        o.spread = std::clamp (fin (o.spread, 0.5), 0.0, 1.0);
        o.startPhase = std::clamp (fin (o.startPhase, 0.0), 0.0, 1.0);
    }
    d.osc[0].sync = false; // l'oscillateur 1 est le maître
    d.subOctave = d.subOctave <= -2 ? -2 : -1;
    for (auto& m : d.mix) m.pan = std::clamp (fin (m.pan, 0.0), -1.0, 1.0);
    d.ampEnv = sanitised (std::move (d.ampEnv));
    d.pitchEnv = sanitised (std::move (d.pitchEnv));
    d.filters = sanitised (std::move (d.filters));
    d.maxVoices = std::clamp (d.maxVoices, 1, SynthDescription::maxVoicesLimit);
    d.glideMs = std::clamp (d.glideMs, 0, SynthDescription::maxGlideMs);
    d.bendRange = std::clamp (d.bendRange, 0, SynthDescription::maxBendRange);
    return d;
}

std::string toJson (const SynthDescription& d)
{
    nlohmann::json o;
    nlohmann::json oscs = nlohmann::json::array();
    for (const auto& s : d.osc)
    {
        nlohmann::json e;
        e["wave"] = std::string (waveInfo (s.wave).key);
        e["octave"] = s.octave;
        e["semitone"] = s.semitone;
        e["unison"] = s.unison;
        e["spread"] = s.spread;
        e["curve"] = toString (s.curve);
        e["retrigger"] = s.retrigger;
        e["start_phase"] = s.startPhase;
        e["sync"] = s.sync;
        oscs.push_back (std::move (e));
    }
    o["oscillators"] = std::move (oscs);
    o["sub_wave"] = toString (d.subWave);
    o["sub_octave"] = d.subOctave;
    o["noise"] = toString (d.noise);
    nlohmann::json mix = nlohmann::json::array();
    for (const auto& m : d.mix) mix.push_back ({ { "mute", m.mute }, { "pan", m.pan } });
    o["mix"] = std::move (mix);
    o["amp_envelope"] = detail::envelopeJson (d.ampEnv);
    o["pitch_envelope"] = detail::envelopeJson (d.pitchEnv);
    o["filters"] = filterBlockJson (d.filters);
    o["polyphony"] = toString (d.polyphony);
    o["max_voices"] = d.maxVoices;
    o["voice_steal"] = toString (d.stealMode);
    o["glide_ms"] = d.glideMs;
    if (d.bendRange != SynthDescription::defaultBendRange) o["bend_range"] = d.bendRange;   // absent tant qu'il vaut 2 : les fichiers d'avant se relisent et se réécrivent à l'identique
    return o.dump();
}

SynthDescription synthFromJson (std::string_view text)
{
    SynthDescription d;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;

    if (const auto it = doc.find ("oscillators"); it != doc.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < d.osc.size(); ++i)
        {
            const auto& e = (*it)[i];
            if (! e.is_object()) continue;
            auto& s = d.osc[i];
            if (const int w = waveIdFromKey (strOf (e, "wave")); w >= 0) s.wave = w;
            s.octave = detail::jsonInt (e, "octave", s.octave);
            s.semitone = detail::jsonInt (e, "semitone", s.semitone);
            s.unison = detail::jsonInt (e, "unison", s.unison);
            s.spread = detail::jsonNum (e, "spread", s.spread);
            s.curve = strOf (e, "curve") == "supersaw" ? DetuneCurve::Supersaw : DetuneCurve::Linear;
            s.retrigger = boolOf (e, "retrigger", s.retrigger);
            s.startPhase = detail::jsonNum (e, "start_phase", s.startPhase);
            s.sync = boolOf (e, "sync", s.sync);
        }
    if (const auto v = strOf (doc, "sub_wave"); v == "triangle") d.subWave = SubWave::Triangle;
    else if (v == "square") d.subWave = SubWave::Square;
    d.subOctave = detail::jsonInt (doc, "sub_octave", d.subOctave);
    d.noise = strOf (doc, "noise") == "pink" ? NoiseType::Pink : NoiseType::White;
    if (const auto it = doc.find ("mix"); it != doc.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < d.mix.size(); ++i)
        {
            const auto& e = (*it)[i];
            if (! e.is_object()) continue;
            d.mix[i].mute = boolOf (e, "mute", d.mix[i].mute);
            d.mix[i].pan = detail::jsonNum (e, "pan", d.mix[i].pan);
        }
    if (const auto it = doc.find ("amp_envelope"); it != doc.end() && it->is_object())
        d.ampEnv = detail::envelopeFrom (nlohmann::json { { "envelope", *it } });
    if (const auto it = doc.find ("pitch_envelope"); it != doc.end() && it->is_object())
        d.pitchEnv = detail::envelopeFrom (nlohmann::json { { "envelope", *it } });
    if (const auto it = doc.find ("filters"); it != doc.end()) d.filters = filterBlockFrom (*it, d.filters);
    if (const auto v = strOf (doc, "polyphony"); v == "mono") d.polyphony = PolyphonyMode::Mono;
    else if (v == "legato") d.polyphony = PolyphonyMode::Legato;
    d.maxVoices = detail::jsonInt (doc, "max_voices", d.maxVoices);
    if (const auto v = strOf (doc, "voice_steal"); v == "quietest") d.stealMode = VoiceStealMode::Quietest;
    else if (v == "keep_low") d.stealMode = VoiceStealMode::KeepLow;
    else if (v == "keep_high") d.stealMode = VoiceStealMode::KeepHigh;
    d.glideMs = detail::jsonInt (doc, "glide_ms", d.glideMs);
    d.bendRange = detail::jsonInt (doc, "bend_range", d.bendRange);
    return sanitised (std::move (d));
}

namespace
{
std::atomic<std::uint64_t> gPatchGeneration { 0 };
}

SynthPatch::SynthPatch (const SynthDescription& d)
    : desc (sanitised (d)), ampEnv (desc.ampEnv), pitchEnv (desc.pitchEnv),
      filterEnv { EnvelopeRuntime (desc.filters.filter[0].env), EnvelopeRuntime (desc.filters.filter[1].env) }
{
    generation = ++gPatchGeneration;
    WaveTables::instance(); // construit les tables d'onde ici (thread de message), jamais sur le thread audio
}
} // namespace neptune::core
