// Banques SoundFont : lecture du RIFF, liste des presets, conversion d'un preset en zones (réglages du preset ajoutés à ceux de l'instrument, paires stéréo
// fusionnées), chemins virtuels d'échantillons pour le chargeur. Un SF2 minimal est construit à la main ; les vraies banques ne sont lues que si les variables
// d'environnement NEPTUNE_TEST_SF2 / NEPTUNE_TEST_SF3 les désignent.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>

#include "neptune/core/SampleLoader.h"
#include "neptune/core/SamplerVoices.h"
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
        { G (48, 100), G (34, 0), G (36, 0), G (37, 200), G (38, -1200), G (8, 9600),                                 // globale
          G (39, -100), G (40, 50), G (11, 2400), G (26, 0), G (28, 0), G (29, 500), G (30, -1200) },
        { R (43, 0, 59), G (54, 1), G (57, 3), G (46, 36), G (47, 100), G (53, 0) },                                         // mono
        { R (43, 60, 127), G (17, -500), G (54, 1), G (53, 1) },                                                      // gauche
        { R (43, 60, 127), G (17, 500), G (54, 1), G (53, 2) } };                                                      // droite
    const std::vector<std::vector<GenDef>> inst1 = { { R (43, 20, 30), G (53, 0) } };
    const std::vector<std::vector<std::vector<GenDef>>> insts = { inst0, inst1 };
    const std::vector<std::vector<std::vector<GenDef>>> presets = { { { G (51, 12), G (41, 0) } }, { { R (43, 0, 10), G (41, 1) } }, { { G (41, 0) }, { G (41, 1) }, { G (41, 0) } } };

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
        putName (phdr, "Preset" + std::to_string (p)); put16 (phdr, static_cast<int> (p)); put16 (phdr, 0); put16 (phdr, pbagNo); put32 (phdr, 0); put32 (phdr, 0); put32 (phdr, 0);
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
    REQUIRE (info.presets.size() == 3);
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

    // loi de vélocité des SoundFonts, note et vélocité imposées, suivi de la touche de l'enveloppe, enveloppe de modulation vers le filtre
    CHECK (mono->velExponent == Approx (2.0));
    CHECK (mono->fixedKey == 36);
    CHECK (mono->fixedVelocity == 100);
    CHECK (stereo->fixedKey == -1);
    CHECK (mono->envHoldKeyTrack == -100);
    CHECK (mono->envDecayKeyTrack == 50);
    CHECK (mono->filters.filter[0].envDepthOctaves == Approx (2.0));
    CHECK (mono->filters.filter[0].env.attack.ms == Approx (1000.0));
    CHECK (mono->filters.filter[0].env.sustain == Approx (0.5));
    CHECK (mono->filters.filter[0].env.decay.ms == Approx (500.0));   // 1 s pour une chute complète, moitié de chute jusqu'au niveau tenu
    CHECK (mono->filters.filter[0].env.release.ms == Approx (500.0));
    for (const auto& w : r.warnings) CHECK (w.find ("fixed") == std::string::npos);

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

    CHECK (mono->layer == 0);
    CHECK (stereo->layer == 0);

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

TEST_CASE ("soundfont: level per key of a real preset", "[soundfont][realbank][level]")
{
    const char* sf2 = std::getenv ("NEPTUNE_TEST_SF2");
    const char* name = std::getenv ("NEPTUNE_TEST_PRESET");
    if (sf2 == nullptr || name == nullptr) { SUCCEED ("no real bank given"); return; }
    const auto bank = sf::readBank (sf2);
    REQUIRE (bank.ok);
    int idx = -1;
    for (std::size_t i = 0; i < bank.presets.size(); ++i) if (bank.presets[i].name == name) { idx = static_cast<int> (i); break; }
    REQUIRE (idx >= 0);
    auto r = sf::importPreset (sf2, idx);
    REQUIRE (r.ok);
    if (const char* lo = std::getenv ("NEPTUNE_TEST_SAMPLE_FROM"))   // ne garder que les zones dont l'échantillon est dans [from, to]
    {
        const int from = std::atoi (lo), to = std::atoi (std::getenv ("NEPTUNE_TEST_SAMPLE_TO"));
        std::vector<ZoneDescription> kept;
        for (const auto& z : r.instrument.zones)
        {
            sf::SampleRef ref;
            if (sf::parseSamplePath (z.samplePath, ref) && ref.left >= from && ref.left <= to) kept.push_back (z);
        }
        r.instrument.zones = kept;
        std::printf ("zones kept: %zu\n", kept.size());
    }
    const auto inst = buildInstrument (r.instrument);
    REQUIRE (inst != nullptr);
    SamplerVoices voices;
    voices.prepare (48000.0);
    voices.setInstrument (inst.get());
    for (int key = 24; key <= 108; key += 3)
    {
        voices.noteOn (0, key, 100.0f / 127.0f);
        std::vector<float> l (24000, 0.0f), rr (24000, 0.0f);
        for (int pos = 0; pos < 24000; pos += 256)
            voices.render (l.data() + pos, rr.data() + pos, std::min (256, 24000 - pos), 1.0f, 1.0f, 0.0);
        double sum = 0, peak = 0;
        for (const float v : l) { sum += static_cast<double> (v) * v; peak = std::max<double> (peak, std::fabs (v)); }
        std::printf ("key %3d  rms %6.1f dB  peak %6.1f dB\n", key, 10.0 * std::log10 (sum / l.size() + 1e-12), 20.0 * std::log10 (peak + 1e-9));
        voices.noteOff (0, key);
        for (int k = 0; k < 40; ++k) voices.render (l.data(), rr.data(), 256, 1.0f, 1.0f, 0.0);
    }
}

