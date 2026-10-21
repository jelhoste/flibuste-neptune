// Enregistreur audio direct : écriture au fil de l'eau d'un flux poussé par blocs.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include "neptune/core/AudioRecorder.h"
#include "neptune/core/SampleEdit.h"

using namespace neptune::core;

TEST_CASE ("recorder writes everything that was pushed", "[recorder]")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-rec-test" / "take.wav";
    std::filesystem::remove_all (path.parent_path());
    AudioRecorder rec;
    REQUIRE_FALSE (rec.recording());
    REQUIRE (rec.start (path, 48000.0));
    REQUIRE (rec.recording());
    REQUIRE_FALSE (rec.start (path, 48000.0));   // déjà en cours

    std::vector<float> l (480), r (480);
    std::int64_t total = 0;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 480; ++i, ++total)
        {
            l[static_cast<std::size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * 440.0 * static_cast<double> (total) / 48000.0));
            r[static_cast<std::size_t> (i)] = -l[static_cast<std::size_t> (i)];
        }
        rec.push (l.data(), r.data(), 480);
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    rec.stop();
    REQUIRE_FALSE (rec.recording());
    REQUIRE (rec.dropped() == 0);
    sampleedit::Buffer b;
    REQUIRE (sampleedit::readFile (path, b));
    REQUIRE (b.channels == 2);
    REQUIRE (b.frames() == 100 * 480);
    REQUIRE (b.frames() == rec.framesWritten());
    REQUIRE (rec.peak() >= 0.0f);
    std::filesystem::remove_all (path.parent_path());
}

TEST_CASE ("recorder keeps the exact samples, mono input is duplicated", "[recorder]")
{
    const auto path = std::filesystem::temp_directory_path() / "neptune-rec-test2" / "take.wav";
    std::filesystem::remove_all (path.parent_path());
    AudioRecorder rec;
    REQUIRE (rec.start (path, 44100.0));
    std::vector<float> m (1000);
    for (std::size_t i = 0; i < m.size(); ++i) m[i] = static_cast<float> (i) / 2000.0f;
    rec.push (m.data(), nullptr, 1000);
    REQUIRE (rec.stop() == 1000);
    sampleedit::Buffer b;
    REQUIRE (sampleedit::readFile (path, b));
    REQUIRE (b.sampleRate == 44100.0);
    for (std::int64_t i = 0; i < 1000; i += 97)
    {
        REQUIRE (b.at (i, 0) == m[static_cast<std::size_t> (i)]);
        REQUIRE (b.at (i, 1) == m[static_cast<std::size_t> (i)]);
    }
    std::filesystem::remove_all (path.parent_path());
}

TEST_CASE ("pushes when not recording are ignored, stop without start is harmless", "[recorder]")
{
    AudioRecorder rec;
    float x[16] = {};
    rec.push (x, x, 16);
    REQUIRE (rec.framesWritten() == 0);
    REQUIRE (rec.stop() == 0);
    // le « dossier » parent est un fichier ordinaire : impossible d'y créer la prise (sur tous les systèmes)
    const auto blocker = std::filesystem::temp_directory_path() / "neptune-rec-blocker";
    { std::ofstream (blocker) << "x"; }
    REQUIRE_FALSE (rec.start (blocker / "take.wav", 48000.0));
    std::filesystem::remove (blocker);
}
