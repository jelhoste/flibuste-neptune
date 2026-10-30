#include "neptune/core/IrGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace neptune::core::irgen
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

struct Rng
{
    std::uint32_t s;
    explicit Rng (std::uint32_t seed) : s (seed == 0 ? 1u : seed) {}
    double uniform() noexcept   // 0..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<double> (s) / 4294967296.0;
    }
    double gaussian() noexcept { return uniform() + uniform() + uniform() + uniform() - 2.0; }   // approximation, variance 1/3
};

struct Room
{
    const char* key;
    double rt60;             // s
    double preDelayMs;
    int earlyTaps;
    double earlyMinMs, earlyMaxMs, earlyLevel;
    double cutoffStart, cutoffEnd;   // Hz : bande passante du champ diffus au début et à rt60
    double buildupMs;                // montée de la densité
};

const Room kRooms[] = {
    { "ambience",   0.35,  0.0, 10,  2.0,  25.0, 0.55, 9000.0, 3000.0,  8.0 },
    { "small_room", 0.60,  2.0, 14,  3.0,  40.0, 0.55, 8000.0, 2500.0, 12.0 },
    { "large_room", 1.10,  6.0, 18,  5.0,  70.0, 0.50, 8000.0, 2200.0, 20.0 },
    { "hall",       2.20, 18.0, 22,  8.0, 110.0, 0.45, 7000.0, 1800.0, 35.0 },
    { "large_hall", 3.40, 28.0, 24, 10.0, 140.0, 0.40, 6500.0, 1500.0, 50.0 },
    { "cathedral",  5.50, 40.0, 20, 20.0, 200.0, 0.35, 5000.0, 1200.0, 80.0 },
    { "plate",      1.80,  0.0,  0,  0.0,   0.0, 0.00, 15000.0, 5000.0,  2.0 },
};

const std::vector<Entry>& table()
{
    static const std::vector<Entry> t = {
        { "ambience", "ir.ambience" }, { "small_room", "ir.small_room" }, { "large_room", "ir.large_room" }, { "hall", "ir.hall" },
        { "large_hall", "ir.large_hall" }, { "cathedral", "ir.cathedral" }, { "plate", "ir.plate" }, { "spring", "ir.spring" } };
    return t;
}

std::uint32_t seedOf (std::string_view key)
{
    std::uint32_t h = 2166136261u;
    for (const char c : key) { h ^= static_cast<std::uint8_t> (c); h *= 16777619u; }
    return h;
}

// Ramène l'énergie moyenne par voie à 1.
void normalise (llc::IRBuffer& ir)
{
    double e = 0.0;
    for (const auto& c : ir.ch) for (const float v : c) e += static_cast<double> (v) * v;
    e /= static_cast<double> (std::max<std::size_t> (1, ir.ch.size()));
    const float g = e > 1.0e-20 ? static_cast<float> (1.0 / std::sqrt (e)) : 1.0f;
    for (auto& c : ir.ch) for (float& v : c) v *= g;
}

void makeRoom (const Room& r, llc::IRBuffer& out)
{
    const std::size_t pre = static_cast<std::size_t> (r.preDelayMs * 0.001 * kRate);
    const std::size_t len = pre + static_cast<std::size_t> (r.rt60 * 1.05 * kRate);
    out.sampleRate = kRate;
    out.ch.assign (2, std::vector<float> (len, 0.0f));
    Rng rng (seedOf (r.key));
    const double decayRate = 6.907755278982137 / r.rt60;   // ln(1000) : -60 dB à rt60
    for (std::size_t c = 0; c < 2; ++c)
    {
        auto& y = out.ch[c];
        // champ diffus : bruit filtré par deux passe-bas à un pôle dont la coupure descend (échelle logarithmique) de cutoffStart à cutoffEnd
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = pre; i < len; ++i)
        {
            const double t = static_cast<double> (i - pre) / kRate;
            const double u = std::min (1.0, t / r.rt60);
            const double fc = r.cutoffStart * std::pow (r.cutoffEnd / r.cutoffStart, u);
            const double a = std::exp (-2.0 * kPi * fc / kRate);
            s1 = (1.0 - a) * rng.gaussian() + a * s1;
            s2 = (1.0 - a) * s1 + a * s2;
            const double comp = std::sqrt (r.cutoffStart / fc);   // la variance du bruit filtré est proportionnelle à la coupure : compensée, pour que l'enveloppe soit l'énergie totale
            const double build = 1.0 - std::exp (-t / (r.buildupMs * 0.001));
            y[i] = static_cast<float> (s2 * comp * build * build * std::exp (-decayRate * t));
        }
        // réflexions précoces : impulsions de signe aléatoire, instants tirés entre earlyMin et earlyMax, amplitude en 1/(1 + t / 10 ms)
        for (int k = 0; k < r.earlyTaps; ++k)
        {
            const double ms = r.earlyMinMs + (r.earlyMaxMs - r.earlyMinMs) * rng.uniform();
            const std::size_t at = pre + static_cast<std::size_t> (ms * 0.001 * kRate);
            if (at >= len) continue;
            const double amp = r.earlyLevel / (1.0 + ms / 10.0) * (rng.uniform() < 0.5 ? -1.0 : 1.0) * (0.6 + 0.8 * rng.uniform());
            y[at] += static_cast<float> (amp * 8.0);
        }
    }
    normalise (out);
}

