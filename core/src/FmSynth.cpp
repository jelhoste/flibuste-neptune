#include "neptune/core/FmSynth.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "neptune/core/FmPreset.h"
#include "neptune/core/SamplerParams.h"

namespace neptune::core::fm
{
using nlohmann::json;
namespace
{
constexpr const char* kFormat = "fm6op-state";
constexpr int kSchema = 1;
} // namespace

bool addSynthParams (ParameterSet& set, std::string* error)
{
    auto vol = ParamSpec::continuous (synthVolume, "Volume", Unit::Decibel, -100.0, 6.0, 0.0);
    vol.minText = "-inf";
    auto gain = ParamSpec::continuous (sampler::gain, "Gain", Unit::Decibel, sampler::silenceDb, 12.0, 0.0);
    gain.minText = "-inf";
    return set.add (std::move (vol), error)
        && set.add (ParamSpec::continuous (synthPan, "Pan", Unit::Percent, -100.0, 100.0, 0.0), error)
        && set.add (std::move (gain), error)
        && set.add (ParamSpec::stepped (sampler::tuneSemitones, "Tune (semitones)", Unit::Semitones, -24, 24, 0), error)
        && set.add (ParamSpec::continuous (sampler::tuneCents, "Fine tune", Unit::Cents, -100.0, 100.0, 0.0), error)
        && mixer::addFmParams (set, 0, "", error);
}

std::string stateToJson (const ParameterSet& set, const FmDescription& d, const std::string& currentPresetId)
{
    json doc;
    doc["format"] = kFormat;
    doc["schema_version"] = kSchema;
    json params = json::object();
    for (std::size_t i = 0; i < set.size(); ++i) params[std::to_string (set.spec (i).id)] = set.getAt (i);
    doc["parameters"] = std::move (params);
    const auto fmJson = json::parse (toJson (d), nullptr, false);
    if (! fmJson.is_discarded()) doc["fm"] = fmJson;
    if (! currentPresetId.empty()) doc["preset_id"] = currentPresetId;
    return doc.dump (2);
}

bool stateFromJson (std::string_view text, ParameterSet& set, FmDescription& d, std::string& currentPresetId)
{
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return false;
    const auto fmt = doc.find ("format");
    if (fmt == doc.end() || ! fmt->is_string() || fmt->get<std::string>() != kFormat) return false;
    set.resetAll();
    if (const auto p = doc.find ("parameters"); p != doc.end() && p->is_object())
        for (const auto& [key, value] : p->items())
        {
            if (! value.is_number() || ! std::isfinite (value.get<double>())) continue;
            const long long id = std::strtoll (key.c_str(), nullptr, 10);
            if (id > 0) set.set (static_cast<ParamId> (id), value.get<double>());
        }
    d = FmDescription();
    if (const auto f = doc.find ("fm"); f != doc.end() && f->is_object()) d = fmFromJson (f->dump());
    seedExtraParams (d, set); // la description fait foi (les états d'avant ces paramètres n'en portent pas)
    currentPresetId.clear();
    if (const auto p = doc.find ("preset_id"); p != doc.end() && p->is_string() && p->get<std::string>().size() <= 64)
        currentPresetId = p->get<std::string>();
    return true;
}

// ---------------------------------------------------------------------------------------------------- moteur

FmSynthEngine::FmSynthEngine()
{
    std::string error;
    ok_ = addSynthParams (params_, &error);
    auto idx = [this] (ParamId id) { return params_.indexOf (id); };
    for (int k = 0; k < mixer::kFmKinds; ++k) fmIdx_[static_cast<std::size_t> (k)] = idx (mixer::fmParam (0, static_cast<mixer::FmKind> (k)));
    for (int k = 0; k < mixer::kFmExtraKinds; ++k)
        extraIdx_[static_cast<std::size_t> (k)] = idx (mixer::fmExtraParam (0, static_cast<mixer::FmExtraKind> (k)));
    idxVolume_ = idx (synthVolume);
    idxPan_ = idx (synthPan);
    idxGain_ = idx (sampler::gain);
    idxSemi_ = idx (sampler::tuneSemitones);
    idxCents_ = idx (sampler::tuneCents);
    voices_.reserve();
    setDescription (FmDescription());
}

void FmSynthEngine::prepare (double sampleRate, int)
{
    initTables (sampleRate);
    voices_.prepare (sampleRate);
    lastFader_ = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxVolume_)));
    lastGain_ = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxGain_)));
    float l = 1.0f, r = 1.0f;
    mixer::panGains (mixer::PanMode::Balance, static_cast<float> (params_.getAt (idxPan_) / 100.0), l, r);
    lastPanL_ = l;
    lastPanR_ = r;
}

void FmSynthEngine::setDescription (const FmDescription& d)
{
    description_ = sanitised (d);
    slot_.publish (std::make_shared<const FmDescription> (description_));
    slot_.collect();
    seedExtraParams (description_, params_); // les paramètres suivent la description
}

