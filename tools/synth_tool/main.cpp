// neptune_synth_tool : rendu hors ligne et analyse du synthé soustractif de Neptune (AnalogSynth), sans Reaper.
//   params                       liste des paramètres que peut porter un preset (clé, nom, unité, plage, défaut)
//   waves                        les formes d'onde disponibles
//   presets                      les presets livrés avec le plugin ; `export N fichier` en écrit un au format .flipreset
//   validate preset.flipreset    vérifie un preset (format, clés connues, valeurs dans les plages)
//   render ...                   joue des notes avec un preset et écrit un WAV
//   analyse fichier.wav          hauteur, enveloppe, harmoniques, brillance
//   compare a.wav b.wav          distance entre deux sons (hauteur, enveloppe, harmoniques, spectre)
// Le rendu utilise le moteur même du plugin : ce qu'il produit est ce qu'on entend dans AnalogSynth et dans une piste Synthé de Neptune.
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <sndfile.h>

#include "neptune/core/AnalogSynth.h"
#include "neptune/core/WaveCatalog.h"

using namespace neptune::core;
namespace as = neptune::core::analogsynth;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ fichiers
bool readText (const std::string& path, std::string& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double rate)
{
    SF_INFO info {};
    info.samplerate = static_cast<int> (rate);
    info.channels = 2;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (path.c_str(), SFM_WRITE, &info);
    if (f == nullptr) return false;
    std::vector<float> inter (l.size() * 2);
    for (std::size_t i = 0; i < l.size(); ++i) { inter[2 * i] = l[i]; inter[2 * i + 1] = r[i]; }
    sf_writef_float (f, inter.data(), static_cast<sf_count_t> (l.size()));
    sf_close (f);
    return true;
}

bool readMono (const std::string& path, std::vector<float>& mono, double& rate)
{
    SF_INFO info {};
    SNDFILE* f = sf_open (path.c_str(), SFM_READ, &info);
    if (f == nullptr) return false;
    std::vector<float> buf (static_cast<std::size_t> (info.frames) * static_cast<std::size_t> (info.channels));
    const auto got = sf_readf_float (f, buf.data(), info.frames);
    sf_close (f);
    mono.assign (static_cast<std::size_t> (got), 0.0f);
    for (sf_count_t i = 0; i < got; ++i)
    {
        double s = 0.0;
        for (int c = 0; c < info.channels; ++c) s += buf[static_cast<std::size_t> (i) * static_cast<std::size_t> (info.channels) + static_cast<std::size_t> (c)];
        mono[static_cast<std::size_t> (i)] = static_cast<float> (s / info.channels);
    }
    rate = info.samplerate;
    return got > 0;
}

// ------------------------------------------------------------------ outils numériques
double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }

void fft (std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / static_cast<double> (len);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Spectre d'amplitude (n/2 + 1 valeurs) d'une fenêtre de Hann de n échantillons prise à partir de `start` (complétée par du silence).
std::vector<double> magnitude (const std::vector<float>& x, std::size_t start, std::size_t n)
{
    std::vector<std::complex<double>> a (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n - 1));
        a[i] = (start + i < x.size() ? x[start + i] : 0.0f) * w;
    }
    fft (a);
    std::vector<double> m (n / 2 + 1);
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = std::abs (a[i]) * 2.0 / static_cast<double> (n) * 2.0;   // x2 : compense la fenêtre de Hann (gain moyen 0,5)
    return m;
}

struct Analysis
{
    double rate = 48000.0, seconds = 0.0, peakDb = -200.0, rmsDb = -200.0;
    double onset = 0.0, peakTime = 0.0, attack10to90 = 0.0, tailEnd = 0.0;
    std::vector<std::pair<double, double>> envAfterOnset;   // (secondes après le début, niveau en dB sous le pic)
    std::vector<double> env;                                // RMS en dB, un point toutes les 10 ms
    double f0 = 0.0, f0Confidence = 0.0;
    std::vector<double> harmonicsDb;                        // harmoniques 1..N en dB relatifs à la plus forte
    double centroid = 0.0, rolloff85 = 0.0, noisiness = 0.0;
    std::vector<double> centroidOverTime;                   // une valeur par 100 ms
    std::vector<double> bandDb;                             // 24 bandes log de 50 Hz à 16 kHz, niveaux relatifs
};

