// Banques SoundFont : lecture du RIFF, liste des presets, conversion d'un preset en zones (réglages du preset ajoutés à ceux de l'instrument, paires stéréo
// fusionnées), chemins virtuels d'échantillons pour le chargeur. Un SF2 minimal est construit à la main ; les vraies banques ne sont lues que si les variables
// d'environnement NEPTUNE_TEST_SF2 / NEPTUNE_TEST_SF3 les désignent.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "neptune/core/SampleLoader.h"
#include "neptune/core/SoundFont.h"

using namespace neptune::core;
using Catch::Approx;
namespace sf = neptune::core::soundfont;

namespace
{
using Bytes = std::vector<std::uint8_t>;

void put16 (Bytes& b, int v) { b.push_back (static_cast<std::uint8_t> (v & 0xff)); b.push_back (static_cast<std::uint8_t> ((v >> 8) & 0xff)); }
void put32 (Bytes& b, std::uint32_t v) { put16 (b, static_cast<int> (v & 0xffff)); put16 (b, static_cast<int> (v >> 16)); }
void putName (Bytes& b, const std::string& s) { for (std::size_t i = 0; i < 20; ++i) b.push_back (i < s.size() ? static_cast<std::uint8_t> (s[i]) : 0); }

Bytes chunk (const char* id, const Bytes& data)
{
    Bytes b (id, id + 4);
    put32 (b, static_cast<std::uint32_t> (data.size()));
    b.insert (b.end(), data.begin(), data.end());
    if (data.size() & 1u) b.push_back (0);
    return b;
}
Bytes list (const char* type, const Bytes& content)
{
    Bytes d (type, type + 4);
    d.insert (d.end(), content.begin(), content.end());
    return chunk ("LIST", d);
}

struct GenDef { int oper, lo, hi; };
GenDef G (int oper, int v) { return { oper, v & 0xff, (v >> 8) & 0xff }; }
GenDef R (int oper, int lo, int hi) { return { oper, lo, hi }; }

struct SampleDef { const char* name; std::uint32_t start, end, loopStart, loopEnd, rate; int pitch, link, type; };

// Une banque : 3 échantillons (mono, paire gauche/droite) dont la valeur du n-ième point est n ; 2 instruments ; 2 presets.
Bytes buildBank()
{
    const std::array<SampleDef, 3> samples = { { { "Mono", 0, 1000, 100, 900, 22050, 60, 0, 1 },
                                                 { "St-L", 1000, 1500, 1100, 1400, 22050, 60, 2, 4 },
                                                 { "St-R", 1500, 2000, 1600, 1900, 22050, 60, 1, 2 } } };
    Bytes smpl;
    for (int i = 0; i < 2000; ++i) put16 (smpl, i);

    Bytes info;
    { Bytes d; put16 (d, 2); put16 (d, 1); info = chunk ("ifil", d); }
    { Bytes d { 'T', 'e', 's', 't', 0, 0 }; const auto c = chunk ("INAM", d); info.insert (info.end(), c.begin(), c.end()); }

    Bytes sdta = chunk ("smpl", smpl);

    // instruments : chaque zone = liste de générateurs ; la zone globale est la première sans sampleID
    const std::vector<std::vector<GenDef>> inst0 = {
        { G (48, 100), G (34, 0), G (36, 0), G (37, 200), G (38, -1200), G (8, 9600) },                              // globale
        { R (43, 0, 59), G (54, 1), G (57, 3), G (53, 0) },                                                           // mono
        { R (43, 60, 127), G (17, -500), G (54, 1), G (53, 1) },                                                      // gauche
        { R (43, 60, 127), G (17, 500), G (54, 1), G (53, 2) } };                                                      // droite
    const std::vector<std::vector<GenDef>> inst1 = { { R (43, 20, 30), G (53, 0) } };
    const std::vector<std::vector<std::vector<GenDef>>> insts = { inst0, inst1 };
    const std::vector<std::vector<std::vector<GenDef>>> presets = { { { G (51, 12), G (41, 0) } }, { { R (43, 0, 10), G (41, 1) } } };

    Bytes ibag, igen, imod, instR, pbag, pgen, pmod, phdr;
    int bagNo = 0, genNo = 0;
    for (std::size_t i = 0; i < insts.size(); ++i)
    {
        putName (instR, i == 0 ? "Inst0" : "Inst1"); put16 (instR, bagNo);
        for (const auto& z : insts[i])
        {
            put16 (ibag, genNo); put16 (ibag, 0);
            ++bagNo;
            for (const auto& g : z) { put16 (igen, g.oper); igen.push_back (static_cast<std::uint8_t> (g.lo)); igen.push_back (static_cast<std::uint8_t> (g.hi)); ++genNo; }
        }
    }
    putName (instR, "EOI"); put16 (instR, bagNo);
    put16 (ibag, genNo); put16 (ibag, 0);
    put16 (igen, 0); put16 (igen, 0);
    for (int k = 0; k < 10; ++k) imod.push_back (0);

    int pbagNo = 0, pgenNo = 0;
    for (std::size_t p = 0; p < presets.size(); ++p)
    {
        putName (phdr, p == 0 ? "Preset0" : "Preset1"); put16 (phdr, static_cast<int> (p)); put16 (phdr, 0); put16 (phdr, pbagNo); put32 (phdr, 0); put32 (phdr, 0); put32 (phdr, 0);
        for (const auto& z : presets[p])
        {
            put16 (pbag, pgenNo); put16 (pbag, 0);
            ++pbagNo;
            for (const auto& g : z) { put16 (pgen, g.oper); pgen.push_back (static_cast<std::uint8_t> (g.lo)); pgen.push_back (static_cast<std::uint8_t> (g.hi)); ++pgenNo; }
        }
    }
    putName (phdr, "EOP"); put16 (phdr, 255); put16 (phdr, 0); put16 (phdr, pbagNo); put32 (phdr, 0); put32 (phdr, 0); put32 (phdr, 0);
    put16 (pbag, pgenNo); put16 (pbag, 0);
    put16 (pgen, 0); put16 (pgen, 0);
    for (int k = 0; k < 10; ++k) pmod.push_back (0);

    Bytes shdr;
    for (const auto& s : samples)
    {
        putName (shdr, s.name); put32 (shdr, s.start); put32 (shdr, s.end); put32 (shdr, s.loopStart); put32 (shdr, s.loopEnd); put32 (shdr, s.rate);
        shdr.push_back (static_cast<std::uint8_t> (s.pitch)); shdr.push_back (0); put16 (shdr, s.link); put16 (shdr, s.type);
    }
    putName (shdr, "EOS");
    for (int k = 0; k < 26; ++k) shdr.push_back (0);

    Bytes pdta;
    for (const auto& c : { chunk ("phdr", phdr), chunk ("pbag", pbag), chunk ("pmod", pmod), chunk ("pgen", pgen), chunk ("inst", instR), chunk ("ibag", ibag), chunk ("imod", imod),
                           chunk ("igen", igen), chunk ("shdr", shdr) })
        pdta.insert (pdta.end(), c.begin(), c.end());

    Bytes body { 's', 'f', 'b', 'k' };
    for (const auto& c : { list ("INFO", info), list ("sdta", sdta), list ("pdta", pdta) }) body.insert (body.end(), c.begin(), c.end());
    Bytes file = chunk ("RIFF", body);
    return file;
}

struct TempBank
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "neptune_soundfont_test.sf2";
    TempBank()
    {
        const auto b = buildBank();
        std::ofstream out (path, std::ios::binary);
        out.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
    }
    ~TempBank() { std::error_code ec; std::filesystem::remove (path, ec); }
};
} // namespace

