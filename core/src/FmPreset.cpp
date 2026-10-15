#include "neptune/core/FmPreset.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"

namespace neptune::core::fm
{
namespace
{
const std::vector<KeyedParam>& buildKeys()
{
    static const std::vector<KeyedParam> keys = []
    {
        std::vector<KeyedParam> k;
        k.push_back ({ "fm.brightness", mixer::fmParam (0, mixer::kFmBrightness) });
        k.push_back ({ "fm.env_speed", mixer::fmParam (0, mixer::kFmEnvSpeed) });
        k.push_back ({ "fm.feedback_drive", mixer::fmParam (0, mixer::kFmFeedbackDrive) });
        static const char* const ops[6] = { "fm.op1_level", "fm.op2_level", "fm.op3_level", "fm.op4_level", "fm.op5_level", "fm.op6_level" };
        for (int i = 0; i < 6; ++i) k.push_back ({ ops[i], mixer::fmParam (0, static_cast<mixer::FmKind> (mixer::kFmLevelOp1 + i)) });
        k.push_back ({ "fm.unison_detune", mixer::fmParam (0, mixer::kFmUnisonDetune) });
        k.push_back ({ "fm.unison_spread", mixer::fmParam (0, mixer::kFmUnisonSpread) });
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

preset::LibraryConfig defaultConfig() { return preset::LibraryConfig {}; }

preset::Preset capturePreset (const ParameterSet& set, const FmDescription& d, const preset::LibraryConfig& cfg, const std::string& name,
                              const std::string& type, const preset::Preset* base, int track)
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
    p.state = toJson (d);
    for (const auto& k : keyedParams())
    {
        if (preset::captureOf (k.key, p, cfg) == preset::Capture::Free) continue;
        const auto idx = set.indexOf (idForTrack (k.id, track));
        if (idx != ParameterSet::npos) p.parameters[k.key] = preset::ParamValue::ofNumber (set.getAt (idx));
    }
    return preset::sanitised (std::move (p));
}

preset::Preset presetFromPatch (const Patch& patch, const std::string& type)
{
    FmDescription d;
    d.patch = patch;
    d.patch.sanitise();
    preset::Preset p;
    p.id = preset::newId();
    p.name = d.patch.name().empty() ? "Voice" : d.patch.name();
    p.type = type;
    p.module = moduleId;
    p.state = toJson (d);
    return preset::sanitised (std::move (p));
}

FmDescription presetDescription (const preset::Preset& p, const FmDescription& current, const preset::LibraryConfig&)
{
    if (p.state.empty()) return current;
    return fmFromJson (p.state);
}

void applyPresetValues (const preset::Preset& p, ParameterSet& set, const preset::LibraryConfig& cfg, int track)
{
    // un preset sans valeur de macro (import sysex) remet les macros à leur défaut : le son est celui du patch
    for (const auto& k : keyedParams())
    {
        if (preset::captureOf (k.key, p, cfg) == preset::Capture::Free) continue;
        const auto it = p.parameters.find (k.key);
        const auto index = set.indexOf (idForTrack (k.id, track));
        if (index == ParameterSet::npos) continue;
        if (it != p.parameters.end() && ! it->second.isText) set.set (idForTrack (k.id, track), it->second.number);
        else set.set (idForTrack (k.id, track), set.spec (index).defaultValue);
    }
}

bool differsFromPreset (const preset::Preset& p, const ParameterSet& set, const FmDescription& d, const preset::LibraryConfig& cfg, int track)
{
    for (const auto& [key, value] : p.parameters)
    {
        const ParamId id = paramIdForKey (key);
        if (id == 0 || value.isText || preset::captureOf (key, p, cfg) == preset::Capture::Free) continue;
        const auto index = set.indexOf (idForTrack (id, track));
        if (index != ParameterSet::npos && std::fabs (set.getAt (index) - value.number) > 1.0e-9) return true;
    }
    if (! p.state.empty() && ! (fmFromJson (p.state) == sanitised (d))) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------------- presets livrés

namespace
{
// Un opérateur (indice sysex : 0 = OP6 ... 5 = OP1) : enveloppe, niveau, rapport.
void setOperator (Patch& p, int op, int outLevel, int coarse, int fine, int detune, const int rates[4], const int levels[4])
{
    p.setOp (op, kOutputLevel, outLevel);
    p.setOp (op, kCoarse, coarse);
    p.setOp (op, kFine, fine);
    p.setOp (op, kDetune, detune);
    for (int i = 0; i < 4; ++i)
    {
        p.setOp (op, kRate1 + i, rates[i]);
        p.setOp (op, kLevel1 + i, levels[i]);
    }
}

preset::Preset make (int n, const char* type, std::vector<std::string> traits, const Patch& patch)
{
    FmDescription d;
    d.patch = patch;
    preset::Preset p;
    p.id = std::string ("factory-fm6op-") + (n < 10 ? "0" : "") + std::to_string (n);
    p.name = patch.name();
    p.type = type;
    p.characteristics = std::move (traits);
    p.module = moduleId;
    p.state = toJson (d);
    return p;
}
} // namespace

std::vector<preset::Preset> factoryPresets()
{
    std::vector<preset::Preset> out;
    const int flat[4] = { 99, 99, 99, 99 };
    const int full[4] = { 99, 99, 99, 0 };
    {
        Patch p = Patch::initVoice();
        p.setName ("SINE");
        out.push_back (make (1, "Lead", { "pur", "simple" }, p));
    }
    {
        // « E.PIANO » : deux paires modulateur -> porteuse (algorithme 5 : OP1/OP2, OP3/OP4, OP5/OP6), la seconde paire donne le tintement
        Patch p = Patch::initVoice();
        p.setName ("BASIC EP");
        p.setGlobal (kAlgorithmOffset, 4);
        p.setGlobal (kFeedbackOffset, 4);
        const int carrierR[4] = { 95, 40, 35, 60 }, carrierL[4] = { 99, 75, 0, 0 };
        const int modR[4] = { 99, 45, 25, 60 }, modL[4] = { 99, 55, 0, 0 };
        const int tineR[4] = { 99, 70, 30, 60 }, tineL[4] = { 99, 40, 0, 0 };
        setOperator (p, 5, 99, 1, 0, 7, carrierR, carrierL); // OP1 porteuse
        setOperator (p, 4, 62, 1, 0, 7, modR, modL);         // OP2 module OP1
        setOperator (p, 3, 90, 1, 0, 8, carrierR, carrierL); // OP3 porteuse légèrement désaccordée
        setOperator (p, 2, 58, 14, 0, 7, tineR, tineL);      // OP4 module OP3 : harmonique haute, courte
        setOperator (p, 1, 0, 1, 0, 7, flat, full);
        setOperator (p, 0, 0, 1, 0, 7, flat, full);
        p.setOp (5, kVelocitySens, 3);
        p.setOp (4, kVelocitySens, 5);
        p.setOp (2, kVelocitySens, 6);
        out.push_back (make (2, "Clavier", { "doux", "électrique" }, p));
    }
    {
        // « CLOCHE » : rapport inharmonique 3,5 de la modulatrice, enveloppe longue qui s'éteint
        Patch p = Patch::initVoice();
        p.setName ("BASIC BEL");
        p.setGlobal (kAlgorithmOffset, 4);
        const int carrierR[4] = { 99, 28, 28, 45 }, carrierL[4] = { 99, 80, 0, 0 };
        const int modR[4] = { 99, 32, 30, 45 }, modL[4] = { 99, 60, 0, 0 };
        setOperator (p, 5, 99, 1, 0, 7, carrierR, carrierL);
        setOperator (p, 4, 72, 3, 50, 7, modR, modL);       // 3,5 : coarse 3, fine 50 %
        for (int op : { 3, 2, 1, 0 }) setOperator (p, op, 0, 1, 0, 7, flat, full);
        out.push_back (make (3, "Percussion", { "brillant", "métallique" }, p));
    }
    {
        // « BASSE FM » : une porteuse modulée par un opérateur à l'octave, attaque nette, feedback
        Patch p = Patch::initVoice();
        p.setName ("BASIC BAS");
        p.setGlobal (kAlgorithmOffset, 15);
        p.setGlobal (kFeedbackOffset, 6);
        const int carrierR[4] = { 99, 55, 70, 70 }, carrierL[4] = { 99, 90, 85, 0 };
        const int modR[4] = { 99, 60, 50, 70 }, modL[4] = { 99, 50, 30, 0 };
        setOperator (p, 5, 99, 0, 0, 7, carrierR, carrierL); // coarse 0 : une octave plus bas (0,5)
        setOperator (p, 4, 78, 1, 0, 7, modR, modL);
        for (int op : { 3, 2, 1, 0 }) setOperator (p, op, 0, 1, 0, 7, flat, full);
        out.push_back (make (4, "Basse", { "gras", "mordant" }, p));
    }
    for (auto& p : out) p = preset::sanitised (std::move (p));
    return out;
}
} // namespace neptune::core::fm
