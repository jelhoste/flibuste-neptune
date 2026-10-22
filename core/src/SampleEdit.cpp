#include "neptune/core/SampleEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

#include <sndfile.h>

#include "neptune/core/SampleLoader.h"
#include "neptune/core/SpectrumAnalyzer.h"

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
   #if defined (_WIN32)
    SNDFILE* f = sf_wchar_open (path.c_str(), SFM_WRITE, &info);   // chemin Unicode (dossier utilisateur accentué)
   #else
    SNDFILE* f = sf_open (path.c_str(), SFM_WRITE, &info);
   #endif
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

StableRegion findStableRegion (const Buffer& b, std::int64_t from, std::int64_t to, double minLengthMs)
{
    StableRegion out;
    clampRegion (b, from, to);
    const std::int64_t hop = std::max<std::int64_t> (64, static_cast<std::int64_t> (b.sampleRate * 0.010)), win = 2 * hop;
    if (to - from < win * 4) return out;
    const auto blocks = static_cast<std::size_t> ((to - from - win) / hop);
    std::vector<double> level (blocks), zcr (blocks);
    double maxLevel = -200.0;
    for (std::size_t k = 0; k < blocks; ++k)
    {
        const std::int64_t a = from + static_cast<std::int64_t> (k) * hop;
        double e = 0.0; int zc = 0; float prev = mono (b, a);
        for (std::int64_t i = 0; i < win; ++i)
        {
            const float v = mono (b, a + i);
            e += static_cast<double> (v) * v;
            if ((prev <= 0.0f) != (v <= 0.0f)) ++zc;
            prev = v;
        }
        level[k] = 10.0 * std::log10 (e / static_cast<double> (win) + 1e-12);
        zcr[k] = static_cast<double> (zc) / static_cast<double> (win);
        maxLevel = std::max (maxLevel, level[k]);
    }
    // références : médianes sur les blocs assez forts
    std::vector<double> ls, zs;
    for (std::size_t k = 0; k < blocks; ++k) if (level[k] > maxLevel - 30.0) { ls.push_back (level[k]); zs.push_back (zcr[k]); }
    if (ls.size() < 4) return out;
    auto median = [] (std::vector<double> v) { std::nth_element (v.begin(), v.begin() + static_cast<long> (v.size() / 2), v.end()); return v[v.size() / 2]; };
    const double medL = median (ls), medZ = median (zs);
    std::vector<char> ok (blocks, 0);
    for (std::size_t k = 0; k < blocks; ++k)
        ok[k] = std::fabs (level[k] - medL) <= 3.0 && (medZ < 1e-4 || std::fabs (zcr[k] - medZ) <= 0.35 * medZ + 0.004) ? 1 : 0;
    // plus longue plage, en tolérant deux blocs douteux de suite
    std::size_t bestA = 0, bestLen = 0;
    for (std::size_t k = 0; k < blocks;)
    {
        if (! ok[k]) { ++k; continue; }
        std::size_t last = k, j = k;
        while (j < blocks)
        {
            if (ok[j]) last = j;
            else if (j - last > 2) break;
            ++j;
        }
        if (last - k + 1 > bestLen) { bestLen = last - k + 1; bestA = k; }
        k = last + 1;
    }
    const std::int64_t start = from + static_cast<std::int64_t> (bestA) * hop + hop / 2, end = from + static_cast<std::int64_t> (bestA + bestLen - 1) * hop + hop;
    if (bestLen == 0 || static_cast<double> (end - start) < minLengthMs * b.sampleRate / 1000.0) return out;
    out.found = true; out.start = start; out.end = std::min (end, to);
    return out;
}

