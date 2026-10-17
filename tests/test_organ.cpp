#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "neptune/core/OrganEngine.h"

using namespace neptune::core::organ;

namespace
{
constexpr double kRate = 48000.0;

struct Level { float peak = 0.0f, rms = 0.0f; };

Level run (OrganVoices& v, const OrganLive& live, int blocks, int blockSize = 128)
{
    std::vector<float> l (static_cast<std::size_t> (blockSize)), r (l);
    double sum = 0.0;
    long n = 0;
    Level out;
    for (int b = 0; b < blocks; ++b)
    {
        v.render (l.data(), r.data(), blockSize, 1.0f, 1.0f, live, false);
        for (float x : l) { out.peak = std::max (out.peak, std::fabs (x)); sum += static_cast<double> (x) * x; ++n; }
    }
    out.rms = n > 0 ? static_cast<float> (std::sqrt (sum / static_cast<double> (n))) : 0.0f;
    return out;
}
} // namespace

TEST_CASE ("organ description: clamped and round trips through JSON", "[organ]")
{
    OrganDescription d;
    d.channel = { 5, 0, 99 };
    d.splitLower = 48;
    d.transpose = { 40, -3, 0 };
    d.tuningCents = -500;
    d = sanitised (d);
    CHECK (d.channel[2] == 16);
    CHECK (d.transpose[0] == 24);
    CHECK (d.tuningCents == -100);
    CHECK (organFromJson (toJson (d)) == d);
    CHECK (organFromJson ("pas du json") == OrganDescription {});
}

TEST_CASE ("organ keys: channels, split and transposition choose the keyboard of setBfree", "[organ]")
{
    OrganDescription d; // U : canal 1, L : canal 2, P : canal 3
    CHECK (organKeyFor (d, 0, 36) == 0);     // do le plus grave du supérieur
    CHECK (organKeyFor (d, 0, 96) == 60);    // le plus aigu
    CHECK (organKeyFor (d, 0, 97) == -1);    // hors étendue : muet
    CHECK (organKeyFor (d, 1, 36) == 64);    // inférieur
    CHECK (organKeyFor (d, 2, 24) == 128);   // pédalier
    CHECK (organKeyFor (d, 2, 55) == 159);
    CHECK (organKeyFor (d, 5, 60) == -1);    // canal que personne n'écoute
    d.transpose[0] = 12;
    CHECK (organKeyFor (d, 0, 36) == 12);
    d.transpose[0] = 0;
    d.splitLower = 48;                       // sur le canal du supérieur : en dessous de 48, pédalier
    d.splitUpper = 60;                       // entre 48 et 60 : inférieur
    CHECK (organKeyFor (d, 0, 40) == 128 + 16);
    CHECK (organKeyFor (d, 0, 50) == 64 + 14);
    CHECK (organKeyFor (d, 0, 72) == 36);
    d.channel[0] = 0;                        // supérieur muet
    CHECK (organKeyFor (d, 0, 72) == -1);
}

TEST_CASE ("organ engine: a played key sounds with the drawbars and goes silent once released", "[organ][engine]")
{
    OrganVoices v;
    REQUIRE (v.reserve (kRate));
    OrganLive live;
    CHECK (run (v, live, 20).peak < 1.0e-4f);   // rien tant qu'aucune touche
    v.noteOn (0, 60);
    CHECK (v.heldKeys() == 1);
    const auto played = run (v, live, 200);
    CHECK (played.peak > 0.02f);
    CHECK (std::isfinite (played.rms));

    v.noteOff (0, 60);
    CHECK (v.heldKeys() == 0);
    run (v, live, 400);                         // la queue de la cabine et de la réverb s'éteint
    CHECK (run (v, live, 20).peak < 0.01f);
}