constexpr int kBands = 24, kHarm = 24;
constexpr double kHop = 0.010;

double midiOf (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

double estimateF0 (const std::vector<float>& x, std::size_t start, double rate, double& confidence)
{
    const std::size_t n = 4096;
    if (start + n > x.size()) start = x.size() > n ? x.size() - n : 0;
    std::vector<double> w (std::min (n, x.size() - start));
    double mean = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) { w[i] = x[start + i]; mean += w[i]; }
    mean /= std::max<std::size_t> (1, w.size());
    for (auto& v : w) v -= mean;
    const std::size_t minLag = static_cast<std::size_t> (rate / 4000.0), maxLag = std::min (static_cast<std::size_t> (rate / 30.0), w.size() / 2);
    if (maxLag <= minLag + 2) { confidence = 0.0; return 0.0; }
    std::vector<double> ac (maxLag + 1, 0.0);
    double e0 = 0.0;
    for (std::size_t i = 0; i < w.size(); ++i) e0 += w[i] * w[i];
    if (e0 < 1e-12) { confidence = 0.0; return 0.0; }
    for (std::size_t lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (std::size_t i = 0; i + lag < w.size(); ++i) s += w[i] * w[i + lag];
        ac[lag] = s / e0;
    }
    double best = 0.0;
    for (std::size_t lag = minLag + 1; lag < maxLag; ++lag) if (ac[lag] > ac[lag - 1] && ac[lag] >= ac[lag + 1]) best = std::max (best, ac[lag]);
    if (best <= 0.0) { confidence = 0.0; return 0.0; }
    for (std::size_t lag = minLag + 1; lag < maxLag; ++lag)   // le plus petit retard dont le pic atteint 90 % du meilleur : évite les erreurs d'octave
        if (ac[lag] > ac[lag - 1] && ac[lag] >= ac[lag + 1] && ac[lag] >= 0.9 * best)
        {
            const double a = ac[lag - 1], b = ac[lag], c = ac[lag + 1];
            const double d = a - 2.0 * b + c;
            const double frac = std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0;
            confidence = b;
            return rate / (static_cast<double> (lag) + frac);
        }
    confidence = 0.0;
    return 0.0;
}