double detectPeriod (const Buffer& b, std::int64_t from, std::int64_t to) noexcept
{
    clampRegion (b, from, to);
    const std::int64_t seg = std::min<std::int64_t> (8192, to - from);
    const std::int64_t minLag = std::max<std::int64_t> (8, static_cast<std::int64_t> (b.sampleRate / 2000.0));
    const std::int64_t maxLag = std::min<std::int64_t> (static_cast<std::int64_t> (b.sampleRate / 40.0), seg / 2);
    if (maxLag <= minLag + 2) return 0.0;
    const std::int64_t a = from + (to - from - seg) / 2;
    std::vector<float> x (static_cast<std::size_t> (seg));
    double mean = 0.0;
    for (std::int64_t i = 0; i < seg; ++i) { x[static_cast<std::size_t> (i)] = mono (b, a + i); mean += x[static_cast<std::size_t> (i)]; }
    mean /= static_cast<double> (seg);
    for (auto& v : x) v -= static_cast<float> (mean);
    const std::int64_t n = seg - maxLag;   // fenêtre de comparaison commune à tous les retards
    std::vector<double> r (static_cast<std::size_t> (maxLag + 2), 0.0);
    double e0 = 0.0;
    for (std::int64_t i = 0; i < n; ++i) e0 += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
    if (e0 < 1e-9) return 0.0;
    for (std::int64_t lag = minLag - 1; lag <= maxLag + 1 && lag < seg; ++lag)
    {
        double c = 0.0, e1 = 0.0;
        for (std::int64_t i = 0; i < n; ++i) { c += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i + lag)]; e1 += static_cast<double> (x[static_cast<std::size_t> (i + lag)]) * x[static_cast<std::size_t> (i + lag)]; }
        r[static_cast<std::size_t> (lag)] = c / std::sqrt (e0 * e1 + 1e-12);
    }
    // premier maximum local élevé : le plus petit retard dont la corrélation est proche du meilleur (évite de prendre un multiple de la période)
    double best = 0.0;
    for (std::int64_t lag = minLag; lag <= maxLag; ++lag) best = std::max (best, r[static_cast<std::size_t> (lag)]);
    if (best < 0.6) return 0.0;
    for (std::int64_t lag = minLag; lag <= maxLag; ++lag)
    {
        const double v = r[static_cast<std::size_t> (lag)];
        if (v >= 0.9 * best && v >= r[static_cast<std::size_t> (lag - 1)] && v >= r[static_cast<std::size_t> (lag + 1)])
        {
            const double y0 = r[static_cast<std::size_t> (lag - 1)], y1 = v, y2 = r[static_cast<std::size_t> (lag + 1)];
            const double den = y0 - 2.0 * y1 + y2;
            return static_cast<double> (lag) + (std::fabs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);   // interpolation parabolique
        }
    }
    return 0.0;
}

namespace
{
// Descripteur de ce qui précède `pos` : spectre en bandes (dB) et niveau (dB), sur ~2048 échantillons.
struct Descriptor { std::vector<float> bands; double levelDb = -120.0; bool ok = false; };

Descriptor describeBefore (const Buffer& b, std::int64_t pos, const Fft& fft)
{
    Descriptor d;
    const std::int64_t n = static_cast<std::int64_t> (fft.size());
    if (pos < n) return d;
    std::vector<float> re (static_cast<std::size_t> (n)), im (static_cast<std::size_t> (n), 0.0f);
    double e = 0.0;
    for (std::int64_t i = 0; i < n; ++i)
    {
        const float v = mono (b, pos - n + i);
        e += static_cast<double> (v) * v;
        re[static_cast<std::size_t> (i)] = v * static_cast<float> (0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n)));
    }
    d.levelDb = 10.0 * std::log10 (e / static_cast<double> (n) + 1e-12);
    fft.transform (re.data(), im.data());
    constexpr int kBands = 24;
    d.bands.assign (kBands, -120.0f);
    const double binHz = b.sampleRate / static_cast<double>(n);
    for (int k = 0; k < kBands; ++k)
    {
        const double lo = 80.0 * std::pow (10000.0 / 80.0, static_cast<double> (k) / kBands), hi = 80.0 * std::pow (10000.0 / 80.0, static_cast<double> (k + 1) / kBands);
        const auto i0 = std::max<std::int64_t> (1, static_cast<std::int64_t> (lo / binHz)), i1 = std::min<std::int64_t> (n / 2 - 1, std::max<std::int64_t> (i0, static_cast<std::int64_t> (hi / binHz)));
        double acc = 0.0;
        for (std::int64_t i = i0; i <= i1; ++i) acc += static_cast<double> (re[static_cast<std::size_t> (i)]) * re[static_cast<std::size_t> (i)] + static_cast<double> (im[static_cast<std::size_t> (i)]) * im[static_cast<std::size_t> (i)];
        d.bands[static_cast<std::size_t> (k)] = static_cast<float> (10.0 * std::log10 (acc / static_cast<double> (i1 - i0 + 1) + 1e-12));
    }
    d.ok = true;
    return d;
}
} // namespace