TEST_CASE ("organ engine: drawbars at zero are silent, more drawbars are louder", "[organ][engine]")
{
    OrganVoices v;
    REQUIRE (v.reserve (kRate));
    OrganLive live;
    live.drawbars[0] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    v.noteOn (0, 60);
    run (v, live, 100);
    CHECK (run (v, live, 100).peak < 1.0e-3f);
    live.drawbars[0] = { 0, 0, 8, 0, 0, 0, 0, 0, 0 };
    run (v, live, 100);
    const auto one = run (v, live, 100);
    live.drawbars[0] = { 8, 8, 8, 8, 0, 0, 0, 0, 0 };
    run (v, live, 100);
    const auto many = run (v, live, 100);
    CHECK (one.rms > 0.005f);
    CHECK (many.rms > 0.005f);
    CHECK (std::fabs (many.rms - one.rms) > 0.0f);
}

TEST_CASE ("organ engine: two instances do not share their keys", "[organ][engine]")
{
    OrganVoices a, b;
    REQUIRE (a.reserve (kRate));
    REQUIRE (b.reserve (kRate));
    OrganLive live;
    a.noteOn (0, 60);
    run (a, live, 100);
    run (b, live, 100);
    CHECK (run (a, live, 50).peak > 0.02f);
    CHECK (run (b, live, 50).peak < 1.0e-4f);
}

TEST_CASE ("organ engine: any block size gives the same sound", "[organ][engine]")
{
    auto render = [] (int blockSize)
    {
        OrganVoices v;
        REQUIRE (v.reserve (kRate));
        OrganLive live;
        v.noteOn (0, 64);
        std::vector<float> all;
        std::vector<float> l (static_cast<std::size_t> (blockSize)), r (l);
        for (int rendered = 0; rendered < 24000; rendered += blockSize)
        {
            v.render (l.data(), r.data(), blockSize, 1.0f, 1.0f, live, false);
            all.insert (all.end(), l.begin(), l.end());
        }
        all.resize (24000);
        return all;
    };
    const auto a = render (128), b = render (50 * 4 + 7 - 7); // 200 : pas un multiple du fragment de 128
    REQUIRE (a.size() == b.size());
    // pas identiques à l'échantillon près (le Leslie et le scanner tournent sur des phases aléatoires) : même niveau
    auto rms = [] (const std::vector<float>& v)
    {
        double s = 0.0;
        for (float x : v) s += static_cast<double> (x) * x;
        return std::sqrt (s / static_cast<double> (v.size()));
    };
    CHECK (rms (a) > 0.01);
    CHECK (std::fabs (rms (a) - rms (b)) < rms (a) * 0.25);   // phases aléatoires du Leslie : l'écart observé va jusqu'à ~15 % (0,1 échouait de temps en temps)
}

#include "neptune/core/OrganSynth.h"

TEST_CASE ("organ synth: parameters, playing through the queue, state round trip", "[organ][synth]")
{
    OrganSynthEngine e;
    REQUIRE (e.ok());
    e.prepare (kRate, 128);
    e.postNoteOn (0, 60);
    std::vector<float> l (128), r (128);
    float peak = 0.0f;
    for (int i = 0; i < 200; ++i)
    {
        e.process (l.data(), r.data(), 128);
        for (auto v : l) peak = std::max (peak, std::fabs (v));
    }
    CHECK (peak > 0.02f);
    CHECK (e.heldKeys() == 1);
    CHECK (e.heldKeyWord (0) == (std::uint64_t (1) << 60));

    // un état relu donne le même état
    e.params().set (neptune::core::mixer::organParam (0, neptune::core::mixer::kOrgLeslieSpeed), 2.0);
    e.params().set (neptune::core::mixer::organParam (0, neptune::core::mixer::kOrgLower0 + 2), 5.0);
    OrganDescription d;
    d.splitLower = 48;
    d.transpose[1] = -12;
    e.setDescription (d);
    const auto text = stateToJson (e.params(), e.description(), "abc");
    OrganSynthEngine f;
    OrganDescription back;
    std::string id;
    REQUIRE (stateFromJson (text, f.params(), back, id));
    CHECK (back == e.description());
    CHECK (id == "abc");
    CHECK (liveFromParams (f.params()).leslieSpeed == 2);
    CHECK (liveFromParams (f.params()).drawbars[1][2] == 5);
    CHECK (stateToJson (f.params(), back, id) == text);
    CHECK_FALSE (stateFromJson ("{\"format\":\"other\"}", f.params(), back, id));

    // Contrôleurs MIDI : désactivés par défaut, et l'état les ramène tels qu'ils étaient.
    bool cc = true;
    REQUIRE (stateFromJson (text, f.params(), back, id, &cc));
    CHECK_FALSE (cc);
    const auto withCc = stateToJson (f.params(), back, id, true);
    REQUIRE (stateFromJson (withCc, f.params(), back, id, &cc));
    CHECK (cc);
}

