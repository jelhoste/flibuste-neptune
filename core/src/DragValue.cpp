#include "neptune/core/DragValue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace neptune::core::dragvalue
{
namespace
{
bool usesLog (const Range& r) noexcept { return r.log && r.min > 0.0 && r.max > r.min; }
}

double clamp (const Range& r, double value) noexcept
{
    if (! std::isfinite (value)) value = r.min;
    double v = std::clamp (value, std::min (r.min, r.max), std::max (r.min, r.max));
    if (r.step > 0.0)
    {
        v = r.min + std::round ((v - r.min) / r.step) * r.step;
        v = std::clamp (v, std::min (r.min, r.max), std::max (r.min, r.max));
    }
    return v;
}

double toNormalised (const Range& r, double value) noexcept
{
    if (r.max == r.min) return 0.0;
    const double v = std::clamp (value, std::min (r.min, r.max), std::max (r.min, r.max));
    const double n = usesLog (r) ? std::log (v / r.min) / std::log (r.max / r.min) : (v - r.min) / (r.max - r.min);
    return std::clamp (n, 0.0, 1.0);
}

double fromNormalised (const Range& r, double n) noexcept
{
    n = std::clamp (std::isfinite (n) ? n : 0.0, 0.0, 1.0);
    const double v = usesLog (r) ? r.min * std::pow (r.max / r.min, n) : r.min + n * (r.max - r.min);
    return clamp (r, v);
}

std::string format (const Range& r, double value)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, "%.*f", std::clamp (r.decimals, 0, 6), clamp (r, value));
    std::string s = buf;
    if (s == "-0" || (s.size() > 1 && s.compare (0, 2, "-0") == 0 && s.find_first_not_of ("-0.") == std::string::npos)) s.erase (0, 1);   // pas de « -0.00 »
    return s;
}

bool parse (const Range& r, const std::string& text, double& out)
{
    std::string t;
    for (const char c : text) t.push_back (c == ',' ? '.' : c);
    const auto first = t.find_first_not_of (" \t\r\n");
    if (first == std::string::npos) return false;
    t = t.substr (first, t.find_last_not_of (" \t\r\n") - first + 1);
    const char* begin = t.c_str();
    char* end = nullptr;
    const double v = std::strtod (begin, &end);
    if (end == begin || ! std::isfinite (v)) return false;
    // ce qui suit le nombre doit être une unité (lettres, %, µ...), pas un autre chiffre ou un signe
    for (const char* p = end; *p != '\0'; ++p)
        if ((*p >= '0' && *p <= '9') || *p == '.' || *p == '+' || *p == '-') return false;
    out = clamp (r, v);
    return true;
}

double dragged (const Range& r, double startNormalised, double dx, double pixelsFullRange, bool fine) noexcept
{
    const double px = std::max (20.0, pixelsFullRange);
    const double n = startNormalised + dx / px * (fine ? kFineRatio : 1.0);
    return fromNormalised (r, n);
}

double wheel (const Range& r, double value, int notches, bool fine) noexcept
{
    if (notches == 0) return clamp (r, value);
    if (r.step > 0.0) return clamp (r, value + notches * r.step * (fine ? kFineRatio : 1.0));
    const double n = toNormalised (r, value) + notches * 0.01 * (fine ? kFineRatio : 1.0);
    return fromNormalised (r, n);
}
} // namespace neptune::core::dragvalue
