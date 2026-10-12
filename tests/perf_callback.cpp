// Mesure de performance du callback audio du cœur (palier 0).
// Exécutable séparé, hors ctest : les temps dépendent de la machine, ils ne servent qu'à comparer
// « avant » et « après » une refactorisation sur la MÊME machine (voir docs/performance.md).
//
//   neptune_perf_callback                         affiche le tableau
//   neptune_perf_callback --save avant.json       enregistre les mesures
//   neptune_perf_callback --compare avant.json    compare ; code de sortie 1 si régression
//                         [--tolerance 25]        seuil en % (défaut 25)
#include "neptune/core/Engine.h"
#include "neptune/core/Instrument.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/ProbePool.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using neptune::core::Engine;
using Clock = std::chrono::steady_clock;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kChannels = 128; // 64 sorties stéréo : le jeu fixe du plugin
constexpr int kRepetitions = 21;
constexpr double kRepetitionSeconds = 0.05; // durée de chaque répétition, en temps de calcul

struct Scenario
{
    std::string name;
    int blockSize;
    int heldNotes;       // notes tenues pendant la mesure (noteOn avant, aucune libération)
    int paramsPerBlock;  // setParameter appelés avant chaque bloc (automation de l'hôte)
};

struct Result
{
    std::string name;
    int blockSize;
    double medianNs, minNs, maxNs; // par bloc
    double budgetNs;               // durée réelle d'un bloc
};

// Empêche le compilateur de supprimer le travail dont le résultat n'est jamais lu.
double g_sink = 0.0;

Result run (const Scenario& s)
{
    Engine engine;
    neptune::core::ParameterSet params;
    neptune::core::sampler::addParams (params);
    engine.attachParameters (&params);
    engine.prepare (kSampleRate, s.blockSize);

    std::vector<std::vector<float>> storage (kChannels, std::vector<float> (static_cast<size_t> (s.blockSize), 0.5f));
    std::vector<float*> chans (kChannels);
    for (int c = 0; c < kChannels; ++c) chans[static_cast<size_t> (c)] = storage[static_cast<size_t> (c)].data();

    // Identifiants réels du pool sonde : 4 maîtres + 32 pistes × 21 (676 paramètres).
    namespace probe = neptune::core::probe;
    std::vector<std::uint32_t> ids { probe::masterVolume, probe::masterPan, probe::masterMute, probe::algorithm };
    for (int t = 0; t < 32; ++t)
        for (int k = 0; k < probe::paramsPerTrack; ++k)
        {
            static const std::uint32_t offs[] = { probe::kVolume, probe::kPan, probe::kMute, probe::kSolo, probe::kType };
            std::uint32_t off = k < 5 ? offs[k] : (k < 13 ? probe::kInsertBypass + (k - 5) : probe::kSendLevel + (k - 13));
            ids.push_back (probe::trackParam (t, off));
        }

    // Charge représentative : un instrument stéréo float de 2 s joué en boucle, notes tenues réparties sur le clavier.
    if (s.heldNotes > 0)
    {
        auto sample = std::make_shared<neptune::core::SampleData>();
        sample->channels = 2;
        sample->frames = static_cast<std::int64_t> (2.0 * kSampleRate);
        sample->sampleRate = kSampleRate;
        sample->pcm32.resize (static_cast<std::size_t> (sample->frames) * 2);
        for (std::int64_t i = 0; i < sample->frames; ++i)
        {
            const float v = 0.3f * std::sin (6.2831853f * 220.0f * static_cast<float> (i) / static_cast<float> (kSampleRate));
            sample->pcm32[static_cast<std::size_t> (i)] = v;
            sample->pcm32[static_cast<std::size_t> (sample->frames + i)] = -v;
        }
        auto inst = std::make_shared<neptune::core::Instrument>();
        inst->zones.push_back (neptune::core::makeFullKeyboardZone (std::move (sample), 60, neptune::core::PlayMode::Loop));
        engine.publishInstrument (std::move (inst));
        engine.process (chans.data(), kChannels, s.blockSize); // adopte l'instrument
    }
    for (int n = 0; n < s.heldNotes; ++n) engine.noteOn (n % 16, 36 + n % 64, 0.8f);

    size_t idCursor = 0;
    auto block = [&]
    {
        for (int p = 0; p < s.paramsPerBlock; ++p)
        {
            engine.setParameter (ids[idCursor], 0.25 + 0.5 * static_cast<double> (idCursor % 3));
            idCursor = (idCursor + 1) % ids.size();
        }
        engine.process (chans.data(), kChannels, s.blockSize);
    };

    // Échauffement, puis calibrage du nombre de blocs par répétition.
    for (int i = 0; i < 200; ++i) block();
    int blocksPerRep = 64;
    for (;;)
    {
        auto t0 = Clock::now();
        for (int i = 0; i < blocksPerRep; ++i) block();
        double sec = std::chrono::duration<double> (Clock::now() - t0).count();
        if (sec >= kRepetitionSeconds || blocksPerRep >= (1 << 24)) break;
        blocksPerRep *= 2;
    }

    std::vector<double> perBlock;
    for (int r = 0; r < kRepetitions; ++r)
    {
        auto t0 = Clock::now();
        for (int i = 0; i < blocksPerRep; ++i) block();
        double ns = std::chrono::duration<double, std::nano> (Clock::now() - t0).count();
        perBlock.push_back (ns / blocksPerRep);
    }
    for (int c = 0; c < kChannels; ++c) g_sink += storage[static_cast<size_t> (c)][0];

    std::sort (perBlock.begin(), perBlock.end());
    return { s.name, s.blockSize, perBlock[perBlock.size() / 2], perBlock.front(), perBlock.back(),
             1e9 * s.blockSize / kSampleRate };
}