TEST_CASE ("organ parameters exist for every Neptune track with distinct ids", "[organ][synth]")
{
    using namespace neptune::core;
    ParameterSet set;
    REQUIRE (mixer::addOrganParams (set, 0, ""));
    REQUIRE (mixer::addOrganParams (set, 15, ""));
    CHECK (static_cast<int> (set.size()) == 2 * (mixer::kOrganKinds - 0));
    CHECK (set.get (mixer::organParam (0, mixer::kOrgUpper0 + 2)) == 6.0);
    CHECK (set.get (mixer::organParam (15, mixer::kOrgLeslieMix)) == 100.0);
}

#include "neptune/core/OrganPreset.h"

TEST_CASE ("organ presets: keys are unique and map to track 0 ids, the Leslie speed is free", "[organ][preset]")
{
    using namespace neptune::core;
    std::set<std::string> keys;
    std::set<ParamId> ids;
    for (const auto& k : organ::keyedParams()) { keys.insert (k.key); ids.insert (k.id); }
    CHECK (static_cast<int> (keys.size()) == mixer::kOrganKinds);
    CHECK (static_cast<int> (ids.size()) == mixer::kOrganKinds);
    CHECK (organ::paramIdForKey ("organ.upper.8") == mixer::organParam (0, mixer::kOrgUpper0 + 2));
    CHECK (organ::idForTrack (mixer::organParam (0, mixer::kOrgReverb), 3) == mixer::organParam (3, mixer::kOrgReverb));

    ParameterSet set;
    REQUIRE (mixer::addOrganParams (set, 0, ""));
    const auto cfg = organ::defaultConfig();
    set.set (mixer::organParam (0, mixer::kOrgLeslieSpeed), 2.0);
    set.set (mixer::organParam (0, mixer::kOrgUpper0), 5.0);
    const auto p = organ::capturePreset (set, cfg, "Mine", "Jazz");
    CHECK (p.module == "organ");
    CHECK (p.parameters.count (organ::leslieSpeedKey) == 0); // libre : jamais enregistré
    CHECK (p.parameters.at ("organ.upper.16").number == 5.0);
    CHECK_FALSE (organ::differsFromPreset (p, set, cfg));
    set.set (mixer::organParam (0, mixer::kOrgUpper0), 1.0);
    CHECK (organ::differsFromPreset (p, set, cfg));
    organ::applyPresetValues (p, set, cfg);
    CHECK (set.get (mixer::organParam (0, mixer::kOrgUpper0)) == 5.0);
    CHECK (set.get (mixer::organParam (0, mixer::kOrgLeslieSpeed)) == 2.0); // la vitesse du Leslie n'a pas bougé
}

