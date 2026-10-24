#include "neptune/core/FacadeTransfer.h"

#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerParams.h"

namespace neptune::core::facade
{
namespace
{
constexpr int kTracks = mixer::numTracks;
constexpr ParamId kSamplerFirst = 30;   // décalage des réglages d'instrument dans le bloc d'une piste (voir mixer::samplerParam)
constexpr ParamId kSamplerCount = mixer::kSamplerKinds;

bool inRange (ParamId id, ParamId first, ParamId count) noexcept { return id >= first && id < first + count; }
} // namespace

ParamId remapParamTrack (ParamId id, int fromTrack, int toTrack) noexcept
{
    if (fromTrack < 0 || fromTrack >= kTracks || toTrack < 0 || toTrack >= kTracks || fromTrack == toTrack) return id;

    // réglages d'instrument de la piste 0 : identifiants historiques 100..106
    if (fromTrack == 0 && inRange (id, sampler::gain, kSamplerCount))
        return mixer::samplerParam (toTrack, static_cast<mixer::SamplerKind> (id - sampler::gain));

    const ParamId blockFrom = probe::trackParam (fromTrack, 0);
    if (inRange (id, blockFrom, probe::trackStride))
    {
        const ParamId offset = id - blockFrom;
        if (toTrack == 0 && fromTrack != 0 && inRange (offset, kSamplerFirst, kSamplerCount))
            return sampler::gain + (offset - kSamplerFirst);   // vers la piste 0 : retour aux identifiants historiques
        return probe::trackParam (toTrack, offset);
    }

    const auto shifted = [id, fromTrack, toTrack] (ParamId base, ParamId stride) -> ParamId
    {
        const ParamId first = base + stride * static_cast<ParamId> (fromTrack);
        if (!inRange (id, first, stride)) return id;
        return base + stride * static_cast<ParamId> (toTrack) + (id - first);
    };
    if (const ParamId r = shifted (mixer::organParamBase, probe::trackStride); r != id) return r;
    if (const ParamId r = shifted (mixer::zoneSlotBase, 16); r != id) return r;
    if (const ParamId r = shifted (mixer::modulatorBase, 16); r != id) return r;
    return id;
}

void remapTrack (Facade& f, int fromTrack, int toTrack)
{
    for (auto& w : f.widgets)
    {
        if (w.param != 0) w.param = remapParamTrack (w.param, fromTrack, toTrack);
        for (auto& [id, value] : w.snapshot) id = remapParamTrack (id, fromTrack, toTrack);
    }
}

std::vector<ParamId> instrumentParamsOfTrack (const ParameterSet& set, int track)
{
    std::vector<ParamId> out;
    if (track < 0 || track >= kTracks) return out;
    const ParamId block = probe::trackParam (track, 0);
    const ParamId organ = mixer::organParam (track, 0);
    const ParamId slots = mixer::zoneSlotParam (track, 0);
    const ParamId mods = mixer::modulatorParam (track, 0);
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        const auto& spec = set.spec (i);
        if (spec.hidden) continue;
        const ParamId id = spec.id;
        const bool legacySampler = track == 0 && inRange (id, sampler::gain, kSamplerCount);
        const bool blockInstrument = inRange (id, block, probe::trackStride) && id - block >= kSamplerFirst;   // les décalages 0..29 sont ceux de la bande
        if (legacySampler || blockInstrument || inRange (id, organ, probe::trackStride) || inRange (id, slots, 16) || inRange (id, mods, 16)) out.push_back (id);
    }
    return out;
}

void forEachImagePath (Facade& f, const std::function<void (std::string&)>& visit)
{
    if (! f.image.empty()) visit (f.image);
    for (auto& s : f.styles)
        for (const auto& role : imageRoles())
            if (! (s.*(role.field)).empty()) visit (s.*(role.field));
}
} // namespace neptune::core::facade
