// Moteur de convolution LowLatConv (conv/*.hpp) : les jeux de tests d'origine, compilés avec leur `main` renommé (voir tests/CMakeLists.txt),
// et le module « Convolution » (insert) de bout en bout : chargement d'une IR par le thread de chargement, retard exact, état texte.
#include <catch2/catch_test_macros.hpp>
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
std::filesystem::path writeDelayIr (int delaySamples, double sampleRate = 48000.0)
{
    const auto file = std::filesystem::temp_directory_path() / ("neptune_conv_ir_" + std::to_string (delaySamples) + ".wav");
    SF_INFO info {};
    info.samplerate = static_cast<int> (sampleRate);
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
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