TEST_CASE ("soundfont: the bank index lists presets and samples", "[soundfont]")
{
    TempBank t;
    const auto info = sf::readBank (t.path);
    REQUIRE (info.ok);
    CHECK (info.name == "Test");
    CHECK (info.versionMajor == 2);
    CHECK_FALSE (info.compressed);
    CHECK (info.samples == 3);
    REQUIRE (info.presets.size() == 2);
    CHECK (info.presets[0].name == "Preset0");
    CHECK (info.presets[1].program == 1);

    CHECK_FALSE (sf::readBank (t.path.parent_path() / "missing_bank.sf2").ok);
}

TEST_CASE ("soundfont: a preset becomes zones, with preset values added to instrument values", "[soundfont]")
{
    TempBank t;
    const auto r = sf::importPreset (t.path, 0);
    REQUIRE (r.ok);
    CHECK (r.name == "Preset0");
    CHECK (r.instrument.soundfontPreset == 0);
    CHECK (r.instrument.soundfontFile == t.path.u8string());
    REQUIRE (r.zones == 2);   // le mono + la paire stéréo fusionnée

    const ZoneDescription* mono = nullptr;
    const ZoneDescription* stereo = nullptr;
    for (const auto& z : r.instrument.zones) (z.keyLow == 0 ? mono : stereo) = &z;
    REQUIRE (mono != nullptr);
    REQUIRE (stereo != nullptr);

    CHECK (mono->keyHigh == 59);
    CHECK (mono->samplePath == t.path.u8string() + "#0");
    CHECK (mono->rootNote == 60);
    CHECK (mono->mode == PlayMode::Loop);
    CHECK (mono->loopStart == 100);
    CHECK (mono->loopEnd == 900);
    CHECK (mono->gainDb == Approx (-10.0));
    CHECK (mono->tuneCents == Approx (1200.0));   // accord grossier du preset (+12 demi-tons)
    CHECK (mono->chokeCuts == 3);
    CHECK (mono->chokeCutBy == 3);
    REQUIRE (mono->envelopeOverride);
    CHECK (mono->envelope.attack.ms == Approx (1000.0));
    CHECK (mono->envelope.sustain == Approx (0.1));
    CHECK (mono->envelope.decay.ms == Approx (200.0));   // 1 s pour 100 dB, 20 dB de chute jusqu'au sustain
    CHECK (mono->envelope.release.ms == Approx (500.0));
    REQUIRE (mono->filterOverride);
    CHECK (mono->filters.filter[0].on);
    CHECK (mono->filters.filter[0].cutoffHz == Approx (2093.0).margin (3.0));

    CHECK (stereo->samplePath == t.path.u8string() + "#1+2");
    CHECK (stereo->pan == Approx (0.0));
    CHECK (stereo->loopStart == 100);   // 1100 - début de l'échantillon 1000
    CHECK (stereo->loopEnd == 400);

    // deux imports du même preset donnent les mêmes zones (l'éditeur s'en sert pour savoir si les zones ont été modifiées à la main), et la banque survit à la sauvegarde
    const auto again = sf::importPreset (t.path, 0);
    CHECK (again.instrument.zones == r.instrument.zones);
    const auto restored = instrumentFromJson (toJson (r.instrument));
    CHECK (restored.soundfontFile == t.path.u8string());
    CHECK (restored.soundfontPreset == 0);
    CHECK (restored.zones == r.instrument.zones);

    const auto empty = sf::importPreset (t.path, 1);   // l'étendue de notes du preset ne rencontre pas celle de l'instrument
    CHECK_FALSE (empty.ok);
    CHECK_FALSE (sf::importPreset (t.path, 7).ok);
}

