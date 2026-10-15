#include "neptune/core/Mixer.h"

#include <algorithm>

namespace neptune::core::mixer
{
bool addSynthParams (ParameterSet& set, int track, const std::string& prefix, std::string* error)
{
    static const char* const sourceNames[5] = { "Osc 1 Level", "Osc 2 Level", "Osc 3 Level", "Sub Level", "Noise Level" };
    for (int k = 0; k < kSynthKinds; ++k)
    {
        const auto id = synthParam (track, static_cast<SynthKind> (k));
        ParamSpec spec;
        const std::string o = std::to_string (k % 3 + 1);
        if (k <= kLevelNoise) spec = ParamSpec::continuous (id, prefix + sourceNames[k], Unit::Percent, 0.0, 100.0, k == kLevelOsc1 ? 100.0 : 0.0);
        else if (k <= kFineOsc3) spec = ParamSpec::continuous (id, prefix + "Osc " + o + " Fine", Unit::Cents, -100.0, 100.0, 0.0);
        else if (k <= kPulseOsc3) spec = ParamSpec::continuous (id, prefix + "Osc " + o + " Pulse Width", Unit::Percent, 2.0, 98.0, 50.0);
        else if (k <= kDetuneOsc3) spec = ParamSpec::continuous (id, prefix + "Osc " + o + " Unison Detune", Unit::Cents, 0.0, 100.0, 20.0);
        else spec = ParamSpec::continuous (id, prefix + "Pitch Env Depth", Unit::Semitones, -48.0, 48.0, 0.0);
        if (! set.add (std::move (spec), error)) return false;
    }
    return true;
}

bool addParams (ParameterSet& set, int tracks, int buses, std::string* error)
{
    auto level = [] (ParamId id, std::string name, double def)
    {
        auto s = ParamSpec::continuous (id, std::move (name), Unit::Decibel, -100.0, 6.0, def);
        s.minText = "-inf";
        return s;
    };
    bool ok = true;

    // Instrument de chaque piste (la piste 0 est déjà déclarée par sampler::addParams, avec les identifiants historiques).
    for (int t = 1; ok && t < tracks; ++t)
    {
        const std::string p = "Track " + std::to_string (t + 1) + " ";
        auto gain = ParamSpec::continuous (samplerParam (t, kGain), p + "Gain", Unit::Decibel, sampler::silenceDb, 12.0, 0.0);
        gain.minText = "-inf";
        ok = set.add (std::move (gain), error)
          && set.add (ParamSpec::stepped (samplerParam (t, kTuneSemitones), p + "Tune (semitones)", Unit::Semitones, -24, 24, 0), error)
          && set.add (ParamSpec::continuous (samplerParam (t, kTuneCents), p + "Fine tune", Unit::Cents, -100.0, 100.0, 0.0), error)
          && set.add (ParamSpec::continuous (samplerParam (t, kFilter1Cutoff), p + "Filter 1 Cutoff", Unit::Hertz, 20.0, 20000.0, 20000.0, Curve::Exponential), error)
          && set.add (ParamSpec::continuous (samplerParam (t, kFilter1Resonance), p + "Filter 1 Reso", Unit::Percent, 0.0, 100.0, 0.0), error)
          && set.add (ParamSpec::continuous (samplerParam (t, kFilter2Cutoff), p + "Filter 2 Cutoff", Unit::Hertz, 20.0, 20000.0, 20000.0, Curve::Exponential), error)
          && set.add (ParamSpec::continuous (samplerParam (t, kFilter2Resonance), p + "Filter 2 Reso", Unit::Percent, 0.0, 100.0, 0.0), error);
    }

    // Synthé : 15 paramètres par piste
    for (int t = 0; ok && t < tracks; ++t) ok = addSynthParams (set, t, "Track " + std::to_string (t + 1) + " ", error);

    // Pistes parallèles
    for (int p = 0; ok && p < numParallels; ++p)
    {
        const std::string n = "Parallel " + std::to_string (p + 1) + " ";
        ok = set.add (level (parallelParam (p, kParVolume), n + "Volume", 0.0), error)
          && set.add (ParamSpec::continuous (parallelParam (p, kParPan), n + "Pan", Unit::Percent, -100.0, 100.0, 0.0), error)
          && set.add (ParamSpec::toggle (parallelParam (p, kParMute), n + "Mute"), error)
          && set.add (ParamSpec::toggle (parallelParam (p, kParSolo), n + "Solo"), error);
        for (int i = 0; ok && i < numInserts; ++i)
            ok = set.add (ParamSpec::toggle (parallelParam (p, kParInsertBypass + static_cast<ParamId> (i)), n + "Insert " + std::to_string (i + 1) + " Bypass"), error);
        for (int i = 0; ok && i < numSends; ++i)
            ok = set.add (level (parallelParam (p, kParSendLevel + static_cast<ParamId> (i)), n + "Send " + std::to_string (i + 1) + " Level", -100.0), error);
    }

    ok = ok && set.add (ParamSpec::continuous (masterTrim, "Master Trim", Unit::Decibel, -12.0, 12.0, 0.0), error)
            && set.add (ParamSpec::toggle (masterMono, "Master Mono"), error);
    for (int i = 0; ok && i < numInserts; ++i)
        ok = set.add (ParamSpec::toggle (masterInsertBypassId (i), "Master Insert " + std::to_string (i + 1) + " Bypass"), error);

    for (int b = 0; ok && b < buses; ++b)
    {
        const std::string p = "Bus " + std::to_string (b + 1) + " ";
        ok = set.add (level (busParam (b, kBusVolume), p + "Volume", 0.0), error)
          && set.add (ParamSpec::continuous (busParam (b, kBusPan), p + "Pan", Unit::Percent, -100.0, 100.0, 0.0), error)
          && set.add (ParamSpec::toggle (busParam (b, kBusMute), p + "Mute"), error)
          && set.add (ParamSpec::toggle (busParam (b, kBusSolo), p + "Solo"), error);
        for (int i = 0; ok && i < numInserts; ++i)
            ok = set.add (ParamSpec::toggle (busParam (b, kBusInsertBypass + static_cast<ParamId> (i)), p + "Insert " + std::to_string (i + 1) + " Bypass"), error);
        for (int i = 0; ok && i < numSends; ++i)
            ok = set.add (level (busParam (b, kBusSendLevel + static_cast<ParamId> (i)), p + "Send " + std::to_string (i + 1) + " Level", -100.0), error);
    }
    return ok;
}

StripSettings sanitisedStrip (StripSettings s, bool isBus, int index)
{
    if (s.name.size() > 64) s.name.resize (64);
    s.color = std::clamp (s.color, 0, 15);
    s.midiChannel = isBus ? 0 : std::clamp (s.midiChannel, 0, 16);
    s.routing.directOut = std::clamp (s.routing.directOut, 0, numOutputs);
    s.routing.mainDest = std::clamp (s.routing.mainDest, 0, numBuses);
    // pas de cycle : un bus ne route que vers un bus d'indice supérieur (les bus sont numérotés 1..numBuses ici)
    if (isBus && s.routing.mainDest != 0 && s.routing.mainDest <= index + 1) s.routing.mainDest = 0;
    s.panMode = s.panMode == PanMode::True ? PanMode::True : PanMode::Balance;
    for (auto& in : s.inserts)
        if (in.type.size() > 32) in = InsertState {};
    for (auto& send : s.sends)
    {
        send.destination = std::clamp (send.destination, 0, numBuses);
        if (isBus && send.destination != 0 && send.destination <= index + 1) send.destination = 0;
        const int tap = std::clamp (static_cast<int> (send.tap), 0, 3);
        send.tap = static_cast<TapPoint> (tap);
        send.pan = std::isfinite (send.pan) ? std::clamp (send.pan, -1.0, 1.0) : 0.0;
    }
    return s;
}

ParallelSettings sanitised (ParallelSettings p, int index)
{
    p.parent = std::clamp (p.parent, 0, numTracks + numParallels - 1);
    if (p.parent == numTracks + index) p.parent = 0; // jamais sa propre piste
    p.tap = static_cast<TapPoint> (std::clamp (static_cast<int> (p.tap), 0, 3));
    p.strip = sanitisedStrip (std::move (p.strip), false, index);
    p.strip.midiChannel = 0;
    // les envois d'une piste parallèle vont vers des bus (aucun cycle : seules les pistes alimentent des bus)
    return p;
}

MasterSettings sanitised (MasterSettings m)
{
    m.output = std::clamp (m.output, 1, numOutputs);
    m.panMode = m.panMode == PanMode::True ? PanMode::True : PanMode::Balance;
    return m;
}
} // namespace neptune::core::mixer