Analysis analyse (const std::vector<float>& x, double rate)
{
    Analysis a;
    a.rate = rate;
    a.seconds = static_cast<double> (x.size()) / rate;
    double peak = 0.0, sum = 0.0;
    for (const float v : x) { peak = std::max<double> (peak, std::fabs (v)); sum += static_cast<double> (v) * v; }
    a.peakDb = toDb (peak);
    a.rmsDb = toDb (std::sqrt (sum / std::max<std::size_t> (1, x.size())));

    // enveloppe : RMS sur 20 ms, toutes les 10 ms
    const std::size_t win = static_cast<std::size_t> (0.020 * rate), hop = static_cast<std::size_t> (kHop * rate);
    std::vector<double> lin;
    for (std::size_t s = 0; s + win <= x.size(); s += hop)
    {
        double e = 0.0;
        for (std::size_t i = 0; i < win; ++i) e += static_cast<double> (x[s + i]) * x[s + i];
        lin.push_back (std::sqrt (e / static_cast<double> (win)));
        a.env.push_back (toDb (lin.back()));
    }
    if (lin.empty()) return a;
    std::size_t pk = 0;
    for (std::size_t i = 0; i < lin.size(); ++i) if (lin[i] > lin[pk]) pk = i;
    const double pkv = lin[pk];
    std::size_t on = 0;
    while (on < lin.size() && lin[on] < 0.1 * pkv) ++on;
    // Attaque : de 10 % à 90 % du pic. Si le pic global arrive tard à cause d'un battement (voix désaccordées) et que le son est déjà plein dans les 150 premières ms,
    // on prend comme référence le maximum de ces 150 ms.
    double refLevel = pkv;
    {
        double early = 0.0;
        for (std::size_t i = on; i < lin.size() && i < on + 15; ++i) early = std::max (early, lin[i]);
        if (early >= 0.5 * pkv) refLevel = early;
    }
    std::size_t at90 = on;
    while (at90 < lin.size() && lin[at90] < 0.9 * refLevel) ++at90;
    a.onset = static_cast<double> (on) * kHop;
    a.peakTime = static_cast<double> (pk) * kHop;
    a.attack10to90 = static_cast<double> (at90 - on) * kHop;
    std::size_t end = pk;
    for (std::size_t i = pk; i < lin.size(); ++i) if (lin[i] >= pkv * 0.001) end = i;   // dernier point à moins de 60 dB sous le pic
    a.tailEnd = static_cast<double> (end) * kHop;
    for (const double t : { 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 3.0 })
    {
        const std::size_t i = on + static_cast<std::size_t> (t / kHop);
        if (i < lin.size()) a.envAfterOnset.push_back ({ t, toDb (lin[i] / pkv) });
    }

    // hauteur et spectre : sur la partie la plus pleine du son, un peu après le pic
    const std::size_t ref = std::min (x.size() - 1, static_cast<std::size_t> ((a.peakTime + 0.03) * rate));
    a.f0 = estimateF0 (x, ref, rate, a.f0Confidence);
    const std::size_t N = 16384;
    const auto mag = magnitude (x, ref, N);
    const double binHz = rate / static_cast<double> (N);
    double tot = 0.0, cen = 0.0;
    for (std::size_t i = 1; i < mag.size(); ++i) { const double p = mag[i] * mag[i]; tot += p; cen += p * static_cast<double> (i) * binHz; }
    a.centroid = tot > 0.0 ? cen / tot : 0.0;
    double acc = 0.0;
    for (std::size_t i = 1; i < mag.size(); ++i) { acc += mag[i] * mag[i]; if (acc >= 0.85 * tot) { a.rolloff85 = static_cast<double> (i) * binHz; break; } }
    if (a.f0 > 0.0)
    {
        double strongest = 0.0, hEnergy = 0.0;
        std::vector<double> h;
        for (int k = 1; k <= kHarm; ++k)
        {
            const double f = a.f0 * k;
            if (f > 0.45 * rate) { h.push_back (-200.0); continue; }
            const auto lo = static_cast<std::size_t> (std::max (1.0, std::floor ((f * 0.97) / binHz))), hi = std::min (mag.size() - 1, static_cast<std::size_t> (std::ceil ((f * 1.03) / binHz)));
            double m = 0.0;
            for (std::size_t i = lo; i <= hi; ++i) { m = std::max (m, mag[i]); hEnergy += mag[i] * mag[i]; }
            h.push_back (m);
            strongest = std::max (strongest, m);
        }
        for (auto& v : h) a.harmonicsDb.push_back (v <= 0.0 ? -200.0 : (v < 0.0 ? -200.0 : toDb (v / std::max (strongest, 1e-12))));
        a.noisiness = tot > 0.0 ? std::clamp (1.0 - hEnergy / tot, 0.0, 1.0) : 0.0;
    }
    // brillance dans le temps : centroïde toutes les 100 ms
    for (double t = 0.0; t + 0.1 <= a.seconds && a.centroidOverTime.size() < 40; t += 0.1)
    {
        const auto m = magnitude (x, static_cast<std::size_t> (t * rate), 4096);
        double tt = 0.0, cc = 0.0;
        for (std::size_t i = 1; i < m.size(); ++i) { const double p = m[i] * m[i]; tt += p; cc += p * static_cast<double> (i) * rate / 4096.0; }
        a.centroidOverTime.push_back (tt > 1e-12 ? cc / tt : 0.0);
    }
    // spectre moyen sur la première seconde après le début, en 24 bandes log
    {
        std::vector<double> power (4097, 0.0);
        int frames = 0;
        const std::size_t from = static_cast<std::size_t> (a.onset * rate);
        for (std::size_t s = from; s + 8192 <= x.size() && s < from + static_cast<std::size_t> (rate); s += 4096)
        {
            const auto m = magnitude (x, s, 8192);
            for (std::size_t i = 0; i < m.size(); ++i) power[i] += m[i] * m[i];
            ++frames;
        }
        std::vector<double> band (kBands, 0.0);
        for (int b = 0; b < kBands; ++b)
        {
            const double f0b = 50.0 * std::pow (16000.0 / 50.0, static_cast<double> (b) / kBands), f1b = 50.0 * std::pow (16000.0 / 50.0, static_cast<double> (b + 1) / kBands);
            for (std::size_t i = 0; i < power.size(); ++i)
            {
                const double f = static_cast<double> (i) * rate / 8192.0;
                if (f >= f0b && f < f1b) band[static_cast<std::size_t> (b)] += power[i];
            }
        }
        double all = 0.0;
        for (const double v : band) all += v;
        for (const double v : band) a.bandDb.push_back (frames > 0 && all > 0.0 ? 10.0 * std::log10 (std::max (v / all, 1e-12)) : -120.0);
    }
    return a;
}

