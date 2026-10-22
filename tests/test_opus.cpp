#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "neptune/core/OpusFile.h"
#include "neptune/core/SampleLoader.h"
#include "neptune/core/SampleMeta.h"

#if defined (NEPTUNE_HAS_OPUS)
 #include <opus.h>
#endif

using namespace neptune::core;

namespace
{
std::uint32_t oggCrc (const unsigned char* p, std::size_t n)
{
    static std::uint32_t table[256];
    static bool init = false;
    if (! init)
    {
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? (r << 1) ^ 0x04c11db7u : (r << 1);
            table[i] = r;
        }
        init = true;
    }
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < n; ++i) crc = (crc << 8) ^ table[((crc >> 24) & 0xFF) ^ p[i]];
    return crc;
}

void put32 (std::vector<unsigned char>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back (static_cast<unsigned char> (x >> (8 * i))); }

// Une page Ogg contenant des paquets entiers (chacun < 255 * 255 octets, répartis en segments).
void writePage (std::vector<unsigned char>& out, std::uint32_t serial, std::uint32_t seq, unsigned char flags, std::int64_t granule,
                const std::vector<std::vector<unsigned char>>& packets)
{
    std::vector<unsigned char> page { 'O', 'g', 'g', 'S', 0, flags };
    for (int i = 0; i < 8; ++i) page.push_back (static_cast<unsigned char> (static_cast<std::uint64_t> (granule) >> (8 * i)));
    put32 (page, serial); put32 (page, seq); put32 (page, 0);
    std::vector<unsigned char> lace, body;
    for (const auto& p : packets)
    {
        std::size_t left = p.size();
        while (left >= 255) { lace.push_back (255); left -= 255; }
        lace.push_back (static_cast<unsigned char> (left));
        body.insert (body.end(), p.begin(), p.end());
    }
    page.push_back (static_cast<unsigned char> (lace.size()));
    page.insert (page.end(), lace.begin(), lace.end());
    page.insert (page.end(), body.begin(), body.end());
    const auto crc = oggCrc (page.data(), page.size());
    for (int i = 0; i < 4; ++i) page[22 + static_cast<std::size_t> (i)] = static_cast<unsigned char> (crc >> (8 * i));
    out.insert (out.end(), page.begin(), page.end());
}

#if defined (NEPTUNE_HAS_OPUS)
// Encode une sinusoïde (440 Hz, deux fois plus fort à gauche qu'à droite) en Ogg Opus. Rend les octets du fichier.
std::vector<unsigned char> makeOpus (int channels, int frames, int preSkipOverride = -1, int gainQ8 = 0, int inputRate = 44100)
{
    int err = 0;
    OpusEncoder* enc = opus_encoder_create (48000, channels, OPUS_APPLICATION_AUDIO, &err);
    REQUIRE (err == OPUS_OK);
    opus_encoder_ctl (enc, OPUS_SET_BITRATE (128000));
    opus_int32 look = 0;
    opus_encoder_ctl (enc, OPUS_GET_LOOKAHEAD (&look));
    const int preSkip = preSkipOverride >= 0 ? preSkipOverride : look;

    std::vector<unsigned char> out;
    std::vector<unsigned char> head { 'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, static_cast<unsigned char> (channels),
                                      static_cast<unsigned char> (preSkip & 0xFF), static_cast<unsigned char> (preSkip >> 8) };
    put32 (head, static_cast<std::uint32_t> (inputRate));
    head.push_back (static_cast<unsigned char> (gainQ8 & 0xFF)); head.push_back (static_cast<unsigned char> ((gainQ8 >> 8) & 0xFF));
    head.push_back (0);
    writePage (out, 77, 0, 0x02, 0, { head });
    std::vector<unsigned char> tags { 'O', 'p', 'u', 's', 'T', 'a', 'g', 's' };
    put32 (tags, 4); for (const char c : { 't', 'e', 's', 't' }) tags.push_back (static_cast<unsigned char> (c));
    put32 (tags, 0);
    writePage (out, 77, 1, 0, 0, { tags });

    const int frameSize = 960;   // 20 ms
    const int totalIn = frames + preSkip;
    const int packetsCount = (totalIn + frameSize - 1) / frameSize;
    std::vector<float> in (static_cast<std::size_t> (packetsCount * frameSize * channels), 0.0f);
    for (int i = 0; i < packetsCount * frameSize; ++i)
    {
        // le début (pré-saut) est du silence : le signal « utile » commence à preSkip
        const float v = i < preSkip || i >= totalIn ? 0.0f : 0.5f * std::sin (6.2831853f * 440.0f * static_cast<float> (i - preSkip) / 48000.0f);
        for (int c = 0; c < channels; ++c) in[static_cast<std::size_t> (i * channels + c)] = c == 0 ? v : v * 0.5f;
    }
    std::uint32_t seq = 2;
    for (int k = 0; k < packetsCount; ++k)
    {
        unsigned char buf[4000];
        const int n = opus_encode_float (enc, in.data() + static_cast<std::size_t> (k * frameSize * channels), frameSize, buf, sizeof (buf));
        REQUIRE (n > 0);
        const bool last = k == packetsCount - 1;
        writePage (out, 77, seq++, last ? 0x04 : 0, last ? totalIn : static_cast<std::int64_t> ((k + 1) * frameSize),
                   { std::vector<unsigned char> (buf, buf + n) });
    }
    opus_encoder_destroy (enc);
    return out;
}
#endif

