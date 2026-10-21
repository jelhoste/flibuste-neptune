#include "neptune/core/SampleEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <sndfile.h>

#include "neptune/core/SampleLoader.h"

namespace neptune::core::sampleedit
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

void clampRegion (const Buffer& b, std::int64_t& from, std::int64_t& to) noexcept
{
    const auto n = b.frames();
    if (to < 0 || to > n) to = n;
    from = std::clamp<std::int64_t> (from, 0, to);
}

float mono (const Buffer& b, std::int64_t f) noexcept
{
    float s = 0.0f;
    for (int c = 0; c < b.channels; ++c) s += b.at (f, c);
    return s / static_cast<float> (b.channels);
}

std::uint64_t fnv (std::uint64_t h, const void* p, std::size_t n) noexcept
{
    const auto* d = static_cast<const unsigned char*> (p);
    for (std::size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 1099511628211ull; }
    return h;
}
} // namespace

bool readFile (const std::filesystem::path& path, Buffer& out, std::string* error)
{
    AudioStreamReader reader;
    if (! reader.open (path, error)) return false;
    out.channels = reader.channels();
    out.data.assign (static_cast<std::size_t> (reader.totalFrames()) * static_cast<std::size_t> (out.channels), 0.0f);
    const auto info = probeAudioFile (path);
    out.sampleRate = info.ok && info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    std::int64_t done = 0;
    while (done < reader.totalFrames())
    {
        const auto got = reader.read (out.data.data() + done * out.channels, std::min<std::int64_t> (65536, reader.totalFrames() - done));
        if (got <= 0) break;
        done += got;
    }
    out.data.resize (static_cast<std::size_t> (done) * static_cast<std::size_t> (out.channels));
    return true;
}

bool writeWav (const std::filesystem::path& path, const Buffer& b, std::string* error)
{
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories (path.parent_path(), ec);
    SF_INFO info {};
    info.samplerate = static_cast<int> (std::lround (b.sampleRate));
    info.channels = b.channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (path.string().c_str(), SFM_WRITE, &info);
    if (f == nullptr) { if (error != nullptr) *error = sf_strerror (nullptr); return false; }
    const auto n = b.frames();
    const bool ok = sf_writef_float (f, b.data.data(), n) == n;
    sf_close (f);
    if (! ok && error != nullptr) *error = "write failed";
    return ok;
}

std::filesystem::path derivedPath (const std::filesystem::path& dir, const std::string& sourcePath, const std::string& tag, const Buffer& b)
{
    std::uint64_t h = 1469598103934665603ull;
    h = fnv (h, sourcePath.data(), sourcePath.size());
    h = fnv (h, tag.data(), tag.size());
    h = fnv (h, b.data.data(), b.data.size() * sizeof (float));
    char hex[17];
    std::snprintf (hex, sizeof hex, "%016llx", static_cast<unsigned long long> (h));
    auto stem = std::filesystem::u8path (sourcePath).stem().string();
    if (stem.size() > 40) stem.resize (40);
    return dir / (stem + "-" + tag + "-" + std::string (hex, 8) + ".wav");
}

double toDb (double a) noexcept { return a <= 1e-6 ? -120.0 : 20.0 * std::log10 (a); }

double peakAmplitude (const Buffer& b, std::int64_t from, std::int64_t to) noexcept
{
    clampRegion (b, from, to);
    float p = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (from * b.channels); i < static_cast<std::size_t> (to * b.channels); ++i) p = std::max (p, std::fabs (b.data[i]));
    return p;
}

double rmsAmplitude (const Buffer& b, std::int64_t from, std::int64_t to) noexcept
{
    clampRegion (b, from, to);
    if (to <= from) return 0.0;
    double s = 0.0;
    for (std::size_t i = static_cast<std::size_t> (from * b.channels); i < static_cast<std::size_t> (to * b.channels); ++i) s += static_cast<double> (b.data[i]) * b.data[i];
    return std::sqrt (s / static_cast<double> ((to - from) * b.channels));
}

Buffer trimmed (const Buffer& b, std::int64_t from, std::int64_t to)
{
    clampRegion (b, from, to);
    Buffer out;
    out.channels = b.channels;
    out.sampleRate = b.sampleRate;
    out.data.assign (b.data.begin() + from * b.channels, b.data.begin() + to * b.channels);
    return out;
}

void applyGainDb (Buffer& b, double db) noexcept
{
    const float g = static_cast<float> (std::pow (10.0, db / 20.0));
    for (auto& v : b.data) v *= g;
}