FmDescription FmSynthEngine::description() const { return withLiveParams (description_, params_); }

void FmSynthEngine::noteOn (int channel, int key, float velocity) noexcept
{
    adopt();
    if (key >= 0 && key < 128) held_[key >> 6].fetch_or (std::uint64_t (1) << (key & 63), std::memory_order_relaxed);
    voices_.noteOn (channel, key, velocity);
}

void FmSynthEngine::noteOff (int channel, int key) noexcept
{
    adopt();
    if (key >= 0 && key < 128) held_[key >> 6].fetch_and (~(std::uint64_t (1) << (key & 63)), std::memory_order_relaxed);
    voices_.noteOff (channel, key);
}

void FmSynthEngine::post (Event::Type t, int key, float velocity) noexcept
{
    const auto head = head_.load (std::memory_order_relaxed);
    if (head - tail_.load (std::memory_order_acquire) >= kQueue) return;
    queue_[head % kQueue] = Event { t, key, velocity };
    head_.store (head + 1, std::memory_order_release);
}

void FmSynthEngine::drain() noexcept
{
    auto tail = tail_.load (std::memory_order_relaxed);
    const auto head = head_.load (std::memory_order_acquire);
    while (tail != head)
    {
        const Event e = queue_[tail % kQueue];
        switch (e.type)
        {
            case Event::NoteOn: noteOn (0, e.key, e.velocity); break;
            case Event::NoteOff: noteOff (0, e.key); break;
            case Event::Panic: panic(); break;
            case Event::ReleaseAll: releaseAll(); break;
        }
        ++tail;
    }
    tail_.store (tail, std::memory_order_release);
}

void FmSynthEngine::process (float* left, float* right, int n) noexcept
{
    if (left == nullptr || n <= 0) return;
    adopt();
    drain();
    std::fill (left, left + n, 0.0f);
    if (right != nullptr) std::fill (right, right + n, 0.0f);

    // le son lit les valeurs effectives : la modulation de l'hôte (et celle de la matrice) s'y ajoute sans changer les réglages
    FmLive live;
    auto get = [this] (mixer::FmKind k) { return params_.effectiveAt (fmIdx_[static_cast<std::size_t> (k)]); };
    live.brightness = static_cast<float> (get (mixer::kFmBrightness) / 100.0);
    live.envSpeed = static_cast<float> (get (mixer::kFmEnvSpeed) / 100.0);
    live.feedbackDrive = static_cast<float> (get (mixer::kFmFeedbackDrive) / 100.0);
    for (int op = 0; op < kOperators; ++op)
        live.opLevel[static_cast<std::size_t> (op)] = static_cast<float> (get (static_cast<mixer::FmKind> (mixer::kFmLevelOp1 + op)));
    live.unisonDetuneCents = static_cast<float> (get (mixer::kFmUnisonDetune));
    live.unisonSpread = static_cast<float> (get (mixer::kFmUnisonSpread) / 100.0);
    live.tuneCents = static_cast<float> (params_.effectiveAt (idxSemi_) * 100.0 + params_.effectiveAt (idxCents_));
    live.modWheel = modWheel_;
    live.pitchBend = pitchBend_;
    auto extra = [this] (mixer::FmExtraKind k) { return static_cast<int> (std::lround (params_.effectiveAt (extraIdx_[static_cast<std::size_t> (k)]))); };
    live.lfoSpeed = extra (mixer::kFmLfoSpeed);
    live.lfoPmd = extra (mixer::kFmLfoPmd);
    live.lfoAmd = extra (mixer::kFmLfoAmd);
    live.bendRange = extra (mixer::kFmBendRange);
    live.wheelPitchSens = extra (mixer::kFmWheelPitch);
    live.wheelAmpSens = extra (mixer::kFmWheelAmp);
    live.glideMs = extra (mixer::kFmGlide);

    const float gain = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxGain_)));
    voices_.render (left, right, n, lastGain_, gain, live);
    lastGain_ = gain;

    const float fader = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxVolume_)));
    float pl = 1.0f, pr = 1.0f;
    mixer::panGains (mixer::PanMode::Balance, static_cast<float> (params_.effectiveAt (idxPan_) / 100.0), pl, pr);
    const float inv = 1.0f / static_cast<float> (n);
    const float fStep = (fader - lastFader_) * inv, lStep = (pl - lastPanL_) * inv, rStep = (pr - lastPanR_) * inv;
    float f = lastFader_, gl = lastPanL_, gr = lastPanR_;
    for (int i = 0; i < n; ++i)
    {
        f += fStep; gl += lStep; gr += rStep;
        left[i] *= f * gl;
        if (right != nullptr) right[i] *= f * gr;
    }
    lastFader_ = fader;
    lastPanL_ = pl;
    lastPanR_ = pr;
    voiceCount_.store (voices_.activeVoices(), std::memory_order_relaxed);
}
} // namespace neptune::core::fm
