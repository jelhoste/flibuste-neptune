// Tests de lecture de fichiers audio (palier 1).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sndfile.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "neptune/core/SampleLoader.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
// FLAC minimaux fabriqués à la main (sous-trame « verbatim », CRC valides) : 64 échantillons mono, rampe de -32 à +31 pas.
static const unsigned char kFlac16Mono64[] = {
    0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0a, 0xc4, 0x40, 0xf0, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf8, 0x60, 0x08, 0x00, 0x3f,
    0x06, 0x02, 0xc1, 0x80, 0xc3, 0x74, 0xc5, 0x68, 0xc7, 0x5c, 0xc9, 0x50, 0xcb, 0x44, 0xcd, 0x38,
    0xcf, 0x2c, 0xd1, 0x20, 0xd3, 0x14, 0xd5, 0x08, 0xd6, 0xfc, 0xd8, 0xf0, 0xda, 0xe4, 0xdc, 0xd8,
    0xde, 0xcc, 0xe0, 0xc0, 0xe2, 0xb4, 0xe4, 0xa8, 0xe6, 0x9c, 0xe8, 0x90, 0xea, 0x84, 0xec, 0x78,
    0xee, 0x6c, 0xf0, 0x60, 0xf2, 0x54, 0xf4, 0x48, 0xf6, 0x3c, 0xf8, 0x30, 0xfa, 0x24, 0xfc, 0x18,
    0xfe, 0x0c, 0x00, 0x00, 0x01, 0xf4, 0x03, 0xe8, 0x05, 0xdc, 0x07, 0xd0, 0x09, 0xc4, 0x0b, 0xb8,
    0x0d, 0xac, 0x0f, 0xa0, 0x11, 0x94, 0x13, 0x88, 0x15, 0x7c, 0x17, 0x70, 0x19, 0x64, 0x1b, 0x58,
    0x1d, 0x4c, 0x1f, 0x40, 0x21, 0x34, 0x23, 0x28, 0x25, 0x1c, 0x27, 0x10, 0x29, 0x04, 0x2a, 0xf8,
    0x2c, 0xec, 0x2e, 0xe0, 0x30, 0xd4, 0x32, 0xc8, 0x34, 0xbc, 0x36, 0xb0, 0x38, 0xa4, 0x3a, 0x98,
    0x3c, 0x8c, 0x23, 0x37
};

static const unsigned char kFlac24Mono64[] = {
    0x66, 0x4c, 0x61, 0x43, 0x80, 0x00, 0x00, 0x22, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0a, 0xc4, 0x41, 0x70, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xf8, 0x60, 0x0c, 0x00, 0x3f,
    0xad, 0x02, 0xcf, 0x2c, 0x00, 0xd0, 0xb2, 0xa0, 0xd2, 0x39, 0x40, 0xd3, 0xbf, 0xe0, 0xd5, 0x46,
    0x80, 0xd6, 0xcd, 0x20, 0xd8, 0x53, 0xc0, 0xd9, 0xda, 0x60, 0xdb, 0x61, 0x00, 0xdc, 0xe7, 0xa0,
    0xde, 0x6e, 0x40, 0xdf, 0xf4, 0xe0, 0xe1, 0x7b, 0x80, 0xe3, 0x02, 0x20, 0xe4, 0x88, 0xc0, 0xe6,
    0x0f, 0x60, 0xe7, 0x96, 0x00, 0xe9, 0x1c, 0xa0, 0xea, 0xa3, 0x40, 0xec, 0x29, 0xe0, 0xed, 0xb0,
    0x80, 0xef, 0x37, 0x20, 0xf0, 0xbd, 0xc0, 0xf2, 0x44, 0x60, 0xf3, 0xcb, 0x00, 0xf5, 0x51, 0xa0,
    0xf6, 0xd8, 0x40, 0xf8, 0x5e, 0xe0, 0xf9, 0xe5, 0x80, 0xfb, 0x6c, 0x20, 0xfc, 0xf2, 0xc0, 0xfe,
    0x79, 0x60, 0x00, 0x00, 0x00, 0x01, 0x86, 0xa0, 0x03, 0x0d, 0x40, 0x04, 0x93, 0xe0, 0x06, 0x1a,
    0x80, 0x07, 0xa1, 0x20, 0x09, 0x27, 0xc0, 0x0a, 0xae, 0x60, 0x0c, 0x35, 0x00, 0x0d, 0xbb, 0xa0,
    0x0f, 0x42, 0x40, 0x10, 0xc8, 0xe0, 0x12, 0x4f, 0x80, 0x13, 0xd6, 0x20, 0x15, 0x5c, 0xc0, 0x16,
    0xe3, 0x60, 0x18, 0x6a, 0x00, 0x19, 0xf0, 0xa0, 0x1b, 0x77, 0x40, 0x1c, 0xfd, 0xe0, 0x1e, 0x84,
    0x80, 0x20, 0x0b, 0x20, 0x21, 0x91, 0xc0, 0x23, 0x18, 0x60, 0x24, 0x9f, 0x00, 0x26, 0x25, 0xa0,
    0x27, 0xac, 0x40, 0x29, 0x32, 0xe0, 0x2a, 0xb9, 0x80, 0x2c, 0x40, 0x20, 0x2d, 0xc6, 0xc0, 0x2f,
    0x4d, 0x60, 0x82, 0x96
};