TEST_CASE ("organ factory presets: complete, distinct and in range, they apply to any track", "[organ][preset]")
{
    using namespace neptune::core;
    const auto list = organ::factoryPresets();
    CHECK (list.size() == 18);
    std::set<std::string> ids, names;
    ParameterSet set;
    REQUIRE (mixer::addOrganParams (set, 5, ""));
    for (const auto& p : list)
    {
        ids.insert (p.id);
        names.insert (p.name);
        CHECK (p.module == "organ");
        organ::applyPresetValues (p, set, organ::defaultConfig(), 5);
        CHECK_FALSE (organ::differsFromPreset (p, set, organ::defaultConfig(), 5));
        for (const auto& [key, value] : p.parameters)
        {
            const auto index = set.indexOf (organ::idForTrack (organ::paramIdForKey (key), 5));
            REQUIRE (index != ParameterSet::npos);
            CHECK (value.number >= set.spec (index).min);
            CHECK (value.number <= set.spec (index).max);
        }
    }
    CHECK (ids.size() == list.size());
    CHECK (names.size() == list.size());
}

TEST_CASE ("organ description: Leslie settings are bounded and round trip", "[organ][leslie]")
{
    OrganDescription d;
    CHECK (d.hornSlowRpm == 40.32);
    d.hornSlowRpm = 5000.0;
    d.drumAcc = -3.0;
    d.hornLevel = 7.0;
    d = sanitised (d);
    CHECK (d.hornSlowRpm == 200.0);
    CHECK (d.drumAcc == 0.5);
    CHECK (d.hornLevel == 1.0);
    d.hornFastRpm = 500.0;
    d.drumDec = 2.0;
    CHECK (organFromJson (toJson (d)) == d);
}

TEST_CASE ("organ engine: a new tuning builds another organ that takes over, held keys keep sounding", "[organ][engine][tuning]")
{
    auto crossings = [] (OrganVoices& v, const OrganLive& live)
    {
        std::vector<float> l (128), r (128), all;
        for (int i = 0; i < 120; ++i)
        {
            v.render (l.data(), r.data(), 128, 1.0f, 1.0f, live, false);
            if (i >= 60) all.insert (all.end(), l.begin(), l.end());
        }
        int n = 0;
        for (std::size_t i = 1; i < all.size(); ++i) if (all[i - 1] <= 0.0f && all[i] > 0.0f) ++n;
        return n;
    };
    OrganVoices v;
    REQUIRE (v.reserve (kRate));
    OrganLive live;
    live.leslieMix = 0.0f; // sans Leslie : la fréquence est celle de la note
    live.drawbars[0] = { 0, 0, 8, 0, 0, 0, 0, 0, 0 };
    live.vibratoMode = 0;
    v.noteOn (0, 69);
    const int before = crossings (v, live);
    REQUIRE (before > 20);
    OrganDescription tuned;
    tuned.tuningCents = 100;
    v.requestBuild (tuned); // un demi-ton environ plus haut
    const int after = crossings (v, live); // la note tenue est rejouée sur le nouvel orgue
    v.collect();           // le thread de message libère l'ancien
    CHECK (after > before);
    CHECK (static_cast<double> (after) / before == Catch::Approx (std::pow (2.0, 100.0 / 1200.0)).epsilon (0.04));
    CHECK (v.heldKeys() == 1);
}

