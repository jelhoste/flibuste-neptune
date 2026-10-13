#include "neptune/core/Synth.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "neptune/core/DescriptionJson.h"

namespace neptune::core
{
const char* toString (TrackType t) noexcept { return t == TrackType::Synth ? "synth" : "sampler"; }

bool trackTypeFromString (std::string_view s, TrackType& out)
{
    if (s == "sampler") { out = TrackType::Sampler; return true; }
    if (s == "synth") { out = TrackType::Synth; return true; }
    return false;
}

const char* toString (Waveform w) noexcept
{
    switch (w)
    {
        case Waveform::Sine: return "sine";
        case Waveform::Triangle: return "triangle";
        case Waveform::Square: return "square";
        default: return "saw";
    }
}

bool waveformFromString (std::string_view s, Waveform& out)
{
    if (s == "sine") { out = Waveform::Sine; return true; }
    if (s == "triangle") { out = Waveform::Triangle; return true; }
    if (s == "saw") { out = Waveform::Saw; return true; }
    if (s == "square") { out = Waveform::Square; return true; }
    return false;
}

SynthDescription sanitised (SynthDescription d)
{
    if (static_cast<int> (d.waveform) < 0 || static_cast<int> (d.waveform) >= numWaveforms) d.waveform = Waveform::Saw;
    d.octave = std::clamp (d.octave, SynthDescription::minOctave, SynthDescription::maxOctave);
    d.ampEnv = sanitised (std::move (d.ampEnv));
    d.filter = sanitised (std::move (d.filter));
    d.maxVoices = std::clamp (d.maxVoices, 1, SynthDescription::maxVoicesLimit);
    d.glideMs = std::clamp (d.glideMs, 0, SynthDescription::maxGlideMs);
    return d;
}

std::string toJson (const SynthDescription& d)
{
    nlohmann::json o;
    o["waveform"] = toString (d.waveform);
    o["octave"] = d.octave;
    o["amp_envelope"] = detail::envelopeJson (d.ampEnv);
    o["filter"] = detail::filterJson (d.filter);
    o["polyphony"] = toString (d.polyphony);
    o["max_voices"] = d.maxVoices;
    o["voice_steal"] = toString (d.stealMode);
    o["glide_ms"] = d.glideMs;
    return o.dump();
}

SynthDescription synthFromJson (std::string_view text)
{
    SynthDescription d;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
    auto str = [&doc] (const char* k) -> std::string
    {
        const auto it = doc.find (k);
        return it != doc.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    waveformFromString (str ("waveform"), d.waveform);
    d.octave = detail::jsonInt (doc, "octave", d.octave);
    if (const auto it = doc.find ("amp_envelope"); it != doc.end() && it->is_object())
        d.ampEnv = detail::envelopeFrom (nlohmann::json { { "envelope", *it } });
    if (const auto it = doc.find ("filter"); it != doc.end() && it->is_object())
        d.filter = detail::filterFrom (*it);
    if (const auto v = str ("polyphony"); v == "mono") d.polyphony = PolyphonyMode::Mono;
    else if (v == "legato") d.polyphony = PolyphonyMode::Legato;
    else d.polyphony = PolyphonyMode::Poly;
    d.maxVoices = detail::jsonInt (doc, "max_voices", d.maxVoices);
    if (const auto v = str ("voice_steal"); v == "quietest") d.stealMode = VoiceStealMode::Quietest;
    else if (v == "keep_low") d.stealMode = VoiceStealMode::KeepLow;
    else if (v == "keep_high") d.stealMode = VoiceStealMode::KeepHigh;
    d.glideMs = detail::jsonInt (doc, "glide_ms", d.glideMs);
    return sanitised (std::move (d));
}
} // namespace neptune::core