// ------------------------------------------------------------------ affichage
std::string num (double v, int decimals = 2)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

void printAnalysis (const Analysis& a, bool json)
{
    if (json)
    {
        std::printf ("{\n  \"seconds\": %s,\n  \"peak_db\": %s,\n  \"rms_db\": %s,\n  \"onset_s\": %s,\n  \"peak_time_s\": %s,\n  \"attack_10_to_90_s\": %s,\n  \"tail_end_s\": %s,\n",
                     num (a.seconds, 3).c_str(), num (a.peakDb).c_str(), num (a.rmsDb).c_str(), num (a.onset, 3).c_str(), num (a.peakTime, 3).c_str(), num (a.attack10to90, 3).c_str(), num (a.tailEnd, 3).c_str());
        std::printf ("  \"f0_hz\": %s,\n  \"f0_midi\": %s,\n  \"f0_confidence\": %s,\n  \"spectral_centroid_hz\": %s,\n  \"rolloff85_hz\": %s,\n  \"noisiness\": %s,\n",
                     num (a.f0).c_str(), a.f0 > 0 ? num (midiOf (a.f0)).c_str() : "null", num (a.f0Confidence).c_str(), num (a.centroid, 1).c_str(), num (a.rolloff85, 1).c_str(), num (a.noisiness, 3).c_str());
        auto arr = [] (const char* name, const std::vector<double>& v, int d, bool last = false)
        {
            std::printf ("  \"%s\": [", name);
            for (std::size_t i = 0; i < v.size(); ++i) std::printf ("%s%s", i ? ", " : "", num (v[i], d).c_str());
            std::printf ("]%s\n", last ? "" : ",");
        };
        std::printf ("  \"env_after_onset_db\": {");
        for (std::size_t i = 0; i < a.envAfterOnset.size(); ++i) std::printf ("%s\"%ss\": %s", i ? ", " : "", num (a.envAfterOnset[i].first, 2).c_str(), num (a.envAfterOnset[i].second, 1).c_str());
        std::printf ("},\n");
        arr ("harmonics_db", a.harmonicsDb, 1);
        arr ("centroid_over_time_hz", a.centroidOverTime, 0);
        arr ("bands_db", a.bandDb, 1, true);
        std::printf ("}\n");
        return;
    }
    std::printf ("duration %.3f s, peak %.1f dBFS, rms %.1f dBFS\n", a.seconds, a.peakDb, a.rmsDb);
    std::printf ("onset %.3f s, peak at %.3f s, attack (10%% to 90%%) %.0f ms, tail ends (-60 dB) at %.2f s\n", a.onset, a.peakTime, a.attack10to90 * 1000.0, a.tailEnd);
    std::printf ("level after onset (dB below peak):");
    for (const auto& e : a.envAfterOnset) std::printf ("  %.2fs: %.1f", e.first, e.second);
    std::printf ("\n");
    if (a.f0 > 0.0) std::printf ("pitch %.2f Hz (MIDI %.2f), confidence %.2f\n", a.f0, midiOf (a.f0), a.f0Confidence);
    else std::printf ("pitch: none found (noisy or unpitched sound)\n");
    std::printf ("spectral centroid %.0f Hz, 85%% rolloff %.0f Hz, noisiness %.2f (0 = purely harmonic, 1 = noise)\n", a.centroid, a.rolloff85, a.noisiness);
    if (! a.harmonicsDb.empty())
    {
        std::printf ("harmonics (dB re strongest):");
        for (std::size_t i = 0; i < a.harmonicsDb.size(); ++i) std::printf (" %zu:%.0f", i + 1, a.harmonicsDb[i]);
        std::printf ("\n");
    }
    std::printf ("centroid every 100 ms (Hz):");
    for (const double c : a.centroidOverTime) std::printf (" %.0f", c);
    std::printf ("\n");
}

