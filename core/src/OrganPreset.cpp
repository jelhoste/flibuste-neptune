#include "neptune/core/OrganPreset.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"

namespace neptune::core::organ
{
namespace
{
const std::vector<KeyedParam>& buildKeys()
{
    static const std::vector<KeyedParam> keys = []
    {
        std::vector<KeyedParam> k;
        static const char* const footage[9] = { "16", "513", "8", "4", "223", "2", "135", "113", "1" };
        static std::vector<std::string> names; // les clés doivent vivre aussi longtemps que le programme
        names.reserve (64);
        auto add = [&] (const std::string& key, int kind)
        {
            names.push_back (key);
            k.push_back ({ names.back().c_str(), mixer::organParam (0, kind) });
        };
        for (int i = 0; i < 9; ++i) add (std::string ("organ.upper.") + footage[i], mixer::kOrgUpper0 + i);
        for (int i = 0; i < 9; ++i) add (std::string ("organ.lower.") + footage[i], mixer::kOrgLower0 + i);
        add ("organ.pedal.16", mixer::kOrgPedal16);
        add ("organ.pedal.8", mixer::kOrgPedal8);
        add ("organ.perc.on", mixer::kOrgPercOn);
        add ("organ.perc.soft", mixer::kOrgPercSoft);
        add ("organ.perc.fast", mixer::kOrgPercFast);
        add ("organ.perc.third", mixer::kOrgPercThird);
        add ("organ.vibrato", mixer::kOrgVibMode);
        add ("organ.vibrato.upper", mixer::kOrgVibUpper);
        add ("organ.vibrato.lower", mixer::kOrgVibLower);
        add (leslieSpeedKey, mixer::kOrgLeslieSpeed);
        add ("organ.leslie.mix", mixer::kOrgLeslieMix);
        add ("organ.drive.on", mixer::kOrgDriveOn);
        add ("organ.drive.input", mixer::kOrgDriveInput);
        add ("organ.drive.character", mixer::kOrgDriveCharacter);
        add ("organ.drive.output", mixer::kOrgDriveOutput);
        add ("organ.reverb", mixer::kOrgReverb);
        return k;
    }();
    return keys;
}
} // namespace

const std::vector<KeyedParam>& keyedParams() { return buildKeys(); }

ParamId paramIdForKey (std::string_view key) noexcept
{
    for (const auto& k : buildKeys())
        if (key == k.key) return k.id;
    return 0;
}

ParamId idForTrack (ParamId id, int track) noexcept
{
    return track <= 0 ? id : id + probe::trackStride * static_cast<ParamId> (track);
}

preset::LibraryConfig defaultConfig()
{
    preset::LibraryConfig c;
    c.parameterCaptureDefaults[leslieSpeedKey] = preset::Capture::Free;
    return c;
}

preset::Preset capturePreset (const ParameterSet& set, const preset::LibraryConfig& cfg, const std::string& name, const std::string& type,
                              const preset::Preset* base, int track)
{
    preset::Preset p;
    if (base != nullptr)
    {
        p = *base;
        p.parameters.clear();
        p.factory = false;
    }
    else
    {
        p.id = preset::newId();
        p.name = name;
        p.type = type;
    }
    p.module = moduleId;
    p.state.clear();
    for (const auto& k : keyedParams())
    {
        if (preset::captureOf (k.key, p, cfg) == preset::Capture::Free) continue;
        const auto idx = set.indexOf (idForTrack (k.id, track));
        if (idx != ParameterSet::npos) p.parameters[k.key] = preset::ParamValue::ofNumber (set.getAt (idx));
    }
    return preset::sanitised (std::move (p));
}

void applyPresetValues (const preset::Preset& p, ParameterSet& set, const preset::LibraryConfig& cfg, int track)
{
    for (const auto& k : keyedParams())
    {
        if (preset::captureOf (k.key, p, cfg) == preset::Capture::Free) continue;
        const auto index = set.indexOf (idForTrack (k.id, track));
        if (index == ParameterSet::npos) continue;
        const auto it = p.parameters.find (k.key);
        // clé absente (preset incomplet) : le réglage retrouve son défaut, comme les macros du FM
        set.set (idForTrack (k.id, track), it != p.parameters.end() && ! it->second.isText ? it->second.number : set.spec (index).defaultValue);
    }
}

bool differsFromPreset (const preset::Preset& p, const ParameterSet& set, const preset::LibraryConfig& cfg, int track)
{
    for (const auto& [key, value] : p.parameters)
    {
        const ParamId id = paramIdForKey (key);
        if (id == 0 || value.isText || preset::captureOf (key, p, cfg) == preset::Capture::Free) continue;
        const auto index = set.indexOf (idForTrack (id, track));
        if (index != ParameterSet::npos && std::fabs (set.getAt (index) - value.number) > 1.0e-9) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------- presets livrés

namespace
{
struct Reg
{
    const char* name;
    const char* type;
    std::vector<std::string> traits;
    int upper[9], lower[9], pedal[2];
    bool perc, soft, fast, third;       // percussion : marche, douce, rapide, 3e harmonique
    int vib;                            // 0 sans ; 1..6 : V1 C1 V2 C2 V3 C3
    bool vibUpper, vibLower;
    int drive;                          // 0 : sans ; sinon % d'entrée de l'overdrive
    int reverb;                         // %
    int leslieMix;                      // %
};

preset::Preset make (int n, const Reg& r)
{
    static const char* const footage[9] = { "16", "513", "8", "4", "223", "2", "135", "113", "1" };
    preset::Preset p;
    p.id = std::string ("factory-organ-") + (n < 10 ? "0" : "") + std::to_string (n);
    p.name = r.name;
    p.type = r.type;
    p.characteristics = r.traits;
    p.module = moduleId;
    auto set = [&p] (const std::string& key, double v) { p.parameters[key] = preset::ParamValue::ofNumber (v); };
    for (int i = 0; i < 9; ++i)
    {
        set (std::string ("organ.upper.") + footage[i], r.upper[i]);
        set (std::string ("organ.lower.") + footage[i], r.lower[i]);
    }
    set ("organ.pedal.16", r.pedal[0]);
    set ("organ.pedal.8", r.pedal[1]);
    set ("organ.perc.on", r.perc ? 1 : 0);
    set ("organ.perc.soft", r.soft ? 1 : 0);
    set ("organ.perc.fast", r.fast ? 1 : 0);
    set ("organ.perc.third", r.third ? 1 : 0);
    set ("organ.vibrato", r.vib);
    set ("organ.vibrato.upper", r.vibUpper ? 1 : 0);
    set ("organ.vibrato.lower", r.vibLower ? 1 : 0);
    set ("organ.leslie.mix", r.leslieMix);
    set ("organ.drive.on", r.drive > 0 ? 1 : 0);
    set ("organ.drive.input", r.drive > 0 ? r.drive : 50);
    set ("organ.drive.character", 50);
    set ("organ.drive.output", 50);
    set ("organ.reverb", r.reverb);
    return p;
}
} // namespace

std::vector<preset::Preset> factoryPresets()
{
    // Points de départ classiques (tirettes 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'), jamais écoutés : à régler à l'oreille.
    const std::vector<Reg> regs = {
        { "Jazz Club B3", "Jazz", { "percussion", "chaud" }, { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 0 }, true, false, true, true, 6, true, false, 0, 10, 100 },
        { "Gospel Swell", "Gospel", { "plein", "tenu" }, { 8, 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 0 }, false, false, true, true, 4, true, true, 0, 20, 100 },
        { "Rock Comp", "Rock", { "saturé", "rythmique" }, { 8, 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 0, 0, 0, 0, 0, 0, 0, 0 }, { 8, 4 }, false, false, true, true, 0, true, false, 70, 5, 100 },
        { "Soft Pads", "Nappe", { "doux", "tenu" }, { 0, 0, 8, 0, 4, 0, 0, 0, 0 }, { 0, 0, 6, 0, 0, 0, 0, 0, 0 }, { 4, 0 }, false, false, true, true, 2, true, true, 0, 30, 100 },
        { "Perc Lead", "Lead", { "percussion", "solo" }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0, 4, 0, 0, 0, 0, 0, 0 }, { 0, 0 }, true, false, true, true, 0, false, false, 20, 5, 100 },
        { "Full Organ", "Plein jeu", { "plein", "brillant" }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 }, { 8, 8, 8, 8, 8, 0, 0, 0, 0 }, { 8, 8 }, false, false, true, true, 0, true, true, 0, 20, 100 },
        { "Whistle", "Flûtes", { "aigu", "pur" }, { 0, 0, 4, 0, 0, 8, 0, 0, 8 }, { 0, 0, 5, 0, 0, 0, 0, 0, 0 }, { 0, 0 }, false, false, true, true, 2, true, false, 0, 15, 100 },
        { "Bass Pedal", "Basse", { "grave", "pédalier" }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8 }, false, false, true, true, 0, false, false, 0, 5, 100 },
        { "Clean Jazz", "Jazz", { "clair", "percussion" }, { 0, 0, 8, 6, 0, 0, 0, 0, 0 }, { 0, 0, 6, 0, 0, 0, 0, 0, 0 }, { 6, 0 }, true, true, true, false, 2, true, false, 0, 10, 100 },
        { "Theatre", "Théâtre", { "vibrato", "ample" }, { 8, 0, 8, 8, 0, 4, 0, 0, 4 }, { 4, 0, 8, 4, 0, 0, 0, 0, 0 }, { 6, 0 }, false, false, true, true, 5, true, true, 0, 40, 60 },
        { "Dark Soul", "Soul", { "sombre", "chaud" }, { 8, 6, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 0 }, false, false, true, true, 4, true, false, 30, 10, 100 },
        { "Bright Top", "Brillant", { "aigu", "brillant" }, { 0, 0, 8, 8, 8, 8, 4, 4, 4 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0 }, false, false, true, true, 0, false, false, 0, 10, 100 },
        { "Cathedral", "Église", { "réverbéré", "plein" }, { 8, 0, 8, 4, 0, 2, 0, 0, 0 }, { 0, 0, 8, 4, 0, 0, 0, 0, 0 }, { 8, 8 }, false, false, true, true, 0, false, false, 0, 70, 0 },
        { "Bar Blues", "Blues", { "sale", "percussion" }, { 8, 8, 6, 0, 0, 0, 0, 0, 0 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 8, 0 }, true, false, false, true, 6, true, false, 40, 10, 100 },
        { "Funk Chop", "Funk", { "sec", "percussion" }, { 0, 0, 8, 8, 0, 0, 0, 0, 0 }, { 0, 0, 8, 0, 0, 0, 0, 0, 0 }, { 0, 0 }, true, false, true, true, 0, false, false, 50, 0, 100 },
        { "Slow Ballad", "Ballade", { "doux", "tenu" }, { 8, 8, 3, 0, 0, 0, 0, 0, 0 }, { 0, 0, 6, 0, 0, 0, 0, 0, 0 }, { 6, 0 }, false, false, true, true, 4, true, true, 0, 30, 100 },
        { "Pipe Mix", "Église", { "tuyaux", "mixtures" }, { 8, 0, 6, 8, 0, 6, 0, 0, 4 }, { 0, 0, 8, 6, 0, 0, 0, 0, 0 }, { 8, 0 }, false, false, true, true, 0, false, false, 0, 50, 20 },
        { "Shimmer", "Nappe", { "brillant", "tournant" }, { 0, 0, 6, 0, 6, 0, 6, 0, 6 }, { 0, 0, 5, 0, 0, 0, 0, 0, 0 }, { 0, 0 }, false, false, true, true, 6, true, true, 0, 45, 100 },
    };
    std::vector<preset::Preset> out;
    int n = 1;
    for (const auto& r : regs) out.push_back (make (n++, r));
    for (auto& p : out) p = preset::sanitised (std::move (p));
    return out;
}
} // namespace neptune::core::organ
