#pragma once
// Lecture interpolée d'un tampon circulaire de retard (delay, chorus). `pos` est l'indice de la PROCHAINE écriture : un retard de d échantillons lit
// l'échantillon écrit d échantillons plus tôt. Hermite à quatre points (d >= 3) ou linéaire (d >= 1).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neptune::core
{
inline float readHermite (const std::vector<float>& b, std::size_t pos, double d) noexcept
{
    const auto size = static_cast<std::int64_t> (b.size());
    const double rp = static_cast<double> (pos) - d;
    const double fl = std::floor (rp);
    const auto frac = static_cast<float> (rp - fl);
    auto at = [&] (std::int64_t i) { return b[static_cast<std::size_t> (((i % size) + size) % size)]; };
    const auto i1 = static_cast<std::int64_t> (fl);
    const float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

inline float readLinear (const std::vector<float>& b, std::size_t pos, double d) noexcept
{
    const auto size = static_cast<std::int64_t> (b.size());
    const double rp = static_cast<double> (pos) - d;
    const double fl = std::floor (rp);
    const auto frac = static_cast<float> (rp - fl);
    auto at = [&] (std::int64_t i) { return b[static_cast<std::size_t> (((i % size) + size) % size)]; };
    const auto i1 = static_cast<std::int64_t> (fl);
    return at (i1) + frac * (at (i1 + 1) - at (i1));
}
} // namespace neptune::core