// Ressort : échos espacés, chacun passé k fois dans une chaîne de passe-tout (dispersion) et dans un passe-bas.
void makeSpring (llc::IRBuffer& out)
{
    constexpr double kRt60 = 2.0;
    const double periodMs[2] = { 36.0, 38.5 };
    const std::size_t len = static_cast<std::size_t> (kRt60 * 1.05 * kRate);
    out.sampleRate = kRate;
    out.ch.assign (2, std::vector<float> (len, 0.0f));
    constexpr int kSections = 30;
    constexpr std::size_t kSpan = 16384;   // longueur de travail d'un écho (la dispersion l'étale de plus en plus)
    for (std::size_t c = 0; c < 2; ++c)
    {
        const double a = c == 0 ? 0.70 : 0.68;
        const std::size_t period = static_cast<std::size_t> (periodMs[c] * 0.001 * kRate);
        const double perEcho = std::pow (10.0, -3.0 * periodMs[c] * 0.001 / kRt60);   // -60 dB à kRt60
        std::vector<double> echo (kSpan, 0.0), ap (kSpan);
        echo[0] = 1.0;
        double lp;
        const double lpA = std::exp (-2.0 * kPi * 4500.0 / kRate);
        for (std::size_t k = 0; k * period < len; ++k)
        {
            // un tour de plus dans le ressort : dispersion (passe-tout en cascade), perte, adoucissement des aigus
            for (int s = 0; s < kSections; ++s)
            {
                double x1 = 0.0, y1 = 0.0;
                for (std::size_t i = 0; i < kSpan; ++i)
                {
                    const double y = -a * echo[i] + x1 + a * y1;
                    x1 = echo[i];
                    y1 = y;
                    ap[i] = y;
                }
                echo.swap (ap);
            }
            lp = 0.0;
            for (std::size_t i = 0; i < kSpan; ++i) { lp = (1.0 - lpA) * echo[i] + lpA * lp; echo[i] = lp; }
            const double gain = std::pow (perEcho, static_cast<double> (k));
            const std::size_t base = k * period;
            for (std::size_t i = 0; i < kSpan && base + i < len; ++i) out.ch[c][base + i] += static_cast<float> (echo[i] * gain);
        }
    }
    normalise (out);
}
} // namespace

const std::vector<Entry>& entries() { return table(); }

bool isBuiltinPath (std::string_view path) noexcept { return path.substr (0, kPrefix.size()) == kPrefix; }
std::string pathFor (std::string_view key) { return std::string (kPrefix) + std::string (key); }
std::string keyOfPath (std::string_view path) { return isBuiltinPath (path) ? std::string (path.substr (kPrefix.size())) : std::string(); }

bool generate (std::string_view key, llc::IRBuffer& out)
{
    out = llc::IRBuffer {};
    if (key == "spring") { makeSpring (out); return true; }
    for (const auto& r : kRooms)
        if (key == r.key) { makeRoom (r, out); return true; }
    return false;
}
} // namespace neptune::core::irgen