nlohmann::json toJson (const std::vector<Result>& rs)
{
    nlohmann::json j;
    j["format"] = "neptune-perf";
    j["sample_rate"] = kSampleRate;
    j["channels"] = kChannels;
    for (const auto& r : rs)
        j["scenarios"][r.name] = { { "block_size", r.blockSize }, { "median_ns", r.medianNs },
                                   { "min_ns", r.minNs },         { "max_ns", r.maxNs } };
    return j;
}
} // namespace

int main (int argc, char** argv)
{
    std::string savePath, comparePath;
    double tolerance = 25.0;
    for (int i = 1; i < argc; ++i)
    {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (! std::strcmp (argv[i], "--save")) { auto v = next(); if (! v) return 2; savePath = v; }
        else if (! std::strcmp (argv[i], "--compare")) { auto v = next(); if (! v) return 2; comparePath = v; }
        else if (! std::strcmp (argv[i], "--tolerance")) { auto v = next(); if (! v) return 2; tolerance = std::atof (v); }
        else { std::fprintf (stderr, "option inconnue : %s\n", argv[i]); return 2; }
    }

    std::vector<Scenario> scenarios;
    for (int bs : { 32, 64, 128, 256, 512 })
    {
        scenarios.push_back ({ "idle_" + std::to_string (bs), bs, 0, 0 });
        scenarios.push_back ({ "loaded_" + std::to_string (bs), bs, 64, 64 });
    }

    std::vector<Result> results;
    std::printf ("%-14s %8s %12s %12s %12s %10s\n", "scenario", "bloc", "mediane ns", "min ns", "max ns", "% budget");
    for (const auto& s : scenarios)
    {
        results.push_back (run (s));
        const auto& r = results.back();
        std::printf ("%-14s %8d %12.0f %12.0f %12.0f %9.4f%%\n", r.name.c_str(), r.blockSize, r.medianNs, r.minNs,
                     r.maxNs, 100.0 * r.medianNs / r.budgetNs);
    }

    if (! savePath.empty())
    {
        std::ofstream (savePath) << toJson (results).dump (2) << "\n";
        std::printf ("mesures enregistrees : %s\n", savePath.c_str());
    }

    if (! comparePath.empty())
    {
        std::ifstream in (comparePath);
        nlohmann::json base = nlohmann::json::parse (in, nullptr, false);
        if (base.is_discarded() || base.value ("format", "") != "neptune-perf" || ! base.contains ("scenarios"))
        {
            std::fprintf (stderr, "reference illisible : %s\n", comparePath.c_str());
            return 2;
        }
        bool regression = false;
        std::printf ("\ncomparaison avec %s (tolerance %.0f%%, sur la mediane)\n", comparePath.c_str(), tolerance);
        for (const auto& r : results)
        {
            if (! base["scenarios"].contains (r.name)) { std::printf ("%-14s absent de la reference\n", r.name.c_str()); continue; }
            double b = base["scenarios"][r.name].value ("median_ns", 0.0);
            double pct = b > 0.0 ? 100.0 * (r.medianNs - b) / b : 0.0;
            bool bad = pct > tolerance;
            regression = regression || bad;
            std::printf ("%-14s %10.0f -> %10.0f ns  %+7.1f%%  %s\n", r.name.c_str(), b, r.medianNs, pct, bad ? "REGRESSION" : "ok");
        }
        if (regression) return 1;
    }
    return g_sink < 0.0 ? 3 : 0; // g_sink n'est jamais négatif : sert seulement à garder les écritures
}