struct TempDir
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "neptune_sample_loader_test";
    TempDir() { std::filesystem::remove_all (path); std::filesystem::create_directories (path); }
    ~TempDir() { std::filesystem::remove_all (path); }
};

std::filesystem::path writeWav (const std::filesystem::path& file, int channels, int rate, int subformat, const std::vector<float>& interleaved)
{
    SF_INFO info {};
    info.channels = channels;
    info.samplerate = rate;
    info.format = SF_FORMAT_WAV | subformat;
    SNDFILE* f = sf_open (file.string().c_str(), SFM_WRITE, &info);
    REQUIRE (f != nullptr);
    sf_writef_float (f, interleaved.data(), static_cast<sf_count_t> (interleaved.size() / static_cast<std::size_t> (channels)));
    sf_close (f);
    return file;
}

void writeBytes (const std::filesystem::path& file, const unsigned char* data, std::size_t n)
{
    std::ofstream out (file, std::ios::binary);
    out.write (reinterpret_cast<const char*> (data), static_cast<std::streamsize> (n));
}
} // namespace

TEST_CASE ("wav 16 bit stays int16 in memory")
{
    TempDir d;
    std::vector<float> data;
    for (int i = 0; i < 100; ++i) data.push_back (static_cast<float> (i) / 200.0f);
    const auto file = writeWav (d.path / "a16.wav", 1, 44100, SF_FORMAT_PCM_16, data);

    const auto r = loadSampleFile (file);
    REQUIRE (r.ok());
    CHECK (r.sample->isInt16());
    CHECK (r.sample->channels == 1);
    CHECK (r.sample->frames == 100);
    CHECK (r.sample->sampleRate == 44100.0);
    CHECK (r.sample->sourceBitDepth == 16);
    CHECK (r.sample->name == "a16.wav");
    CHECK_THAT (r.sample->at (0, 50), WithinAbs (0.25, 1.0 / 16384));
    CHECK (r.sample->at (0, -1) == 0.0f);
    CHECK (r.sample->at (0, 100) == 0.0f);
    CHECK (r.sample->bytes() == 200);
}

TEST_CASE ("wav 24 bit and float become float32, stereo is planar")
{
    TempDir d;
    std::vector<float> data; // L croissant, R décroissant
    for (int i = 0; i < 40; ++i) { data.push_back (static_cast<float> (i) / 100.0f); data.push_back (-static_cast<float> (i) / 100.0f); }

    for (int sub : { SF_FORMAT_PCM_24, SF_FORMAT_FLOAT })
    {
        const auto r = loadSampleFile (writeWav (d.path / ("s" + std::to_string (sub) + ".wav"), 2, 48000, sub, data));
        REQUIRE (r.ok());
        CHECK_FALSE (r.sample->isInt16());
        CHECK (r.sample->channels == 2);
        CHECK (r.sample->frames == 40);
        CHECK_THAT (r.sample->at (0, 30), WithinAbs (0.30, 1e-4));
        CHECK_THAT (r.sample->at (1, 30), WithinAbs (-0.30, 1e-4));
    }
}