double normalizeGainDb (const Buffer& b, std::int64_t from, std::int64_t to, NormalizeTarget target, double targetDb, bool* limited) noexcept
{
    if (limited != nullptr) *limited = false;
    const double peak = peakAmplitude (b, from, to);
    if (peak <= 1e-6) return 0.0;
    if (target == NormalizeTarget::Peak) return targetDb - toDb (peak);
    const double rms = rmsAmplitude (b, from, to);
    double g = targetDb - toDb (rms);
    const double maxG = -toDb (peak);   // la crête ne dépasse pas 0 dBFS
    if (g > maxG) { g = maxG; if (limited != nullptr) *limited = true; }
    return g;
}

void removeDc (Buffer& b) noexcept
{
    const float r = static_cast<float> (1.0 - 2.0 * kPi * 5.0 / b.sampleRate);
    for (int c = 0; c < b.channels; ++c)
    {
        float xp = 0.0f, yp = 0.0f;
        // amorce au premier échantillon : pas d'impulsion de départ si le fichier commence décalé
        if (b.frames() > 0) xp = b.at (0, c), yp = 0.0f;
        for (std::int64_t i = 0; i < b.frames(); ++i)
        {
            float& v = b.data[static_cast<std::size_t> (i * b.channels + c)];
            const float in = v;
            const float out = in - xp + r * yp + 1e-15f;
            xp = in; yp = out; v = out;
        }
    }
}

double dcOffset (const Buffer& b, std::int64_t from, std::int64_t to) noexcept
{
    clampRegion (b, from, to);
    if (to <= from) return 0.0;
    double worst = 0.0;
    for (int c = 0; c < b.channels; ++c)
    {
        double s = 0.0;
        for (std::int64_t i = from; i < to; ++i) s += b.at (i, c);
        s /= static_cast<double> (to - from);
        if (std::fabs (s) > std::fabs (worst)) worst = s;
    }
    return worst;
}

Silence detectSilence (const Buffer& b, double thresholdDb) noexcept
{
    Silence out;
    const float thr = static_cast<float> (std::pow (10.0, thresholdDb / 20.0));
    const auto n = b.frames();
    auto loud = [&] (std::int64_t f) { for (int c = 0; c < b.channels; ++c) if (std::fabs (b.at (f, c)) >= thr) return true; return false; };
    std::int64_t first = 0;
    while (first < n && ! loud (first)) ++first;
    if (first >= n) return out;
    std::int64_t last = n - 1;
    while (last > first && ! loud (last)) --last;
    out.found = true;
    out.start = first;
    out.end = last + 1;
    return out;
}

std::vector<std::int64_t> detectTransients (const Buffer& b, double sensitivity, double minGapMs)
{
    std::vector<std::int64_t> hits;
    const auto n = b.frames();
    constexpr int kHop = 128, kWin = 512;
    if (n < kWin * 2) return hits;
    const auto blocks = static_cast<std::size_t> ((n - kWin) / kHop);
    std::vector<double> env (blocks);
    for (std::size_t k = 0; k < blocks; ++k)
    {
        double s = 0.0;
        for (int i = 0; i < kWin; ++i) { const double v = mono (b, static_cast<std::int64_t> (k) * kHop + i); s += v * v; }
        env[k] = 10.0 * std::log10 (s / kWin + 1e-10);   // dB
    }
    // flux : montée d'énergie par rapport à la moyenne des blocs précédents (dB)
    std::vector<double> flux (blocks, 0.0);
    constexpr int kBack = 8;
    for (std::size_t k = 1; k < blocks; ++k)
    {
        double ref = 0.0; int cnt = 0;
        for (int j = 1; j <= kBack && static_cast<std::size_t> (j) <= k; ++j) { ref += env[k - static_cast<std::size_t> (j)]; ++cnt; }
        flux[k] = std::max (0.0, env[k] - ref / cnt);
    }
    // seuil en dB : de 18 dB (peu sensible) à 4 dB (très sensible) ; le niveau absolu doit dépasser le plancher du fichier
    const double thr = 18.0 - 14.0 * std::clamp (sensitivity, 0.0, 1.0);
    double maxEnv = -200.0;
    for (const double e : env) maxEnv = std::max (maxEnv, e);
    const double floorDb = maxEnv - 50.0;
    const auto minGap = static_cast<std::int64_t> (minGapMs * b.sampleRate / 1000.0);
    for (std::size_t k = 1; k + 1 < blocks; ++k)
    {
        if (flux[k] < thr || env[k] < floorDb || flux[k] < flux[k - 1] || flux[k] < flux[k + 1]) continue;   // maximum local de flux
        // le début réel du coup : le plus bas niveau du bloc précédent
        const std::int64_t base = static_cast<std::int64_t> (k) * kHop;
        std::int64_t pos = std::max<std::int64_t> (0, base - kHop), best = pos;
        float bestMag = std::numeric_limits<float>::max();
        for (std::int64_t f = pos; f < base + kHop && f < n; ++f)
            if (std::fabs (mono (b, f)) < bestMag && f + kHop / 2 > base) { bestMag = std::fabs (mono (b, f)); best = f; }
        if (! hits.empty() && best - hits.back() < minGap) continue;
        hits.push_back (best);
    }
    return hits;
}

