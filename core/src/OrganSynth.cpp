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

static_assert (mixer::kOrganKinds == kOrganValues, "OrganKind et liveFromValues doivent rester d'accord");

OrganLive liveFromParams (const ParameterSet& set, int track)
{
    // valeurs par défaut des paramètres déclarés, remplacées par les valeurs courantes quand le paramètre existe
    double v[kOrganValues];
    for (int k = 0; k < kOrganValues; ++k)
    {
        const auto i = set.indexOf (mixer::organParam (track, k));
        v[k] = i == ParameterSet::npos ? 0.0 : set.effectiveAt (i);
    }
    return liveFromValues (v);
}

std::string stateToJson (const ParameterSet& set, const OrganDescription& d, const std::string& currentPresetId, bool ccEnabled)
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
    if (ccEnabled) doc["cc_enabled"] = true;
    return doc.dump (2);
}

bool stateFromJson (std::string_view text, ParameterSet& set, OrganDescription& d, std::string& currentPresetId, bool* ccEnabled)
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
    if (ccEnabled != nullptr)
    {
        const auto c = doc.find ("cc_enabled");
        *ccEnabled = c != doc.end() && c->is_boolean() && c->get<bool>();
    }
    return true;
}

bool applyControlChange (const OrganDescription& d, ParameterSet& set, int channel, int cc, int value, int track)
{
    value = std::clamp (value, 0, 127);
    const int ch = channel + 1;
    auto put = [&] (int kind, double v) { set.set (mixer::organParam (track, kind), v); };
    auto get = [&] (int kind) { return set.get (mixer::organParam (track, kind)); };
    const bool upper = d.channel[0] != 0 && ch == d.channel[0];
    const bool lower = d.channel[1] != 0 && ch == d.channel[1];
    const bool pedal = d.channel[2] != 0 && ch == d.channel[2];
    if (! (upper || lower || pedal)) return false;

    // tirettes : 127 = rentrée, 0 = sortie à fond (comme les faders d'un contrôleur)
    if (cc >= 70 && cc <= 78)
    {
        const int i = cc - 70;
        const double db = std::rint ((127 - value) * 8.0 / 127.0);
        if (upper) put (mixer::kOrgUpper0 + i, db);
        else if (lower) put (mixer::kOrgLower0 + i, db);
        else if (pedal && i == 0) put (mixer::kOrgPedal16, db);
        else if (pedal && i == 2) put (mixer::kOrgPedal8, db);
        else return false;
        return true;
    }
    // communs à tous les claviers
    if (cc == 1) { put (mixer::kOrgLeslieSpeed, value / 43 == 0 ? kLeslieChorale : value / 43 == 1 ? kLeslieBrake : kLeslieTremolo); return true; }
    if (cc == 64)
    {
        if (value > 63) put (mixer::kOrgLeslieSpeed, std::lround (get (mixer::kOrgLeslieSpeed)) == kLeslieChorale ? kLeslieTremolo : kLeslieChorale);
        return true;
    }
    if (cc == 66) { put (mixer::kOrgPercOn, value >= 64 ? 1 : 0); return true; }
    if (cc == 65) { put (mixer::kOrgDriveOn, value >= 64 ? 1 : 0); return true; }
    if (! upper) return false; // le reste agit sur la voie du clavier supérieur
    switch (cc)
    {
        case 91: put (mixer::kOrgReverb, value * 100.0 / 127.0); return true;
        case 80: put (mixer::kOrgPercOn, value >= 64 ? 1 : 0); return true;
        case 81: put (mixer::kOrgPercSoft, value >= 64 ? 1 : 0); return true;
        case 82: put (mixer::kOrgPercFast, value >= 64 ? 1 : 0); return true;
        case 83: put (mixer::kOrgPercThird, value >= 64 ? 0 : 1); return true; // haut : 2e harmonique, bas : 3e
        case 92: put (mixer::kOrgVibMode, 1 + std::clamp (value / 23, 0, 5)); return true;
        case 95: put (mixer::kOrgVibLower, (value / 32) == 1 || (value / 32) == 3 ? 1 : 0);
                 put (mixer::kOrgVibUpper, (value / 32) >= 2 ? 1 : 0); return true;
        case 30: put (mixer::kOrgVibLower, value >= 64 ? 1 : 0); return true;
        case 31: put (mixer::kOrgVibUpper, value >= 64 ? 1 : 0); return true;
        case 93: put (mixer::kOrgDriveCharacter, value * 100.0 / 127.0); return true;
        default: return false;
    }
}

bool isOrganParam (ParamId id, int track) noexcept
{
    const ParamId first = mixer::organParam (track, 0);
    return id >= first && id < first + static_cast<ParamId> (mixer::kOrganKinds);
}

std::vector<control::Binding> organControlBindings (const OrganDescription& d, int track)
{
    std::vector<control::Binding> out;
    auto add = [&] (int channel, int cc, int kind, bool invert = false, double lo = 0.0, double hi = 1.0)
    {
        control::Binding b;
        b.source.kind = control::SourceKind::CC;
        b.source.channel = channel;
        b.source.number = cc;
        b.target.param = mixer::organParam (track, kind);
        b.invert = invert;
        b.lo = lo;
        b.hi = hi;
        out.push_back (std::move (b));
    };
    const int upper = d.channel[0], lower = d.channel[1], pedal = d.channel[2];
    for (int i = 0; i < 9; ++i)
    {
        if (upper != 0) add (upper, 70 + i, mixer::kOrgUpper0 + i, true);   // 127 = rentrée, 0 = sortie à fond
        if (lower != 0) add (lower, 70 + i, mixer::kOrgLower0 + i, true);
    }
    if (pedal != 0)
    {
        add (pedal, 70, mixer::kOrgPedal16, true);
        add (pedal, 72, mixer::kOrgPedal8, true);
    }
    // communs à tous les claviers (voie 0 = toutes)
    add (0, 1, mixer::kOrgLeslieSpeed, false, 0.0, 0.5);   // molette : bas = chorale, haut = tremolo
    add (0, 66, mixer::kOrgPercOn);
    add (0, 65, mixer::kOrgDriveOn);
    if (upper != 0)
    {
        add (upper, 91, mixer::kOrgReverb);
        add (upper, 80, mixer::kOrgPercOn);
        add (upper, 81, mixer::kOrgPercSoft);
        add (upper, 82, mixer::kOrgPercFast);
        add (upper, 83, mixer::kOrgPercThird, true);
        add (upper, 92, mixer::kOrgVibMode, false, 1.0 / 6.0, 1.0);   // V1 .. C3
        add (upper, 30, mixer::kOrgVibLower);
        add (upper, 31, mixer::kOrgVibUpper);
        add (upper, 93, mixer::kOrgDriveCharacter);
    }
    return out;
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
    lastFader_ = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxVolume_)));
    lastGain_ = static_cast<float> (sampler::dbToGain (params_.effectiveAt (idxGain_)));
    float l = 1.0f, r = 1.0f;
    mixer::panGains (mixer::PanMode::Balance, static_cast<float> (params_.effectiveAt (idxPan_) / 100.0), l, r);
    lastPanL_ = l;
    lastPanR_ = r;
}

void OrganSynthEngine::setDescription (const OrganDescription& d)
{
    description_ = sanitised (d);
    voices_.collect(); // libère les orgues déjà remplacés par l'audio
    voices_.requestBuild (description_);
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

    // le son lit les valeurs effectives : la modulation de l'hôte (et celle de la matrice) s'y ajoute sans changer les réglages
    const OrganLive live = liveFromParams (params_);
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
}
} // namespace neptune::core::organ