TEST_CASE ("flac 16 and 24 bit are decoded")
{
    TempDir d;
    writeBytes (d.path / "x16.flac", kFlac16Mono64, sizeof kFlac16Mono64);
    writeBytes (d.path / "x24.FLAC", kFlac24Mono64, sizeof kFlac24Mono64);

    const auto a = loadSampleFile (d.path / "x16.flac");
    REQUIRE (a.ok());
    CHECK (a.sample->isInt16());
    CHECK (a.sample->frames == 64);
    CHECK (a.sample->channels == 1);
    CHECK (a.sample->sampleRate == 44100.0);
    CHECK_THAT (a.sample->at (0, 0), WithinAbs (-16000.0 / 32768.0, 1e-6));
    CHECK_THAT (a.sample->at (0, 63), WithinAbs (15500.0 / 32768.0, 1e-6));

    const auto b = loadSampleFile (d.path / "x24.FLAC"); // extension en majuscules
    REQUIRE (b.ok());
    CHECK_FALSE (b.sample->isInt16());
    CHECK (b.sample->sourceBitDepth == 24);
    CHECK_THAT (b.sample->at (0, 0), WithinAbs (-3200000.0 / 8388608.0, 1e-6));
    CHECK_THAT (b.sample->at (0, 40), WithinAbs (800000.0 / 8388608.0, 1e-6));
}

TEST_CASE ("loading failures give a message and never throw")
{
    TempDir d;
    CHECK_FALSE (loadSampleFile (d.path / "missing.wav").ok());
    CHECK (loadSampleFile (d.path / "missing.wav").error.find ("not found") != std::string::npos);

    writeBytes (d.path / "garbage.wav", kFlac16Mono64, 20); // pas un WAV
    CHECK_FALSE (loadSampleFile (d.path / "garbage.wav").ok());

    writeBytes (d.path / "cut.flac", kFlac16Mono64, 60); // FLAC tronqué
    CHECK_FALSE (loadSampleFile (d.path / "cut.flac").ok());

    std::vector<float> six (6 * 10, 0.1f);
    const auto r = loadSampleFile (writeWav (d.path / "six.wav", 6, 48000, SF_FORMAT_PCM_16, six));
    CHECK_FALSE (r.ok());
    CHECK (r.error.find ("channel") != std::string::npos);
}

TEST_CASE ("probe predicts exactly the memory the loader will use, from the header only")
{
    TempDir d;
    std::vector<float> mono (100, 0.1f), stereo (200, 0.1f);
    struct Case { std::filesystem::path file; };
    const std::vector<std::filesystem::path> files = {
        writeWav (d.path / "m16.wav", 1, 44100, SF_FORMAT_PCM_16, mono),
        writeWav (d.path / "s16.wav", 2, 44100, SF_FORMAT_PCM_16, stereo),
        writeWav (d.path / "m24.wav", 1, 44100, SF_FORMAT_PCM_24, mono),
        writeWav (d.path / "s24.wav", 2, 48000, SF_FORMAT_PCM_24, stereo),
        writeWav (d.path / "m32.wav", 1, 44100, SF_FORMAT_PCM_32, mono),
        writeWav (d.path / "sf.wav", 2, 44100, SF_FORMAT_FLOAT, stereo),
    };
    for (const auto& f : files)
    {
        INFO (f.filename().string());
        const auto p = probeAudioFile (f);
        const auto r = loadSampleFile (f);
        REQUIRE (p.ok);
        REQUIRE (r.ok());
        CHECK (p.ramBytes == r.sample->bytes());
        CHECK (p.frames == r.sample->frames);
        CHECK (p.channels == r.sample->channels);
        CHECK (p.bitDepth == r.sample->sourceBitDepth);
        CHECK (p.keptAsInt16 == r.sample->isInt16());
    }
    // 24 bits stéréo = 100 frames x 2 canaux x 4 octets ; 16 bits mono = 100 x 1 x 2
    CHECK (probeAudioFile (d.path / "s24.wav").ramBytes == 800);
    CHECK (probeAudioFile (d.path / "m16.wav").ramBytes == 200);

    writeBytes (d.path / "x16.flac", kFlac16Mono64, sizeof kFlac16Mono64);
    writeBytes (d.path / "x24.flac", kFlac24Mono64, sizeof kFlac24Mono64);
    CHECK (probeAudioFile (d.path / "x16.flac").ramBytes == 64 * 2);
    CHECK (probeAudioFile (d.path / "x24.flac").ramBytes == 64 * 4);
    CHECK (probeAudioFile (d.path / "x24.flac").bitDepth == 24);

    CHECK_FALSE (probeAudioFile (d.path / "missing.wav").ok);
    std::vector<float> six (6 * 10, 0.1f);
    const auto bad = probeAudioFile (writeWav (d.path / "six.wav", 6, 48000, SF_FORMAT_PCM_16, six));
    CHECK_FALSE (bad.ok);
    CHECK (bad.ramBytes == 0);
}