TEST_CASE ("soundfont: layers of a preset play together", "[soundfont]")
{
    TempBank t;
    // trois couches : Inst0 (mono 0-59 + paire stéréo 60-127), Inst1 (notes 20 à 30), puis Inst0 de nouveau
    const auto r = sf::importPreset (t.path, 2);
    REQUIRE (r.ok);
    std::map<int, int> perLayer;
    for (const auto& z : r.instrument.zones) ++perLayer[z.layer];
    CHECK (perLayer[0] == 2);   // Inst0 : mono + paire stéréo fusionnée
    CHECK (perLayer[1] == 1);   // Inst1
    CHECK (perLayer[2] == 2);   // Inst0 en seconde couche
    CHECK (perLayer.size() == 3);

    // dans le moteur : le la 25 est couvert par une zone de chaque couche, les trois jouent ; le do 70 par les deux couches d'Inst0 (la paire stéréo, deux fois)
    const auto inst = buildInstrument (r.instrument);
    REQUIRE (inst != nullptr);
    SamplerVoices voices;
    voices.prepare (48000.0);
    voices.setInstrument (inst.get());
    voices.noteOn (0, 25, 1.0f);
    CHECK (voices.activeVoices() == 3);
    voices.noteOn (0, 70, 1.0f);
    CHECK (voices.activeVoices() == 5);
}

TEST_CASE ("sampler: zones of different layers play together, zones of one layer stay alternatives", "[sampler]")
{
    InstrumentDescription d;
    auto zone = [] (const char* path, int layer)
    {
        ZoneDescription z;
        z.samplePath = path;
        z.layer = layer;
        return z;
    };
    CHECK (sanitised (zone ("a.wav", 99)).layer == ZoneDescription::maxLayers - 1);
    CHECK (sanitised (zone ("a.wav", -4)).layer == 0);
    d.zones = { zone ("a.wav", 0), zone ("b.wav", 1) };
    const auto back = instrumentFromJson (toJson (d));
    REQUIRE (back.zones.size() == 2);
    CHECK (back.zones[0].layer == 0);
    CHECK (back.zones[1].layer == 1);
}

TEST_CASE ("soundfont: velocity follows the squared law, and fixed key and velocity ignore the played ones", "[soundfont]")
{
    TempBank t;
    const auto r = sf::importPreset (t.path, 0);
    REQUIRE (r.ok);
    const auto inst = buildInstrument (r.instrument);
    REQUIRE (inst != nullptr);

    auto render = [&] (int key, float velocity)
    {
        SamplerVoices voices;
        voices.prepare (48000.0);
        voices.setInstrument (inst.get());
        voices.noteOn (0, key, velocity);
        std::vector<float> l (2048, 0.0f), rr (2048, 0.0f);
        for (int block = 0; block < 4; ++block) voices.render (l.data() + block * 512, rr.data() + block * 512, 512, 1.0f, 1.0f, 0.0);
        return l;
    };
    auto energy = [] (const std::vector<float>& v) { double e = 0.0; for (const float x : v) e += std::abs (static_cast<double> (x)); return e; };

    // zone sans note ni vélocité imposées (touches 60 à 127) : le volume suit le carré de la vélocité
    const double loud = energy (render (72, 1.0f));
    const double soft = energy (render (72, 0.5f));
    REQUIRE (loud > 0.0);
    CHECK (soft / loud == Approx (0.25).epsilon (0.02));

    // zone à note 36 et vélocité 100 imposées : la touche et la force jouées ne changent rien au son
    const auto a = render (20, 1.0f);
    const auto b = render (50, 0.3f);
    REQUIRE (energy (a) > 0.0);
    CHECK (a == b);
}

TEST_CASE ("soundfont: the new zone fields survive saving and are bounded", "[soundfont]")
{
    ZoneDescription z;
    z.samplePath = "a.wav";
    z.velExponent = 2.0; z.fixedKey = 36; z.fixedVelocity = 90; z.envHoldKeyTrack = -50; z.envDecayKeyTrack = 75;
    InstrumentDescription d;
    d.zones = { z };
    const auto back = instrumentFromJson (toJson (d));
    REQUIRE (back.zones.size() == 1);
    CHECK (back.zones[0] == z);
    z.velExponent = 99.0; z.fixedKey = 500; z.fixedVelocity = -7; z.envHoldKeyTrack = 99999;
    const auto s = sanitised (z);
    CHECK (s.velExponent == Approx (4.0));
    CHECK (s.fixedKey == 127);
    CHECK (s.fixedVelocity == -1);
    CHECK (s.envHoldKeyTrack == 1200);
}