double bandDistance (const Analysis& a, const Analysis& b)
{
    double s = 0.0;
    int n = 0;
    for (std::size_t i = 0; i < a.bandDb.size() && i < b.bandDb.size(); ++i) { const double d = a.bandDb[i] - b.bandDb[i]; s += d * d; ++n; }
    return n > 0 ? std::sqrt (s / n) : 0.0;
}

void compare (const Analysis& a, const Analysis& b)
{
    std::printf ("                         A            B\n");
    std::printf ("peak dBFS            %8.1f     %8.1f\n", a.peakDb, b.peakDb);
    std::printf ("attack 10-90 ms      %8.0f     %8.0f\n", a.attack10to90 * 1000.0, b.attack10to90 * 1000.0);
    std::printf ("tail end s           %8.2f     %8.2f\n", a.tailEnd, b.tailEnd);
    std::printf ("pitch Hz             %8.2f     %8.2f\n", a.f0, b.f0);
    std::printf ("centroid Hz          %8.0f     %8.0f\n", a.centroid, b.centroid);
    std::printf ("rolloff85 Hz         %8.0f     %8.0f\n", a.rolloff85, b.rolloff85);
    std::printf ("noisiness            %8.2f     %8.2f\n", a.noisiness, b.noisiness);
    if (a.f0 > 0 && b.f0 > 0) std::printf ("pitch difference: %.0f cents\n", 1200.0 * std::log2 (b.f0 / a.f0));
    double hs = 0.0;
    int hn = 0;
    for (std::size_t i = 0; i < 16 && i < a.harmonicsDb.size() && i < b.harmonicsDb.size(); ++i)
        if (a.harmonicsDb[i] > -100 && b.harmonicsDb[i] > -100) { hs += std::fabs (a.harmonicsDb[i] - b.harmonicsDb[i]); ++hn; }
    if (hn > 0) std::printf ("harmonics: mean difference %.1f dB over the first %d\n", hs / hn, hn);
    double es = 0.0;
    int en = 0;
    const std::size_t oa = static_cast<std::size_t> (a.onset / kHop), ob = static_cast<std::size_t> (b.onset / kHop);
    for (std::size_t i = 0; i < 200 && oa + i < a.env.size() && ob + i < b.env.size(); ++i)
        { const double d = std::max (a.env[oa + i] - a.peakDb, -80.0) - std::max (b.env[ob + i] - b.peakDb, -80.0); es += d * d; ++en; }   // le silence compte -80 dB, pas -180
    if (en > 0) std::printf ("envelope: RMS difference %.1f dB over the first %.1f s (aligned on onset, levels relative to each peak)\n", std::sqrt (es / en), en * kHop);
    std::printf ("spectrum shape (24 bands): RMS difference %.1f dB (below 3 dB: close; above 6 dB: clearly different)\n", bandDistance (a, b));
    std::printf ("loudness difference: %.1f dB (B minus A, by RMS)\n", b.rmsDb - a.rmsDb);
}

