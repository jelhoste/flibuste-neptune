// Moteur de convolution LowLatConv (conv/*.hpp) : les jeux de tests d'origine, compilés avec leur `main` renommé (voir tests/CMakeLists.txt),
// et le module « Convolution » (insert) de bout en bout : chargement d'une IR par le thread de chargement, retard exact, état texte.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <sndfile.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <thread>
#include <vector>

#include "neptune/core/ConvolutionModule.h"
#include "neptune/core/StockModule.h"

using namespace neptune::core;

int llc_test_convolver();
int llc_test_display();
int llc_test_eq();
int llc_test_ir();
int llc_test_live();
int llc_test_meters();
int llc_test_processor();
int llc_test_stereo();
int llc_test_wav();

TEST_CASE ("conv engine: the original DSP test suites pass", "[conv]")
{
    CHECK (llc_test_convolver() == 0);
    CHECK (llc_test_stereo() == 0);
    CHECK (llc_test_live() == 0);
    CHECK (llc_test_ir() == 0);
    CHECK (llc_test_eq() == 0);
    CHECK (llc_test_display() == 0);
    CHECK (llc_test_meters() == 0);
    CHECK (llc_test_wav() == 0);
    CHECK (llc_test_processor() == 0);
}

namespace
{
std::filesystem::path writeDelayIr (int delaySamples, double sampleRate = 48000.0, bool aiff = false)
{
    const auto file = std::filesystem::temp_directory_path() / ("neptune_conv_ir_" + std::to_string (delaySamples) + (aiff ? ".aiff" : ".wav"));
    SF_INFO info {};
    info.samplerate = static_cast<int> (sampleRate);
    info.channels = 1;
    info.format = (aiff ? SF_FORMAT_AIFF : SF_FORMAT_WAV) | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    std::vector<float> ir (static_cast<std::size_t> (delaySamples) + 64, 0.0f);
    ir[static_cast<std::size_t> (delaySamples)] = 1.0f;
    sf_write_float (f, ir.data(), static_cast<sf_count_t> (ir.size()));
    sf_close (f);
    return file;
}

bool waitFor (const ConvolutionModule& m, int status, int ms = 5000)
{
    for (int t = 0; t < ms; t += 10)
    {
        if (m.irStatus() == status) return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    return m.irStatus() == status;
}
} // namespace

TEST_CASE ("convolution module: the EQ response curve matches the filters", "[conv][module]")
{
    auto inst = createInsert ("convolution", 48000.0, 256);
    REQUIRE (inst != nullptr);
    auto& set = inst->runner.params();
    using namespace convolution;
    CHECK (ConvolutionModule::eqResponseDb (set, 1000.0, 48000.0) == 0.0);   // bandes et filtres coupés : plat
    set.set (bandParam (0, kOn), 1.0);
    set.set (bandParam (0, kType), 0.0);
    set.set (bandParam (0, kFreq), 1000.0);
    set.set (bandParam (0, kGain), 12.0);
    CHECK (std::fabs (ConvolutionModule::eqResponseDb (set, 1000.0, 48000.0) - 12.0) < 0.05);       // cloche : le gain à sa fréquence
    CHECK (std::fabs (ConvolutionModule::eqResponseDb (set, 30.0, 48000.0)) < 0.5);                // loin de la cloche : plat
    set.set (hpOn, 1.0);
    set.set (hpFreq, 200.0);
    set.set (hpSlope, 1.0);   // 24 dB/oct
    CHECK (std::fabs (ConvolutionModule::eqBandResponseDb (set, 3, 200.0, 48000.0) + 3.01) < 0.1);  // Butterworth : -3 dB à la coupure, quel que soit l'ordre
    CHECK (ConvolutionModule::eqBandResponseDb (set, 3, 20.0, 48000.0) < -40.0);
    set.set (eqOn, 0.0);
    CHECK (ConvolutionModule::eqResponseDb (set, 200.0, 48000.0) == 0.0);                           // EQ coupé : plat
}

TEST_CASE ("convolution module: transparent without an IR, exact delay with one", "[conv][module]")
{
    auto inst = createInsert ("convolution", 48000.0, 256);
    REQUIRE (inst != nullptr);
    auto& runner = inst->runner;
    auto& module = static_cast<ConvolutionModule&> (runner.module());
    runner.params().set (convolution::autoTrim, 0.0);   // le silence du début fait partie du retard à vérifier
    runner.params().set (convolution::norm, 0.0);

    std::mt19937 rng (7);
    std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
    std::vector<float> in, out;
    auto run = [&] (int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            std::vector<float> l (256), r (256);
            for (auto& x : l) x = dist (rng);
            r = l;
            in.insert (in.end(), l.begin(), l.end());
            runner.process (l.data(), r.data(), 256, false);
            out.insert (out.end(), l.begin(), l.end());
        }
    };
    run (4);
    for (std::size_t i = 0; i < in.size(); ++i) REQUIRE (std::fabs (out[i] - in[i]) < 1.0e-5f);   // pas d'IR : transparent