TEST_CASE ("soundfont: virtual sample paths load, probe and stream like files", "[soundfont]")
{
    TempBank t;
    sf::SampleRef ref;
    REQUIRE (sf::parseSamplePath (t.path.u8string() + "#1+2", ref));
    CHECK (ref.left == 1);
    CHECK (ref.right == 2);
    CHECK (ref.bank == t.path);
    CHECK_FALSE (sf::parseSamplePath ("/tmp/some.wav", ref));
    CHECK_FALSE (sf::parseSamplePath (t.path.u8string() + "#x", ref));
    CHECK_FALSE (sf::parseSamplePath (t.path.u8string() + "#1+", ref));
    CHECK_FALSE (sf::parseSamplePath ("/tmp/folder#3/file.wav#4", ref));

    const auto path = std::filesystem::u8path (sf::samplePath (t.path, 1, 2));
    const auto info = probeAudioFile (path);
    REQUIRE (info.ok);
    CHECK (info.channels == 2);
    CHECK (info.frames == 500);
    CHECK (info.sampleRate == Approx (22050.0));
    CHECK (info.keptAsInt16);

    const auto loaded = loadSampleFile (path, 64);   // le préchargement demandé est ignoré : jamais en streaming
    REQUIRE (loaded.ok());
    CHECK (loaded.sample->frames == 500);
    CHECK_FALSE (loaded.sample->isStreamed());
    REQUIRE (loaded.sample->isInt16());
    CHECK (loaded.sample->at (0, 0) == Approx (1000.0f / 32768.0f));
    CHECK (loaded.sample->at (1, 0) == Approx (1500.0f / 32768.0f));
    CHECK (loaded.sample->at (1, 499) == Approx (1999.0f / 32768.0f));

    AudioStreamReader reader;
    REQUIRE (reader.open (path));
    CHECK (reader.channels() == 2);
    CHECK (reader.totalFrames() == 500);
    std::vector<float> buf (8);
    CHECK (reader.read (buf.data(), 4) == 4);
    CHECK (buf[0] == Approx (1000.0f / 32768.0f));
    CHECK (buf[1] == Approx (1500.0f / 32768.0f));

    CHECK_FALSE (probeAudioFile (std::filesystem::u8path (sf::samplePath (t.path, 9))).ok);
}

