#include "neptune/core/Profile.h"

namespace neptune::core
{
const char* toString (PerformanceProfile p) noexcept { return p == PerformanceProfile::Studio ? "studio" : "live"; }

std::optional<PerformanceProfile> profileFromString (std::string_view s)
{
    if (s == "live") return PerformanceProfile::Live;
    if (s == "studio") return PerformanceProfile::Studio;
    return std::nullopt;
}

SharedProfile& SharedProfile::process() noexcept
{
    static SharedProfile instance;
    return instance;
}

bool SharedProfile::initialiseOnce (PerformanceProfile initial) noexcept
{
    bool expected = false;
    if (! initialised_.compare_exchange_strong (expected, true)) return false;
    value_.store (static_cast<int> (initial), std::memory_order_relaxed);
    return true;
}
} // namespace neptune::core