std::int64_t snapToZero (const Buffer& b, std::int64_t frame, std::int64_t radius) noexcept
{
    const auto n = b.frames();
    if (n < 2) return frame;
    frame = std::clamp<std::int64_t> (frame, 0, n - 1);
    auto crossing = [&] (std::int64_t f)
    {
        if (f < 1 || f >= n) return false;
        const float a = mono (b, f - 1), c = mono (b, f);
        return (a <= 0.0f && c > 0.0f) || (a >= 0.0f && c < 0.0f) || c == 0.0f;
    };
    for (std::int64_t d = 0; d <= radius; ++d)
    {
        if (crossing (frame + d)) return frame + d;
        if (crossing (frame - d)) return frame - d;
    }
    return frame;
}

std::vector<LoopCandidate> findLoopCandidates (const Buffer& b, std::int64_t from, std::int64_t to, int maxCount, double minLengthMs)
{
    std::vector<LoopCandidate> out;
    clampRegion (b, from, to);
    const std::int64_t len = to - from;
    const std::int64_t minLen = std::max<std::int64_t> (64, static_cast<std::int64_t> (minLengthMs * b.sampleRate / 1000.0));
    const std::int64_t win = std::clamp<std::int64_t> (static_cast<std::int64_t> (b.sampleRate * 0.02), 256, 4096);   // ~20 ms
    if (len < minLen + 2 * win || maxCount < 1) return out;

    // points de départ : passages par zéro montants de la première moitié, fin : de la dernière moitié ; échantillonnés pour borner le coût
    std::vector<std::int64_t> starts, ends;
    auto up = [&] (std::int64_t f) { return f > 0 && mono (b, f - 1) <= 0.0f && mono (b, f) > 0.0f; };
    for (std::int64_t f = from + win; f < from + len / 2; ++f) if (up (f)) starts.push_back (f);
    for (std::int64_t f = from + len / 2; f < to - 1; ++f) if (up (f)) ends.push_back (f);
    auto thin = [] (std::vector<std::int64_t>& v, std::size_t cap)
    {
        if (v.size() <= cap) return;
        std::vector<std::int64_t> t;
        for (std::size_t i = 0; i < cap; ++i) t.push_back (v[i * v.size() / cap]);
        v.swap (t);
    };
    thin (starts, 96);
    thin (ends, 192);

    // énergie de référence pour normaliser l'erreur
    const double refEnergy = std::max (1e-9, rmsAmplitude (b, from, to));
    std::vector<LoopCandidate> all;
    for (const auto s : starts)
        for (const auto e : ends)
        {
            if (e - s < minLen) continue;
            // raccord : ce qui précède la fin doit ressembler à ce qui précède le début (le saut e -> s doit être continu)
            double err = 0.0;
            for (std::int64_t k = 1; k <= win; ++k)
            {
                const double d = static_cast<double> (mono (b, e - k)) - static_cast<double> (mono (b, s - k));
                err += d * d;
            }
            const double rmse = std::sqrt (err / static_cast<double> (win)) / refEnergy;
            all.push_back ({ s, e, 1.0 / (1.0 + 4.0 * rmse) });
        }
    std::sort (all.begin(), all.end(), [] (const LoopCandidate& a, const LoopCandidate& c) { return a.score > c.score; });
    const std::int64_t spacing = std::max<std::int64_t> (win, len / 50);
    for (const auto& c : all)
    {
        bool near = false;
        for (const auto& o : out) near = near || (std::llabs (o.start - c.start) < spacing && std::llabs (o.end - c.end) < spacing);
        if (near) continue;
        out.push_back (c);
        if (static_cast<int> (out.size()) >= maxCount) break;
    }
    return out;
}
} // namespace neptune::core::sampleedit