// ------------------------------------------------------------------ commandes
int cmdParams()
{
    as::AnalogSynthEngine e;
    std::printf ("key | name | unit | kind | min | max | default | choices\n");
    static const char* units[] = { "", "dB", "Hz", "ms", "s", "%", "cents", "semitones", "octaves", "deg", "samples", "ratio" };
    static const char* kinds[] = { "continuous", "stepped", "toggle", "choice" };
    for (const auto& k : as::keyedParams())
    {
        const auto idx = e.params().indexOf (k.id);
        if (idx == ParameterSet::npos) continue;
        const auto& s = e.params().spec (idx);
        std::string choices;
        for (std::size_t i = 0; i < s.choices.size(); ++i) choices += (i ? "," : "") + std::to_string (i) + "=" + s.choices[i];
        std::printf ("%s | %s | %s | %s | %g | %g | %g | %s\n", k.key, s.name.c_str(), units[static_cast<int> (s.unit)], kinds[static_cast<int> (s.kind)], s.min, s.max, s.defaultValue, choices.c_str());
    }
    std::printf ("%s | glide (not automatable, in the preset state) |\n", as::glideKey);
    return 0;
}

int cmdWaves()
{
    static const char* cats[] = { "digital", "analog", "complex" };
    for (int i = 0; i < kNumWaves; ++i)
    {
        const auto& w = waveInfo (i);
        std::printf ("%d | %s | %s%s\n", w.id, std::string (w.key).c_str(), cats[static_cast<int> (w.category)], w.analytic ? " | analytic (band-limited, pulse width adjustable)" : "");
    }
    return 0;
}

int cmdPresets (int argc, char** argv)
{
    const auto list = as::factoryPresets();
    if (argc >= 4 && std::string (argv[2]) == "export")
    {
        const auto n = static_cast<std::size_t> (std::atoi (argv[3]));
        if (n < 1 || n > list.size() || argc < 5) { std::fprintf (stderr, "usage: presets export N file.flipreset (N from 1 to %zu)\n", list.size()); return 2; }
        std::ofstream out (argv[4], std::ios::binary);
        out << preset::toJson (list[n - 1]);
        return out ? 0 : 1;
    }
    for (std::size_t i = 0; i < list.size(); ++i) std::printf ("%zu | %s | %s\n", i + 1, list[i].name.c_str(), list[i].type.c_str());
    return 0;
}

int cmdValidate (const std::string& path)
{
    std::string text;
    if (! readText (path, text)) { std::fprintf (stderr, "cannot read %s\n", path.c_str()); return 1; }
    bool ok = false;
    const auto p = preset::presetFromJson (text, &ok);
    if (! ok) { std::printf ("INVALID: not a usable JSON object\n"); return 1; }
    int problems = 0;
    if (p.module != as::moduleId) { std::printf ("PROBLEM: \"module\" is '%s', it must be '%s'\n", p.module.c_str(), as::moduleId); ++problems; }
    as::AnalogSynthEngine e;
    for (const auto& kv : p.parameters)
    {
        const auto id = as::paramIdForKey (kv.first);
        if (id == 0) { if (kv.first != as::glideKey) { std::printf ("PROBLEM: unknown parameter key '%s'\n", kv.first.c_str()); ++problems; } continue; }
        const auto idx = e.params().indexOf (id);
        if (idx == ParameterSet::npos || kv.second.isText) continue;
        const auto& s = e.params().spec (idx);
        if (kv.second.number < s.min - 1e-9 || kv.second.number > s.max + 1e-9)
        {
            std::printf ("PROBLEM: '%s' = %g is outside %g .. %g\n", kv.first.c_str(), kv.second.number, s.min, s.max);
            ++problems;
        }
    }
    if (! p.state.empty())
    {
        as::AnalogSynthEngine e2;
        if (! e2.loadPreset (p, as::defaultConfig())) { std::printf ("PROBLEM: the engine refuses this preset\n"); ++problems; }
    }
    else std::printf ("note: no \"state\" (oscillators, envelopes, filters): only the listed parameters are applied\n");
    std::printf (problems == 0 ? "OK: %s (%zu parameters)\n" : "%s: %zu parameters, see the problems above\n", p.name.c_str(), p.parameters.size());
    return problems == 0 ? 0 : 1;
}

