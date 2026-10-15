#include "neptune/core/OrganSynth.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "neptune/core/SamplerParams.h"

namespace neptune::core::organ
{
using nlohmann::json;
namespace
{
constexpr const char* kFormat = "organ-state";
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
        && mixer::addOrganParams (set, 0, "", error);
}

OrganLive liveFromParams (const ParameterSet& set, int track)
{
    OrganLive l;
    auto get = [&set, track] (int k, double fallback)
    {
        const auto i = set.indexOf (mixer::organParam (track, static_cast<mixer::OrganKind> (k)));
        return i == ParameterSet::npos ? fallback : set.getAt (i);
    };
    auto db = [&get] (int k, int fallback) { return std::clamp (static_cast<int> (std::lround (get (k, fallback))), 0, 8); };
    for (int i = 0; i < kDrawbars; ++i)
    {
        l.drawbars[0][static_cast<std::size_t> (i)] = db (mixer::kOrgUpper0 + i, l.drawbars[0][static_cast<std::size_t> (i)]);
        l.drawbars[1][static_cast<std::size_t> (i)] = db (mixer::kOrgLower0 + i, 0);
    }
    l.drawbars[2][0] = db (mixer::kOrgPedal16, 0);
    l.drawbars[2][2] = db (mixer::kOrgPedal8, 0);
    l.percussionOn = get (mixer::kOrgPercOn, 0) >= 0.5;
    l.percussionSoft = get (mixer::kOrgPercSoft, 0) >= 0.5;
    l.percussionFast = get (mixer::kOrgPercFast, 1) >= 0.5;
    l.percussionThird = get (mixer::kOrgPercThird, 1) >= 0.5;
    l.vibratoMode = std::clamp (static_cast<int> (std::lround (get (mixer::kOrgVibMode, 0))), 0, kVibratoModes - 1);
    l.vibratoUpper = get (mixer::kOrgVibUpper, 1) >= 0.5;
    l.vibratoLower = get (mixer::kOrgVibLower, 0) >= 0.5;
    l.leslieSpeed = std::clamp (static_cast<int> (std::lround (get (mixer::kOrgLeslieSpeed, 0))), 0, 2);
    l.leslieMix = static_cast<float> (std::clamp (get (mixer::kOrgLeslieMix, 100.0) / 100.0, 0.0, 1.0));
    l.driveOn = get (mixer::kOrgDriveOn, 0) >= 0.5;
    l.driveInput = static_cast<float> (std::clamp (get (mixer::kOrgDriveInput, 50.0) / 100.0, 0.0, 1.0));
    l.driveCharacter = static_cast<float> (std::clamp (get (mixer::kOrgDriveCharacter, 50.0) / 100.0, 0.0, 1.0));
    l.driveOutput = static_cast<float> (std::clamp (get (mixer::kOrgDriveOutput, 50.0) / 100.0, 0.0, 1.0));
    l.reverbMix = static_cast<float> (std::clamp (get (mixer::kOrgReverb, 0.0) / 100.0, 0.0, 1.0));
    return l;
}

std::string stateToJson (const ParameterSet& set, const OrganDescription& d, const std::string& currentPresetId)
{
    json doc;
    doc["format"] = kFormat;
    doc["schema_version"] = kSchema;
    json params = json::object();
    for (std::size_t i = 0; i < set.size(); ++i) params[std::to_string (set.spec (i).id)] = set.getAt (i);
    doc["parameters"] = std::move (params);
    const auto o = json::parse (toJson (d), nullptr, false);
    if (! o.is_discarded()) doc["organ"] = o;
    if (! currentPresetId.empty()) doc["preset_id"] = currentPresetId;
    return doc.dump (2);
}

bool stateFromJson (std::string_view text, ParameterSet& set, OrganDescription& d, std::string& currentPresetId)
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
    d = OrganDescription();
    if (const auto o = doc.find ("organ"); o != doc.end() && o->is_object()) d = organFromJson (o->dump());
    currentPresetId.clear();
    if (const auto p = doc.find ("preset_id"); p != doc.end() && p->is_string() && p->get<std::string>().size() <= 64)
        currentPresetId = p->get<std::string>();
    return true;
}

OrganSynthEngine::OrganSynthEngine()
{
    std::string error;
    ok_ = addSynthParams (params_, &error);
    auto idx = [this] (ParamId id) { return params_.indexOf (id); };
    idxVolume_ = idx (synthVolume);
    idxPan_ = idx (synthPan);
    idxGain_ = idx (sampler::gain);
    setDescription (OrganDescription());
}

void OrganSynthEngine::prepare (double sampleRate, int)
{
    voices_.reserve (sampleRate);
    lastFader_ = static_cast<float> (sampler::dbToGain (params_.getAt (idxVolume_)));
    lastGain_ = static_cast<float> (sampler::dbToGain (params_.getAt (idxGain_)));
    float l = 1.0f, r = 1.0f;
    mixer::panGains (mixer::PanMode::Balance, static_cast<float> (params_.getAt (idxPan_) / 100.0), l, r);
    lastPanL_ = l;
    lastPanR_ = r;
}

void OrganSynthEngine::setDescription (const OrganDescription& d)
{
    description_ = sanitised (d);
    slot_.publish (std::make_shared<const OrganDescription> (description_));
    slot_.collect();
}

void OrganSynthEngine::noteOn (int channel, int key) noexcept
{
    adopt();
    if (key >= 0 && key < 128) held_[key >> 6].fetch_or (std::uint64_t (1) << (key & 63), std::memory_order_relaxed);
    voices_.noteOn (channel, key);
}

void OrganSynthEngine::noteOff (int channel, int key) noexcept
{
    adopt();
    if (key >= 0 && key < 128) held_[key >> 6].fetch_and (~(std::uint64_t (1) << (key & 63)), std::memory_order_relaxed);
    voices_.noteOff (channel, key);
}

void OrganSynthEngine::post (Event::Type t, int channel, int key) noexcept
{
    const auto head = head_.load (std::memory_order_relaxed);
    if (head - tail_.load (std::memory_order_acquire) >= kQueue) return;
    queue_[head % kQueue] = Event { t, channel, key };
    head_.store (head + 1, std::memory_order_release);
}

void OrganSynthEngine::drain() noexcept
{
    auto tail = tail_.load (std::memory_order_relaxed);
    const auto head = head_.load (std::memory_order_acquire);
    while (tail != head)
    {
        const Event e = queue_[tail % kQueue];
        switch (e.type)
        {
            case Event::NoteOn: noteOn (e.channel, e.key); break;
            case Event::NoteOff: noteOff (e.channel, e.key); break;
            case Event::Panic: panic(); break;
        }
        ++tail;
    }
    tail_.store (tail, std::memory_order_release);
}

void OrganSynthEngine::process (float* left, float* right, int n) noexcept
{
    if (left == nullptr || n <= 0) return;
    adopt();
    drain();
    std::fill (left, left + n, 0.0f);
    if (right != nullptr) std::fill (right, right + n, 0.0f);

    const OrganLive live = liveFromParams (params_);
    const float gain = static_cast<float> (sampler::dbToGain (params_.getAt (idxGain_)));
    voices_.render (left, right, n, lastGain_, gain, live);
    lastGain_ = gain;

    const float fader = static_cast<float> (sampler::dbToGain (params_.getAt (idxVolume_)));
    float pl = 1.0f, pr = 1.0f;
    mixer::panGains (mixer::PanMode::Balance, static_cast<float> (params_.getAt (idxPan_) / 100.0), pl, pr);
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
}
} // namespace neptune::core::organ