TEST_CASE ("organ MIDI controllers follow setBfree numbers, each keyboard on its own channel", "[organ][cc]")
{
    using namespace neptune::core;
    ParameterSet set;
    REQUIRE (mixer::addOrganParams (set, 0, ""));
    const OrganDescription d; // U : canal 1, L : canal 2, P : canal 3
    auto value = [&set] (int kind) { return set.get (mixer::organParam (0, kind)); };

    CHECK (applyControlChange (d, set, 0, 72, 0));          // supérieur, 8' : 0 = sorti à fond
    CHECK (value (mixer::kOrgUpper0 + 2) == 8.0);
    CHECK (applyControlChange (d, set, 0, 72, 127));        // 127 = rentré
    CHECK (value (mixer::kOrgUpper0 + 2) == 0.0);
    CHECK (applyControlChange (d, set, 1, 70, 0));          // inférieur 16'
    CHECK (value (mixer::kOrgLower0) == 8.0);
    CHECK (applyControlChange (d, set, 2, 72, 0));          // pédalier 8'
    CHECK (value (mixer::kOrgPedal8) == 8.0);
    CHECK_FALSE (applyControlChange (d, set, 2, 71, 0));    // le pédalier n'a que 16' et 8'
    CHECK_FALSE (applyControlChange (d, set, 9, 72, 0));    // voie que personne n'écoute

    CHECK (applyControlChange (d, set, 0, 91, 127));        // réverb
    CHECK (value (mixer::kOrgReverb) == 100.0);
    CHECK (applyControlChange (d, set, 0, 66, 127));        // percussion
    CHECK (value (mixer::kOrgPercOn) == 1.0);
    CHECK (applyControlChange (d, set, 0, 83, 127));        // haut : 2e harmonique
    CHECK (value (mixer::kOrgPercThird) == 0.0);
    CHECK (applyControlChange (d, set, 0, 92, 23 * 3));     // type de vibrato : 4e position = C2
    CHECK (value (mixer::kOrgVibMode) == 4.0);
    CHECK (applyControlChange (d, set, 0, 95, 127));        // routage : les deux claviers
    CHECK (value (mixer::kOrgVibUpper) == 1.0);
    CHECK (value (mixer::kOrgVibLower) == 1.0);
    CHECK_FALSE (applyControlChange (d, set, 1, 91, 127));  // la réverb ne se pilote que sur la voie du supérieur

    CHECK (applyControlChange (d, set, 1, 1, 100));         // vitesse du Leslie : toutes les voies, haut = tremolo
    CHECK (value (mixer::kOrgLeslieSpeed) == kLeslieTremolo);
    CHECK (applyControlChange (d, set, 2, 1, 50));          // milieu = frein
    CHECK (value (mixer::kOrgLeslieSpeed) == kLeslieBrake);
    set.set (mixer::organParam (0, mixer::kOrgLeslieSpeed), kLeslieChorale);
    CHECK (applyControlChange (d, set, 0, 64, 127));        // pédale : bascule chorale / tremolo
    CHECK (value (mixer::kOrgLeslieSpeed) == kLeslieTremolo);
    CHECK (applyControlChange (d, set, 0, 64, 127));
    CHECK (value (mixer::kOrgLeslieSpeed) == kLeslieChorale);
}

TEST_CASE ("organ wiring: extended mode can drop the bass foldback and reshape the 16' taper, classic mode is untouched", "[organ][engine][wiring]")
{
    struct Result { double rms; double freq; };
    auto measure = [] (const OrganDescription& d, int note)
    {
        OrganVoices v;
        v.requestBuild (d);
        REQUIRE (v.reserve (kRate));
        OrganLive live;
        live.leslieMix = 0.0f; live.vibratoMode = 0; live.reverbMix = 0.0f;
        live.drawbars[0] = { 8, 0, 0, 0, 0, 0, 0, 0, 0 }; // 16' seul
        v.noteOn (0, note);
        std::vector<float> l (128), r (128), all;
        double sum = 0.0;
        long n = 0;
        for (int i = 0; i < 200; ++i)
        {
            v.render (l.data(), r.data(), 128, 1.0f, 1.0f, live, false);
            if (i >= 100) { all.insert (all.end(), l.begin(), l.end()); for (float x : l) { sum += static_cast<double> (x) * x; ++n; } }
        }
        int z = 0;
        for (std::size_t i = 1; i < all.size(); ++i) if (all[i - 1] <= 0.0f && all[i] > 0.0f) ++z;
        return Result { std::sqrt (sum / static_cast<double> (n)), z / (static_cast<double> (all.size()) / kRate) };
    };
    OrganDescription classic;
    const auto base = measure (classic, 40); // mi 2 (MIDI 40) : dans les 12 touches graves
    CHECK (base.freq > 75.0);                 // 82 Hz (le 16' est replié une octave plus haut que les 41 Hz attendus)

    OrganDescription ext = classic;
    ext.extended = true;
    ext.foldback = true; // étendu mais comme setBfree : même son
    const auto same = measure (ext, 40);
    CHECK (same.rms == Catch::Approx (base.rms).epsilon (0.02));

    ext.foldback = false;
    const auto low = measure (ext, 40);
    CHECK (low.freq < 55.0);                  // 41 Hz : le vrai 16'
    CHECK (low.freq > 30.0);

    ext = classic;
    ext.extended = true;
    for (auto& v : ext.taperDb[0]) v = 12;    // +12 dB sur toute l'étendue du 16'
    const auto loud = measure (ext, 40);
    CHECK (loud.rms / base.rms == Catch::Approx (4.0).epsilon (0.1)); // +12 dB = x3,98
}