struct NoteEvent { double start, dur; int key; int vel; };

bool parseNotes (const std::string& text, std::vector<NoteEvent>& out)
{
    std::stringstream ss (text);
    std::string item;
    while (std::getline (ss, item, ','))
    {
        NoteEvent n { 0.0, 1.0, 60, 100 };
        const int got = std::sscanf (item.c_str(), "%d:%lf:%lf:%d", &n.key, &n.start, &n.dur, &n.vel);
        if (got < 3) return false;
        out.push_back (n);
    }
    return ! out.empty();
}

int cmdRender (int argc, char** argv)
{
    std::string presetPath, outPath = "render.wav", notesText = "60:0:2";
    int factory = 0;
    double seconds = 0.0, rate = 48000.0;
    bool normalise = false;
    std::vector<std::pair<std::string, double>> sets;
    for (int i = 2; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--preset") presetPath = next();
        else if (a == "--factory") factory = std::atoi (next());
        else if (a == "--notes") notesText = next();
        else if (a == "--seconds") seconds = std::atof (next());
        else if (a == "--rate") rate = std::atof (next());
        else if (a == "--out") outPath = next();
        else if (a == "--normalize") normalise = true;
        else if (a == "--set")
        {
            const std::string kv = next();
            const auto eq = kv.find ('=');
            if (eq == std::string::npos) { std::fprintf (stderr, "--set expects key=value\n"); return 2; }
            sets.push_back ({ kv.substr (0, eq), std::atof (kv.c_str() + eq + 1) });
        }
        else { std::fprintf (stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    std::vector<NoteEvent> notes;
    if (! parseNotes (notesText, notes)) { std::fprintf (stderr, "bad --notes: use key:start_s:duration_s[:velocity], separated by commas\n"); return 2; }

    as::AnalogSynthEngine engine;
    if (! engine.ok()) { std::fprintf (stderr, "engine error\n"); return 1; }
    engine.prepare (rate, 64);
    const auto cfg = as::defaultConfig();
    preset::Preset p;
    bool havePreset = false;
    if (! presetPath.empty())
    {
        std::string text;
        if (! readText (presetPath, text)) { std::fprintf (stderr, "cannot read %s\n", presetPath.c_str()); return 1; }
        bool ok = false;
        p = preset::presetFromJson (text, &ok);
        if (! ok) { std::fprintf (stderr, "%s is not a usable preset (run validate)\n", presetPath.c_str()); return 1; }
        havePreset = true;
    }
    else if (factory > 0)
    {
        const auto list = as::factoryPresets();
        if (static_cast<std::size_t> (factory) > list.size()) { std::fprintf (stderr, "factory presets go from 1 to %zu\n", list.size()); return 2; }
        p = list[static_cast<std::size_t> (factory) - 1];
        havePreset = true;
    }
    if (havePreset && ! engine.loadPreset (p, cfg)) { std::fprintf (stderr, "the engine refuses this preset (module must be 'analogsynth')\n"); return 1; }
    for (const auto& kv : sets)
    {
        const auto id = as::paramIdForKey (kv.first);
        if (id == 0 || ! engine.params().set (id, kv.second)) { std::fprintf (stderr, "unknown parameter key '%s' (see: params)\n", kv.first.c_str()); return 2; }
    }
    engine.collect();

    double last = 0.0;
    for (const auto& n : notes) last = std::max (last, n.start + n.dur);
    const double total = seconds > 0.0 ? seconds : last + 1.5;   // 1,5 s de queue pour le relâchement
    const auto frames = static_cast<std::size_t> (total * rate);
    std::vector<float> L (frames, 0.0f), R (frames, 0.0f);
    struct Ev { std::size_t at; bool on; int key; float vel; };
    std::vector<Ev> evs;
    for (const auto& n : notes)
    {
        evs.push_back ({ static_cast<std::size_t> (n.start * rate), true, n.key, static_cast<float> (std::clamp (n.vel, 1, 127)) / 127.0f });
        evs.push_back ({ static_cast<std::size_t> ((n.start + n.dur) * rate), false, n.key, 0.0f });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
    std::size_t next = 0;
    for (std::size_t pos = 0; pos < frames; pos += 64)
    {
        while (next < evs.size() && evs[next].at <= pos)
        {
            if (evs[next].on) engine.noteOn (0, evs[next].key, evs[next].vel); else engine.noteOff (0, evs[next].key);
            ++next;
        }
        const auto n = static_cast<int> (std::min<std::size_t> (64, frames - pos));
        engine.process (L.data() + pos, R.data() + pos, n);
    }
    double peak = 0.0, sum = 0.0;
    for (std::size_t i = 0; i < frames; ++i) { peak = std::max ({ peak, static_cast<double> (std::fabs (L[i])), static_cast<double> (std::fabs (R[i])) }); sum += static_cast<double> (L[i]) * L[i]; }
    if (normalise && peak > 1e-9)
    {
        const float g = static_cast<float> (0.89 / peak);   // -1 dBFS
        for (auto& v : L) v *= g;
        for (auto& v : R) v *= g;
        std::printf ("normalised by %.1f dB\n", toDb (g));
    }
    if (! writeWav (outPath, L, R, rate)) { std::fprintf (stderr, "cannot write %s\n", outPath.c_str()); return 1; }
    std::printf ("wrote %s: %.2f s, %.0f Hz, peak %.1f dBFS, rms (left) %.1f dBFS%s\n", outPath.c_str(), total, rate, toDb (peak), toDb (std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, frames)))),
                 peak >= 1.0 ? "  WARNING: clipping, lower output.volume or use --normalize" : "");
    return 0;
}

void usage()
{
    std::fprintf (stderr,
        "neptune_synth_tool <command>\n"
        "  params                         parameters a preset can carry\n"
        "  waves                          oscillator waveforms\n"
        "  presets                        factory presets; 'presets export N file.flipreset' writes one\n"
        "  validate file.flipreset        check a preset\n"
        "  render [--preset f.flipreset | --factory N] [--set key=value ...] [--notes key:start:dur[:vel],...]\n"
        "         [--seconds S] [--rate 48000] [--normalize] [--out file.wav]\n"
        "  analyse file.wav [--json]      pitch, envelope, harmonics, brightness\n"
        "  compare a.wav b.wav            how far apart two sounds are\n");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { usage(); return 2; }
    const std::string cmd = argv[1];
    if (cmd == "params") return cmdParams();
    if (cmd == "waves") return cmdWaves();
    if (cmd == "presets") return cmdPresets (argc, argv);
    if (cmd == "validate" && argc >= 3) return cmdValidate (argv[2]);
    if (cmd == "render") return cmdRender (argc, argv);
    if (cmd == "analyse" && argc >= 3)
    {
        std::vector<float> x;
        double rate = 48000.0;
        if (! readMono (argv[2], x, rate)) { std::fprintf (stderr, "cannot read %s\n", argv[2]); return 1; }
        printAnalysis (analyse (x, rate), argc >= 4 && std::string (argv[3]) == "--json");
        return 0;
    }
    if (cmd == "compare" && argc >= 4)
    {
        std::vector<float> a, b;
        double ra = 48000.0, rb = 48000.0;
        if (! readMono (argv[2], a, ra) || ! readMono (argv[3], b, rb)) { std::fprintf (stderr, "cannot read the files\n"); return 1; }
        compare (analyse (a, ra), analyse (b, rb));
        return 0;
    }
    usage();
    return 2;
}