std::vector<LoopCandidate> findLoopCandidates (const Buffer& b, std::int64_t from, std::int64_t to, int maxCount, double minLengthMs, const LoopSearchOptions& opt)
{
    std::vector<LoopCandidate> out;
    clampRegion (b, from, to);
    const std::int64_t len = to - from;
    const std::int64_t minLen = std::max<std::int64_t> (64, static_cast<std::int64_t> (minLengthMs * b.sampleRate / 1000.0));
    const std::int64_t win = std::clamp<std::int64_t> (static_cast<std::int64_t> (b.sampleRate * 0.02), 256, 4096);   // ~20 ms
    if (len < minLen + 2 * win || maxCount < 1) return out;

    // zone de recherche : la plage stable (sinon toute la région)
    std::int64_t lo = from, hi = to;
    if (opt.stableOnly)
    {
        const auto st = findStableRegion (b, from, to, minLengthMs);
        if (st.found) { lo = st.start; hi = st.end; }
    }
    if (hi - lo < minLen + 2 * win) { lo = from; hi = to; }

    auto up = [&] (std::int64_t f) { return f > 0 && mono (b, f - 1) <= 0.0f && mono (b, f) > 0.0f; };
    auto thin = [] (std::vector<std::int64_t>& v, std::size_t cap)
    {
        if (v.size() <= cap) return;
        std::vector<std::int64_t> t;
        for (std::size_t i = 0; i < cap; ++i) t.push_back (v[i * v.size() / cap]);
        v.swap (t);
    };
    const std::int64_t radius = opt.aroundCursor && opt.cursor >= 0 ? static_cast<std::int64_t> (opt.radiusMs * b.sampleRate / 1000.0) : -1;
    auto nearCursor = [&] (std::int64_t s) { return radius < 0 || std::llabs (s - opt.cursor) <= radius; };

    // débuts : passages par zéro montants de la première moitié de la zone (autour du curseur si demandé)
    std::vector<std::int64_t> starts;
    for (std::int64_t f = lo + win; f < lo + (hi - lo) / 2 + (radius >= 0 ? (hi - lo) / 4 : 0); ++f) if (up (f) && nearCursor (f)) starts.push_back (f);
    thin (starts, radius >= 0 ? 160 : 96);

    const double period = opt.period ? detectPeriod (b, lo, hi) : 0.0;
    const double refEnergy = std::max (1e-9, rmsAmplitude (b, from, to));
    const Fft fft (11);
    std::map<std::int64_t, Descriptor> cache;
    auto descriptor = [&] (std::int64_t p) -> const Descriptor& { auto it = cache.find (p); if (it == cache.end()) it = cache.emplace (p, describeBefore (b, p, fft)).first; return it->second; };

    std::vector<LoopCandidate> all;
    auto consider = [&] (std::int64_t s, std::int64_t e)
    {
        if (e - s < minLen || e >= hi + 1 || e <= s) return;
        double err = 0.0;
        for (std::int64_t k = 1; k <= win; ++k)
        {
            const double d = static_cast<double> (mono (b, e - k)) - static_cast<double> (mono (b, s - k));
            err += d * d;
        }
        const double rmse = std::sqrt (err / static_cast<double> (win)) / refEnergy;
        double score = 1.0 / (1.0 + 4.0 * rmse);
        if (opt.spectral)
        {
            const auto& ds = descriptor (s);
            const auto& de = descriptor (e);
            if (ds.ok && de.ok)
            {
                double dist = 0.0;
                for (std::size_t k = 0; k < ds.bands.size(); ++k) dist += std::fabs (static_cast<double> (ds.bands[k]) - de.bands[k]);
                dist /= static_cast<double> (ds.bands.size());
                const double spec = 1.0 / (1.0 + dist / 6.0), lvl = 1.0 / (1.0 + std::fabs (ds.levelDb - de.levelDb) / 1.5);
                score = std::pow (score, 0.5) * std::pow (spec, 0.3) * std::pow (lvl, 0.2);
            }
        }
        score *= 1.0 + 0.05 * static_cast<double> (e - s) / static_cast<double> (std::max<std::int64_t> (1, len));   // à qualité égale, la boucle la plus longue répète moins
        all.push_back ({ s, e, std::min (1.0, score) });
    };

    if (period > 0.0)
    {
        // fins : début + k périodes, recalées sur le passage par zéro montant le plus proche
        for (const auto s : starts)
        {
            std::vector<std::int64_t> ends;
            for (int k = 1; k < 4000; ++k)
            {
                const std::int64_t target = s + static_cast<std::int64_t> (std::llround (period * k));
                if (target >= hi) break;
                if (target - s < minLen) continue;
                std::int64_t best = -1;
                const std::int64_t reach = std::max<std::int64_t> (2, static_cast<std::int64_t> (period / 4.0));
                for (std::int64_t d = 0; d <= reach && best < 0; ++d) { if (up (target + d)) best = target + d; else if (up (target - d)) best = target - d; }
                if (best >= 0) ends.push_back (best);
            }
            thin (ends, 12);
            for (const auto e : ends) consider (s, e);
        }
    }
    else
    {
        std::vector<std::int64_t> ends;
        for (std::int64_t f = lo + (hi - lo) / 2; f < hi - 1; ++f) if (up (f)) ends.push_back (f);
        thin (ends, 192);
        for (const auto s : starts)
            for (const auto e : ends) consider (s, e);
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
