#include "neptune/core/ZoneSlots.h"

#include <algorithm>
#include <cmath>

namespace neptune::core
{
const char* toString (SlotParam p) noexcept
{
    switch (p)
    {
        case SlotParam::Attack: return "attack";
        case SlotParam::Decay: return "decay";
        case SlotParam::Sustain: return "sustain";
        case SlotParam::Release: return "release";
        case SlotParam::Level: return "level";
        case SlotParam::Tune: return "tune";
        default: return "none";
    }
}

bool slotParamFromString (std::string_view s, SlotParam& out)
{
    for (int i = 0; i < kSlotParams; ++i)
        if (s == toString (static_cast<SlotParam> (i))) { out = static_cast<SlotParam> (i); return true; }
    return false;
}

SlotMods resolveSlotMods (const SlotRuntimes& slots, const float* amounts, int zoneSourceIndex, int zoneGroupId) noexcept
{
    SlotMods m;
    for (int i = 0; i < kZoneSlots; ++i)
    {
        const auto& s = slots[static_cast<std::size_t> (i)];
        if (s.param == SlotParam::None) continue;
        const bool hit = s.isGroup ? (s.groupId != 0 && s.groupId == zoneGroupId) : (s.zoneIndex >= 0 && s.zoneIndex == zoneSourceIndex);
        if (! hit) continue;
        const float a = std::clamp (amounts[i], -1.0f, 1.0f);
        switch (s.param)
        {
            case SlotParam::Attack: m.attack *= std::pow (10.0f, a); break;
            case SlotParam::Decay: m.decay *= std::pow (10.0f, a); break;
            case SlotParam::Release: m.release *= std::pow (10.0f, a); break;
            case SlotParam::Sustain: m.sustain *= std::max (0.0f, 1.0f + a); break;
            case SlotParam::Level: m.gain *= std::pow (10.0f, a * 24.0f / 20.0f); break;
            case SlotParam::Tune: m.tuneCents += a * 100.0f; break;
            default: break;
        }
        m.any = true;
    }
    return m;
}
} // namespace neptune::core