TEST_CASE ("soundfont: real banks (SF2 and SF3 give the same sounds)", "[soundfont][realbank]")
{
    const char* sf2 = std::getenv ("NEPTUNE_TEST_SF2");
    const char* sf3 = std::getenv ("NEPTUNE_TEST_SF3");
    if (sf2 == nullptr || sf3 == nullptr) { SUCCEED ("no real bank given"); return; }
    const auto a = sf::readBank (sf2), b = sf::readBank (sf3);
    REQUIRE (a.ok);
    REQUIRE (b.ok);
    CHECK_FALSE (a.compressed);
    CHECK (b.compressed);
    REQUIRE (a.presets.size() == b.presets.size());
    CHECK (a.samples == b.samples);
    INFO ("presets: " << a.presets.size() << ", samples: " << a.samples);

    for (const int idx : { 0, 100, 400, 800, 1200 })
    {
        const auto da = sf::decodeSample ({ sf2, idx, -1 }), db = sf::decodeSample ({ sf3, idx, -1 });
        REQUIRE (da.ok);
        REQUIRE (db.ok);
        INFO ("sample " << idx << ": frames " << da.frames << " vs " << db.frames);
        CHECK (std::llabs (da.frames - db.frames) <= 2048);
        const auto n = static_cast<std::size_t> (std::min (da.frames, db.frames));
        double sxy = 0, sxx = 0, syy = 0;
        for (std::size_t i = 0; i < n; ++i) { const double x = da.interleaved[i], y = db.interleaved[i]; sxy += x * y; sxx += x * x; syy += y * y; }
        CHECK (sxy / std::sqrt (sxx * syy + 1e-9) > 0.99);   // la compression Vorbis ne change pas la forme d'onde
    }

    int played = 0;
    for (std::size_t p = 0; p < a.presets.size(); ++p)
    {
        const auto ra = sf::importPreset (sf2, static_cast<int> (p)), rb = sf::importPreset (sf3, static_cast<int> (p));
        INFO ("preset " << p << " " << a.presets[p].name);
        CHECK (ra.ok == rb.ok);
        if (ra.ok) { CHECK (ra.zones == rb.zones); ++played; }
    }
    CHECK (played > 250);
}
