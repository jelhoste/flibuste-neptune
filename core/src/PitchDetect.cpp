#include "neptune/core/PitchDetect.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "neptune/core/SampleLoader.h"

namespace neptune::core
{
PitchResult detectPitch (const sampleedit::Buffer& b, std::int64_t from, std::int64_t to)
{
    PitchResult out;
    const std::int64_t n = b.frames();
    if (to < 0 || to > n) to = n;
    from = std::clamp<std::int64_t> (from, 0, to);
    const double sr = b.sampleRate > 0.0 ? b.sampleRate : 44100.0;
    const std::int64_t maxTau = static_cast<std::int64_t> (sr / 27.5);    // La0
    const std::int64_t minTau = std::max<std::int64_t> (4, static_cast<std::int64_t> (sr / 4200.0));
    const std::int64_t win = std::max<std::int64_t> (2048, 2 * maxTau);   // fenêtre de comparaison
    if (to - from < win + maxTau + 16) return out;

    // mélange mono
    const std::int64_t span = to - from;
    std::vector<float> x (static_cast<std::size_t> (span));
    for (std::int64_t i = 0; i < span; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < b.channels; ++c) m += b.at (from + i, c);
        x[static_cast<std::size_t> (i)] = m / static_cast<float> (b.channels);
    }
    // fenêtre la plus forte, après les 20 premières ms (l'attaque n'a pas de hauteur)
    const std::int64_t skip = std::min<std::int64_t> (static_cast<std::int64_t> (sr * 0.02), span / 4);
    const std::int64_t need = win + maxTau;
    std::int64_t bestStart = skip;
    double bestEnergy = -1.0;
    const std::int64_t step = std::max<std::int64_t> (256, win / 8);
    for (std::int64_t s = skip; s + need <= span; s += step)
    {
        double e = 0.0;
        for (std::int64_t i = 0; i < win; i += 4) e += static_cast<double> (x[static_cast<std::size_t> (s + i)]) * x[static_cast<std::size_t> (s + i)];
        if (e > bestEnergy) { bestEnergy = e; bestStart = s; }
    }
    if (bestEnergy < 1.0e-6) return out;

    // YIN : différence, moyenne cumulée normalisée, seuil absolu, interpolation parabolique
    const std::int64_t tauMax = std::min<std::int64_t> (maxTau, span - bestStart - win - 1);
    std::vector<double> d (static_cast<std::size_t> (tauMax + 2), 0.0), dn (d.size(), 1.0);
    for (std::int64_t tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (std::int64_t i = 0; i < win; ++i)
        {
            const double diff = static_cast<double> (x[static_cast<std::size_t> (bestStart + i)]) - static_cast<double> (x[static_cast<std::size_t> (bestStart + i + tau)]);
            sum += diff * diff;
        }
        d[static_cast<std::size_t> (tau)] = sum;
    }
    double running = 0.0;
    for (std::int64_t tau = 1; tau <= tauMax; ++tau)
    {
        running += d[static_cast<std::size_t> (tau)];
        dn[static_cast<std::size_t> (tau)] = running > 0.0 ? d[static_cast<std::size_t> (tau)] * static_cast<double> (tau) / running : 1.0;
    }
    constexpr double kThreshold = 0.15;
    std::int64_t tauEst = -1;
    for (std::int64_t tau = minTau; tau < tauMax; ++tau)
    {
        if (dn[static_cast<std::size_t> (tau)] < kThreshold)
        {
            while (tau + 1 < tauMax && dn[static_cast<std::size_t> (tau + 1)] < dn[static_cast<std::size_t> (tau)]) ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0) return out;   // aucun creux franc : indéterminé
    double tauRefined = static_cast<double> (tauEst);
    if (tauEst > 1 && tauEst < tauMax)
    {
        const double y0 = dn[static_cast<std::size_t> (tauEst - 1)], y1 = dn[static_cast<std::size_t> (tauEst)], y2 = dn[static_cast<std::size_t> (tauEst + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        if (std::fabs (den) > 1e-12) tauRefined += 0.5 * (y0 - y2) / den;
    }
    const double conf = std::clamp (1.0 - dn[static_cast<std::size_t> (tauEst)], 0.0, 1.0);
    out.hz = sr / tauRefined;
    out.midi = 69.0 + 12.0 * std::log2 (out.hz / 440.0);
    out.note = std::clamp (static_cast<int> (std::lround (out.midi)), 0, 127);
    out.cents = (out.midi - static_cast<double> (out.note)) * 100.0;
    out.confidence = conf;
    out.found = conf >= 0.8 && out.midi >= 12.0 && out.midi <= 120.0;
    return out;
}

PitchResult detectPitchOfFile (const std::filesystem::path& path)
{
    AudioStreamReader reader;
    if (! reader.open (path)) return {};
    const auto info = probeAudioFile (path);
    sampleedit::Buffer b;
    b.channels = reader.channels();
    b.sampleRate = info.ok && info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    const std::int64_t frames = std::min<std::int64_t> (reader.totalFrames(), static_cast<std::int64_t> (b.sampleRate * 3.0));
    b.data.resize (static_cast<std::size_t> (frames * b.channels));
    const auto got = reader.read (b.data.data(), frames);
    b.data.resize (static_cast<std::size_t> (std::max<std::int64_t> (0, got) * b.channels));
    return detectPitch (b);
}
} // namespace neptune::core