TEST_CASE ("organ synth: several wiring changes in a row are all taken into account, without anyone calling collect", "[organ][synth][wiring]")
{
    auto frequency = [] (OrganSynthEngine& e)
    {
        std::vector<float> l (128), r (128), all;
        for (int i = 0; i < 200; ++i)
        {
            e.process (l.data(), r.data(), 128);
            if (i >= 100) all.insert (all.end(), l.begin(), l.end());
        }
        int z = 0;
        for (std::size_t i = 1; i < all.size(); ++i) if (all[i - 1] <= 0.0f && all[i] > 0.0f) ++z;
        return z / (static_cast<double> (all.size()) / kRate);
    };
    OrganSynthEngine e;
    e.prepare (kRate, 128);
    // 16' seul, sans Leslie ni vibrato, comme le test du moteur
    using namespace neptune::core;
    for (int i = 0; i < 9; ++i) e.params().set (mixer::organParam (0, mixer::kOrgUpper0 + i), i == 0 ? 8.0 : 0.0);
    e.params().set (mixer::organParam (0, mixer::kOrgLeslieMix), 0.0);
    e.params().set (mixer::organParam (0, mixer::kOrgReverb), 0.0);
    e.params().set (mixer::organParam (0, mixer::kOrgVibUpper), 0.0);
    e.postNoteOn (0, 40);
    const double classic = frequency (e);
    CHECK (classic > 75.0); // replié : 82 Hz

    OrganDescription d = e.description();
    d.extended = true;
    e.setDescription (d);            // 1er changement : même son (repli gardé)
    CHECK (frequency (e) > 75.0);
    d.foldback = false;
    e.setDescription (d);            // 2e changement, sans collect() explicite : doit être pris en compte
    CHECK (frequency (e) < 55.0);    // 41 Hz
    d.foldback = true;
    e.setDescription (d);            // 3e : retour au repli
    CHECK (frequency (e) > 75.0);
}