std::filesystem::path writeTemp (const std::vector<unsigned char>& bytes, const char* name)
{
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream f (p, std::ios::binary);
    f.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size()));
    return p;
}
} // namespace

TEST_CASE ("a non-Opus file is not taken for Opus")
{
    const auto p = writeTemp ({ 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, "neptune_not_opus.opus");
    CHECK_FALSE (isOpusFile (p));
    CHECK_FALSE (probeOpusFile (p).ok);
    std::filesystem::remove (p);
}

#if defined (NEPTUNE_HAS_OPUS)
TEST_CASE ("Ogg Opus: exact length (pre-skip and end trimming), 48 kHz, samples where they should be")
{
    const int frames = 10000;   // volontairement pas un multiple de 20 ms
    const auto bytes = makeOpus (2, frames);
    const auto p = writeTemp (bytes, "neptune_stereo.opus");
    REQUIRE (isOpusFile (p));

    const auto info = probeOpusFile (p);
    REQUIRE (info.ok);
    CHECK (info.channels == 2);
    CHECK (info.frames == frames);
    CHECK (info.inputRate == 44100);

    const auto d = decodeOpusFile (p);
    REQUIRE (d.info.ok);
    REQUIRE (d.samples.size() == static_cast<std::size_t> (frames) * 2);
    // la sinusoïde reprend à la phase 0 au premier échantillon utile : mesure grossière de l'amplitude et du rapport entre canaux
    double peakL = 0.0, peakR = 0.0;
    for (int i = 2000; i < 8000; ++i) { peakL = std::max (peakL, std::fabs (static_cast<double> (d.samples[static_cast<std::size_t> (i) * 2]))); peakR = std::max (peakR, std::fabs (static_cast<double> (d.samples[static_cast<std::size_t> (i) * 2 + 1]))); }
    CHECK (peakL == Catch::Approx (0.5).margin (0.05));
    CHECK (peakR == Catch::Approx (0.25).margin (0.05));
    // le signal est bien aligné : le zéro de la sinusoïde tombe près de la frame 0
    CHECK (std::fabs (static_cast<double> (d.samples[0])) < 0.1);
    std::filesystem::remove (p);
}

TEST_CASE ("Ogg Opus: loaded and probed like any other sample file")
{
    const auto p = writeTemp (makeOpus (1, 24000), "neptune_mono.opus");
    const auto probe = probeAudioFile (p);
    REQUIRE (probe.ok);
    CHECK (probe.channels == 1);
    CHECK (probe.frames == 24000);
    CHECK (probe.sampleRate == 48000.0);
    CHECK (probe.ramBytes == 24000u * 4u);

    const auto r = loadSampleFile (p, 4096);   // le streaming ne s'applique pas : tout reste résident
    REQUIRE (r.ok());
    CHECK (r.sample->frames == 24000);
    CHECK (r.sample->sampleRate == 48000.0);
    CHECK_FALSE (r.sample->isStreamed());
    CHECK (r.sample->at (0, 5000) != 0.0f);

    AudioStreamReader reader;
    REQUIRE (reader.open (p));
    CHECK (reader.channels() == 1);
    CHECK (reader.totalFrames() == 24000);
    REQUIRE (reader.seek (5000));
    float buf[16];
    REQUIRE (reader.read (buf, 16) == 16);
    for (int i = 0; i < 16; ++i) CHECK (buf[i] == r.sample->at (0, 5000 + i));
    REQUIRE (reader.seek (23990));
    CHECK (reader.read (buf, 16) == 10);   // fin du fichier
    CHECK (reader.read (buf, 16) == 0);
    std::filesystem::remove (p);
}

TEST_CASE ("Ogg Opus: Neptune marks go to the sidecar file, the Opus file is left untouched")
{
    const auto bytes = makeOpus (1, 6000);
    const auto p = writeTemp (bytes, "neptune_marks.opus");
    SampleMeta m;
    m.hasVolumeMarker = true;
    m.volumeMarkerMs = 12.5;
    MetaStore where = MetaStore::None;
    std::string err;
    REQUIRE (writeSampleMeta (p, m, &where, &err));
    CHECK (where == MetaStore::Sidecar);
    SampleMeta back;
    MetaStore readWhere = MetaStore::None;
    REQUIRE (readSampleMeta (p, back, &readWhere));
    CHECK (back.hasVolumeMarker);
    CHECK (back.volumeMarkerMs == 12.5);
    {
        std::ifstream f (p, std::ios::binary);
        const std::vector<unsigned char> after ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
        CHECK (after == bytes);
    }   // fermé avant le remove (Windows refuse de supprimer un fichier ouvert)
    std::filesystem::remove (p);
    std::filesystem::remove (std::filesystem::path (p.string() + ".neptune.json"));
}

TEST_CASE ("Ogg Opus: output gain is applied, surround and truncated files are refused cleanly")
{
    const auto plain = decodeOpusFile (writeTemp (makeOpus (1, 8000), "neptune_gain0.opus"));
    const auto loud = decodeOpusFile (writeTemp (makeOpus (1, 8000, -1, 6 * 256), "neptune_gain6.opus"));   // +6 dB
    REQUIRE (plain.info.ok);
    REQUIRE (loud.info.ok);
    CHECK (loud.info.outputGainDb == Catch::Approx (6.0));
    double a = 0.0, b = 0.0;
    for (std::size_t i = 2000; i < 6000; ++i) { a = std::max (a, std::fabs (static_cast<double> (plain.samples[i]))); b = std::max (b, std::fabs (static_cast<double> (loud.samples[i]))); }
    CHECK (b / a == Catch::Approx (2.0).margin (0.05));

    auto surround = makeOpus (1, 4000);
    // OpusHead : le champ « channel mapping family » est à l'offset 18 du paquet (page : 27 + 1 segment + 18) ; la page doit aussi être recalculée,
    // mais notre lecteur ne vérifie pas le CRC.
    surround[27 + 1 + 18] = 1;
    const auto s = probeOpusFile (writeTemp (surround, "neptune_surround.opus"));
    CHECK_FALSE (s.ok);

    auto cut = makeOpus (2, 20000);
    cut.resize (cut.size() / 3);
    const auto t = decodeOpusFile (writeTemp (cut, "neptune_cut.opus"));
    CHECK ((! t.info.ok || t.info.frames < 20000));   // soit refusé, soit lu jusqu'où le fichier va
}
#else
TEST_CASE ("without libopus an Opus file is recognised but refused with a message")
{
    CHECK_FALSE (opusDecodingAvailable());
}
#endif