    const auto file = writeDelayIr (100);
    module.setIrPath (file.string());
    CHECK (module.irPath() == file.string());
    run (2);   // la machine de chargement tourne pendant que l'audio avance
    REQUIRE (waitFor (module, convolution::kLoaded));
    CHECK (module.irLengthMs() > 1.0);
    CHECK (module.rawIr() != nullptr);
    run (12);   // le fondu de bascule (1024 échantillons) est passé
    for (std::size_t i = in.size() - 1000; i < in.size(); ++i) REQUIRE (std::fabs (out[i] - in[i - 100]) < 1.0e-4f);

    module.setIrPath ({});
    REQUIRE (waitFor (module, convolution::kNoIr));
    std::filesystem::remove (file);
}

TEST_CASE ("convolution module: an AIFF impulse response loads through the sample loader", "[conv][module]")
{
    auto inst = createInsert ("convolution", 48000.0, 256);
    REQUIRE (inst != nullptr);
    auto& runner = inst->runner;
    auto& module = static_cast<ConvolutionModule&> (runner.module());
    runner.params().set (convolution::autoTrim, 0.0);
    runner.params().set (convolution::norm, 0.0);
    const auto file = writeDelayIr (57, 48000.0, true);
    module.setIrPath (file.string());
    std::vector<float> l (256, 0.0f), r (256, 0.0f);
    for (int i = 0; i < 400 && module.irStatus() != convolution::kLoaded; ++i)
    {
        runner.process (l.data(), r.data(), 256, false);
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    CHECK (module.irStatus() == convolution::kLoaded);
    CHECK (module.irLengthMs() > 1.0);
    std::filesystem::remove (file);
}

TEST_CASE ("convolution module: a missing file is an error and the module stays transparent", "[conv][module]")
{
    auto inst = createInsert ("convolution", 48000.0, 256);
    REQUIRE (inst != nullptr);
    auto& module = static_cast<ConvolutionModule&> (inst->runner.module());
    module.setIrPath ("/nonexistent/folder/none.wav");
    std::vector<float> l (256, 0.25f), r (256, 0.25f);
    for (int i = 0; i < 40 && module.irStatus() != convolution::kError; ++i)
    {
        inst->runner.process (l.data(), r.data(), 256, false);
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    CHECK (module.irStatus() == convolution::kError);
    CHECK (std::fabs (l[255] - 0.25f) < 1.0e-5f);
}

TEST_CASE ("convolution module: the IR file is part of the module's text state", "[conv][module]")
{
    auto inst = createInsert ("convolution", 48000.0, 256);
    REQUIRE (inst != nullptr);
    auto& module = inst->runner.module();
    CHECK (module.hasStateString());
    module.setStateString ("/some/where/ir.wav");
    CHECK (module.stateString() == "/some/where/ir.wav");
    // les autres modules n'ont pas d'état texte
    auto util = createInsert ("utility", 48000.0, 256);
    REQUIRE (util != nullptr);
    CHECK_FALSE (util->runner.module().hasStateString());
}

// ---- longues réponses (au-delà de la tête FIR et des premières partitions) : le banc de test de plugins mesurait 32 ms de réponse seulement
namespace
{
std::filesystem::path writeImpulseIr (const std::vector<std::pair<int, float>>& taps, int length, const std::string& name)
{
    const auto file = std::filesystem::temp_directory_path() / ("neptune_conv_ir_" + name + ".wav");
    SF_INFO info {};
    info.samplerate = 48000;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    std::vector<float> ir (static_cast<std::size_t> (length), 0.0f);
    for (const auto& [pos, v] : taps) ir[static_cast<std::size_t> (pos)] = v;
    sf_write_float (f, ir.data(), static_cast<sf_count_t> (ir.size()));
    sf_close (f);
    return file;
}

// Envoie une impulsion de 1,0 (voies gauche et droite) au premier échantillon, puis du silence : `total` échantillons, par blocs de `block`.
std::vector<float> impulseResponse (InsertInstance& inst, int total, int block)
{
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (total));
    std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
    for (int done = 0; done < total; done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        if (done == 0) l[0] = 1.0f;
        r = l;
        inst.runner.process (l.data(), r.data(), block, false);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

void runSilence (InsertInstance& inst, int total, int block)
{
    std::vector<float> l (static_cast<std::size_t> (block), 0.0f), r (static_cast<std::size_t> (block), 0.0f);
    for (int done = 0; done < total; done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        r = l;
        inst.runner.process (l.data(), r.data(), block, false);
    }
}
} // namespace

TEST_CASE ("convolution module: taps beyond the first partitions are heard (engine settled)", "[conv][module][tail]")
{
    const std::vector<std::pair<int, float>> taps { { 0, 0.5f }, { 500, 0.4f }, { 700, 0.35f }, { 1000, 0.3f }, { 1400, 0.25f }, { 1600, 0.2f }, { 2000, 0.15f }, { 4000, 0.1f }, { 8000, 0.08f }, { 20000, 0.05f } };
    for (const int block : { 64, 512 })
    {
        INFO ("block " << block);
        auto inst = createInsert ("convolution", 48000.0, block);
        REQUIRE (inst != nullptr);
        auto& module = static_cast<ConvolutionModule&> (inst->runner.module());
        inst->runner.params().set (convolution::autoTrim, 0.0);
        inst->runner.params().set (convolution::norm, 0.0);
        const auto file = writeImpulseIr (taps, 24000, "taps" + std::to_string (block));
        module.setIrPath (file.string());
        runSilence (*inst, 4096, block);                          // l'audio tourne pendant le chargement
        REQUIRE (waitFor (module, convolution::kLoaded));
        runSilence (*inst, 8192, block);                          // le fondu de bascule est passé
        const auto out = impulseResponse (*inst, 30000, block);
        for (const auto& [pos, v] : taps) CHECK (std::fabs (out[static_cast<std::size_t> (pos)] - v) < 2.0e-3f);
        double stray = 0.0;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            bool isTap = false;
            for (const auto& t : taps) isTap = isTap || static_cast<std::size_t> (t.first) == i;
            if (! isTap) stray = std::max (stray, static_cast<double> (std::fabs (out[i])));
        }
        CHECK (stray < 2.0e-3);
        std::filesystem::remove (file);
    }
}

TEST_CASE ("convolution module: engine installed at the very first block still produces the whole tail", "[conv][module][tail]")
{
    // comme le banc : le chargement se termine avant tout audio ; l'impulsion arrive dans le premier bloc, pendant le fondu d'entrée de 1024 échantillons
    const std::vector<std::pair<int, float>> taps { { 500, 0.4f }, { 1600, 0.2f }, { 4000, 0.1f }, { 8000, 0.08f }, { 20000, 0.05f } };
    auto inst = createInsert ("convolution", 48000.0, 512);
    REQUIRE (inst != nullptr);
    auto& module = static_cast<ConvolutionModule&> (inst->runner.module());
    inst->runner.params().set (convolution::autoTrim, 0.0);
    inst->runner.params().set (convolution::norm, 0.0);
    const auto file = writeImpulseIr (taps, 24000, "first");
    module.setIrPath (file.string());
    REQUIRE (waitFor (module, convolution::kLoaded));
    const auto out = impulseResponse (*inst, 30000, 512);
    CHECK (std::fabs (out[500] - 0.4f * 500.0f / 1024.0f) < 3.0e-3f);   // fondu d'entrée linéaire
    for (const auto& [pos, v] : taps) if (pos >= 1024) CHECK (std::fabs (out[static_cast<std::size_t> (pos)] - v) < 3.0e-3f);
    std::filesystem::remove (file);
}

TEST_CASE ("convolution module: the tail survives a new prepare (host re-activation)", "[conv][module][tail]")
{
    const std::vector<std::pair<int, float>> taps { { 500, 0.4f }, { 1600, 0.2f }, { 4000, 0.1f }, { 8000, 0.08f } };
    auto inst = createInsert ("convolution", 48000.0, 512);
    REQUIRE (inst != nullptr);
    auto& module = static_cast<ConvolutionModule&> (inst->runner.module());
    inst->runner.params().set (convolution::autoTrim, 0.0);
    inst->runner.params().set (convolution::norm, 0.0);
    const auto file = writeImpulseIr (taps, 24000, "reprepare");
    module.setIrPath (file.string());
    runSilence (*inst, 4096, 512);
    REQUIRE (waitFor (module, convolution::kLoaded));
    runSilence (*inst, 8192, 512);
    const auto versionBefore = module.irVersion();
    inst->runner.prepare (48000.0, 512);   // réactivation par l'hôte, même cadence : le moteur reste en place
    std::this_thread::sleep_for (std::chrono::milliseconds (400));
    CHECK (module.irVersion() == versionBefore);   // pas de reconstruction de l'IR
    // l'IR est refaite en arrière-plan : on laisse tourner l'audio jusqu'à ce qu'elle soit de nouveau chargée et installée
    for (int i = 0; i < 100; ++i) { runSilence (*inst, 4096, 512); if (module.irStatus() == convolution::kLoaded) break; std::this_thread::sleep_for (std::chrono::milliseconds (20)); }
    runSilence (*inst, 16384, 512);
    const auto out = impulseResponse (*inst, 30000, 512);
    for (const auto& [pos, v] : taps) CHECK (std::fabs (out[static_cast<std::size_t> (pos)] - v) < 2.0e-3f);
    std::filesystem::remove (file);
}

TEST_CASE ("convolution module: lab-like sequence, one switch at a time", "[conv][module][tail]")
{
    const bool doReset = GENERATE (false, true);    // runner.reset() après le chemin d'IR (comme setStateInformation)
    const bool doSets = GENERATE (false, true);     // options d'IR changées sans audio, après le chargement
    const bool paced = GENERATE (false, true);      // rendu au rythme du temps réel
    INFO ("reset " << doReset << ", sets " << doSets << ", paced " << paced);
    const std::vector<std::pair<int, float>> taps { { 0, 0.9155f }, { 500, 0.4578f }, { 1000, 0.3662f }, { 1400, 0.3052f }, { 1600, 0.2441f }, { 2000, 0.1831f }, { 4000, 0.1526f }, { 8000, 0.1221f } };   // comme imp8.wav du banc
    auto inst = createInsert ("convolution", 48000.0, 512);
    REQUIRE (inst != nullptr);
    auto& module = static_cast<ConvolutionModule&> (inst->runner.module());
    const auto file = writeImpulseIr (taps, 24000, "bisect");
    inst->runner.params().resetAll();
    module.setIrPath (file.string());
    if (doReset) inst->runner.reset();
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    if (doSets)
    {
        inst->runner.params().set (convolution::autoTrim, 0.0);
        inst->runner.params().set (convolution::norm, 0.0);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
    }
    REQUIRE (waitFor (module, convolution::kLoaded));
    std::vector<float> out;
    std::vector<float> l (512), r (512);
    const auto started = std::chrono::steady_clock::now();
    for (int done = 0; done < 30000; done += 512)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        if (done == 0) l[0] = 1.0f;
        r = l;
        inst->runner.process (l.data(), r.data(), 512, false);
        out.insert (out.end(), l.begin(), l.end());
        if (paced) std::this_thread::sleep_until (started + std::chrono::microseconds (static_cast<long long> ((done + 512) / 48000.0 * 1.0e6)));
    }
    // le gain de normalisation (Énergie) appliqué à l'IR : le même pour toutes les touches de la queue s'il y a une queue
    double energy = 0.0;
    for (const auto& t : taps) energy += static_cast<double> (t.second) * static_cast<double> (t.second);
    const float gain = static_cast<float> (1.0 / std::sqrt (energy));
    INFO ("gain Énergie " << gain << " ; out[500] = " << out[500] << ", out[1000] = " << out[1000] << ", out[1400] = " << out[1400] << ", out[1600] = " << out[1600] << ", out[4000] = " << out[4000] << ", out[8000] = " << out[8000]);
    for (const auto& [pos, v] : taps)
        if (pos >= 1024)
        {
            const float got = out[static_cast<std::size_t> (pos)];
            const bool normOn = std::fabs (got - v * gain) < 3.0e-3f, normOff = std::fabs (got - v) < 3.0e-3f;
            CHECK ((normOn || normOff));   // la queue est là (avec ou sans normalisation)
            if (pos == 4000) { if (doSets) CHECK (normOff); else CHECK (normOn); }   // les options changées sans audio doivent être prises en compte
        }
    std::filesystem::remove (file);
}