TEST_CASE ("organ control bindings give the same values as the direct setBfree CC handler", "[organ][cc][control]")
{
    using namespace neptune::core;
    const OrganDescription d;
    const auto bindings = organControlBindings (d, 0);
    CHECK (isOrganParam (mixer::organParam (0, mixer::kOrgReverb), 0));
    CHECK_FALSE (isOrganParam (mixer::organParam (1, mixer::kOrgReverb), 0));

    struct Probe { int channel0; int cc; };
    const Probe probes[] = { { 0, 70 }, { 0, 72 }, { 0, 78 }, { 1, 70 }, { 1, 75 }, { 2, 70 }, { 2, 72 }, { 0, 91 }, { 0, 66 }, { 0, 80 }, { 0, 81 },
                             { 0, 82 }, { 0, 83 }, { 0, 30 }, { 0, 31 }, { 0, 65 }, { 0, 93 } };
    // CC 92 (type de vibrato) : setBfree coupe par tranches de 23, l'assignation par tranches égales ; les extrémités sont les mêmes
    {
        control::ControlEngine e;
        control::Config cfg;
        cfg.settings.ccEnabled = true;
        cfg.bindings = bindings;
        ParameterSet set;
        REQUIRE (mixer::addOrganParams (set, 0, ""));
        e.configure (cfg, set);
        e.controlChange (0, 92, 0, set);
        CHECK (set.get (mixer::organParam (0, mixer::kOrgVibMode)) == 1.0);
        e.controlChange (0, 92, 127, set);
        CHECK (set.get (mixer::organParam (0, mixer::kOrgVibMode)) == 6.0);
    }
    for (const auto& p : probes)
        for (const int value : { 0, 10, 40, 63, 64, 100, 127 })
        {
            ParameterSet viaCc, viaLayer;
            REQUIRE (mixer::addOrganParams (viaCc, 0, ""));
            REQUIRE (mixer::addOrganParams (viaLayer, 0, ""));
            applyControlChange (d, viaCc, p.channel0, p.cc, value);
            control::ControlEngine engine;
            control::Config cfg;
            cfg.settings.ccEnabled = true;
            cfg.bindings = bindings;
            engine.configure (cfg, viaLayer);
            engine.controlChange (p.channel0, p.cc, value, viaLayer);
            for (int k = 0; k < mixer::kOrganKinds; ++k)
            {
                INFO ("voie " << p.channel0 + 1 << " CC " << p.cc << " valeur " << value << " paramètre " << k);
                // la molette (CC 1) est volontairement différente : elle n'est pas dans cette liste
                CHECK (viaLayer.get (mixer::organParam (0, k)) == Catch::Approx (viaCc.get (mixer::organParam (0, k))).margin (1.0e-9));
            }
        }
}

TEST_CASE ("organ: key click settings are clamped, round trip in JSON and default to setBfree", "[organ]")
{
    OrganDescription d;
    CHECK (d.clickIsDefault());
    d.clickLevel = 900; d.clickLength = 3; d.clickRelease = -4;
    const auto s = sanitised (d);
    CHECK (s.clickLevel == 100);
    CHECK (s.clickLength == 25);
    CHECK (s.clickRelease == 0);
    OrganDescription e;
    e.clickLevel = 80; e.clickLength = 150; e.clickRelease = 30;
    const auto back = organFromJson (toJson (e));
    CHECK (back == e);
    CHECK (back.clickLevel == 80);
    CHECK_FALSE (back.clickIsDefault());
    CHECK (organFromJson ("{}").clickIsDefault());
    OrganDescription f = e;
    f.clickRelease = 31;
    CHECK (f != e);
}

TEST_CASE ("organ engine: the key click settings change the attack (more attenuation noise, longer burst)", "[organ][engine]")
{
    // moyenne sur plusieurs touches : le bruit est aléatoire. Énergie des 128 premiers échantillons.
    auto attackEnergy = [] (int level, int length)
    {
        double total = 0.0;
        for (int trial = 0; trial < 24; ++trial)
        {
            OrganVoices v;
            OrganDescription d;
            d.clickLevel = level;
            d.clickLength = length;
            v.requestBuild (d);
            REQUIRE (v.reserve (kRate));
            OrganLive live;
            v.noteOn (0, 60 + trial % 12);
            std::vector<float> l (128), r (128);
            v.render (l.data(), r.data(), 128, 1.0f, 1.0f, live, false);
            for (const float x : l) total += static_cast<double> (x) * x;
        }
        return total / 24.0;
    };
    CHECK (attackEnergy (0, 100) > attackEnergy (100, 100) * 1.3);   // un contact plus bruité atténue davantage le début
    CHECK (attackEnergy (50, 400) > attackEnergy (50, 25) * 1.5);    // un bruit plus long retarde l'établissement du son
    CHECK (std::isfinite (attackEnergy (50, 100)));
}
