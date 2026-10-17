// OSC : codec, table figée des adresses, répartiteur (valeurs normalisées / réelles, catalogue, plafond).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <set>

#include "neptune/core/Mixer.h"
#include "neptune/core/Osc.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using Catch::Approx;

namespace
{
void buildFullPool (ParameterSet& set)
{
    std::string error;
    REQUIRE (probe::buildPool (set, mixer::numTracks, &error));
    REQUIRE (sampler::addParams (set, &error));
    REQUIRE (mixer::addParams (set, mixer::numTracks, mixer::numBuses, &error));
    REQUIRE (control::addMacroParams (set, &error));
}

osc::Message msg (const std::string& address, double value, char type = 'f')
{
    osc::Message m;
    m.address = address;
    m.args.push_back ({ type, value, {} });
    return m;
}
} // namespace

TEST_CASE ("osc: messages and bundles survive an encode / parse round trip", "[osc]")
{
    osc::Message a = msg ("/neptune/master/volume", 0.5);
    a.args.push_back ({ 's', 0.0, "hello" });
    a.args.push_back ({ 'i', -7.0, {} });
    a.args.push_back ({ 'T', 1.0, {} });
    const auto bytes = osc::encode (a);
    CHECK (bytes.size() % 4 == 0);

    std::vector<osc::Message> out;
    REQUIRE (osc::parsePacket (bytes.data(), bytes.size(), out));
    REQUIRE (out.size() == 1);
    CHECK (out[0].address == "/neptune/master/volume");
    REQUIRE (out[0].args.size() == 4);
    CHECK (out[0].args[0].number == Approx (0.5));
    CHECK (out[0].args[1].text == "hello");
    CHECK (out[0].args[2].number == Approx (-7.0));
    CHECK (out[0].args[3].number == Approx (1.0));

    const auto bundle = osc::encodeBundle ({ osc::encode (msg ("/a", 1.0)), osc::encode (msg ("/b", 2.0, 'i')) });
    out.clear();
    REQUIRE (osc::parsePacket (bundle.data(), bundle.size(), out));
    REQUIRE (out.size() == 2);
    CHECK (out[1].address == "/b");
    CHECK (out[1].args[0].number == Approx (2.0));
}

TEST_CASE ("osc: malformed packets are refused without reading past the end", "[osc]")
{
    std::vector<osc::Message> out;
    const std::uint8_t tiny[] = { '/', 'a' };
    CHECK_FALSE (osc::parsePacket (tiny, sizeof (tiny), out));
    auto bytes = osc::encode (msg ("/neptune/x", 1.0));
    bytes.resize (bytes.size() - 2);                       // argument tronqué
    CHECK_FALSE (osc::parsePacket (bytes.data(), bytes.size(), out));
    const std::uint8_t noSlash[] = { 'a', 'b', 'c', 0, ',', 0, 0, 0 };
    CHECK_FALSE (osc::parsePacket (noSlash, sizeof (noSlash), out));
    std::vector<std::uint8_t> bomb = osc::encodeBundle ({ { 0, 0, 0, 0 } });
    CHECK_FALSE (osc::parsePacket (bomb.data(), bomb.size(), out));
    // paquets groupés imbriqués trop profondément
    std::vector<std::uint8_t> nested = osc::encode (msg ("/a", 1.0));
    for (int i = 0; i < 8; ++i) nested = osc::encodeBundle ({ nested });
    out.clear();
    CHECK_FALSE (osc::parsePacket (nested.data(), nested.size(), out));
    CHECK (out.empty());
}

TEST_CASE ("osc: every parameter of the full pool has a unique, stable address", "[osc]")
{
    ParameterSet set;
    buildFullPool (set);
    std::set<std::string> seen;
    std::vector<ParamId> missing;
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        const auto id = set.spec (i).id;
        const auto& a = osc::addressFor (id);
        if (a.empty()) { missing.push_back (id); continue; }
        CHECK (seen.insert (a).second);
        ParamId back = 0;
        REQUIRE (osc::paramForAddress (a, back));
        CHECK (back == id);
    }
    INFO ("first id without address: " << (missing.empty() ? 0u : missing.front()) << ", count " << missing.size());
    CHECK (missing.empty());
}

TEST_CASE ("osc: the address table is frozen (golden addresses)", "[osc]")
{
    // Ne jamais modifier ces valeurs : les surfaces de contrôle et les scripts des utilisateurs en dépendent.
    CHECK (osc::addressFor (1) == "/neptune/master/volume");
    CHECK (osc::addressFor (5) == "/neptune/master/trim");
    CHECK (osc::addressFor (7) == "/neptune/master/insert/1/bypass");
    CHECK (osc::addressFor (100) == "/neptune/track/1/sampler/gain");
    CHECK (osc::addressFor (1000) == "/neptune/track/1/volume");
    CHECK (osc::addressFor (1001) == "/neptune/track/1/pan");
    CHECK (osc::addressFor (1010) == "/neptune/track/1/insert/1/bypass");
    CHECK (osc::addressFor (1027) == "/neptune/track/1/send/8/level");
    CHECK (osc::addressFor (1230) == "/neptune/track/3/sampler/gain");
    CHECK (osc::addressFor (1240) == "/neptune/track/3/synth/level_osc1");
    CHECK (osc::addressFor (1271) == "/neptune/track/3/synth/amp_attack");
    CHECK (osc::addressFor (1260) == "/neptune/track/3/fm/brightness");
    CHECK (osc::addressFor (1285) == "/neptune/track/3/fm/lfo_speed");
    CHECK (osc::addressFor (3000) == "/neptune/bus/1/volume");
    CHECK (osc::addressFor (3120) == "/neptune/bus/2/send/1/level");
    CHECK (osc::addressFor (5100) == "/neptune/parallel/2/volume");
    CHECK (osc::addressFor (9233) == "/neptune/track/3/organ/reverb");
    CHECK (osc::addressFor (9200) == "/neptune/track/3/organ/upper/drawbar/1");
    CHECK (osc::addressFor (9220) == "/neptune/track/3/organ/percussion/on");
    CHECK (osc::addressFor (9227) == "/neptune/track/3/organ/leslie/speed");
    CHECK (osc::addressFor (12000 + 16 * 2 + 3) == "/neptune/track/3/slot/4");
    CHECK (osc::addressFor (13000) == "/neptune/macro/1");
    CHECK (osc::addressFor (13031) == "/neptune/macro/32");
    CHECK (osc::addressFor (424242).empty());
    ParamId id = 0;
    CHECK_FALSE (osc::paramForAddress ("/neptune/track/99/volume", id));
}

TEST_CASE ("osc: a message moves its parameter, normalised or real, and flags it for the host", "[osc]")
{
    ParameterSet set;
    buildFullPool (set);
    control::ControlEngine engine;
    osc::Dispatcher d (engine, set);
    std::vector<std::vector<std::uint8_t>> replies;

    CHECK (d.handleMessage (msg ("/neptune/track/1/pan", 1.0), replies) == 1);
    CHECK (set.get (probe::trackParam (0, probe::kPan)) == Approx (100.0));
    CHECK (d.handleMessage (msg ("/neptune/track/1/pan/real", -50.0), replies) == 1);
    CHECK (set.get (probe::trackParam (0, probe::kPan)) == Approx (-50.0));
    CHECK (d.handleMessage (msg ("/neptune/id/1001", 0.5, 'i'), replies) == 1);
    CHECK (set.get (probe::trackParam (0, probe::kPan)) == Approx (100.0 * 0.0 + 0.0 + (0.5 * 200.0 - 100.0)));
    CHECK (d.handleMessage (msg ("/neptune/track/1/mute", 1.0, 'T'), replies) == 1);
    CHECK (set.get (probe::trackParam (0, probe::kMute)) == Approx (1.0));
    CHECK (d.handleMessage (msg ("/neptune/track/1/pan", 5.0), replies) == 1);   // hors plage : bornée
    CHECK (set.get (probe::trackParam (0, probe::kPan)) == Approx (100.0));
    CHECK (d.handleMessage (msg ("/neptune/track/99/pan", 0.5), replies) == 0);
    CHECK (d.handleMessage (msg ("/elsewhere/pan", 0.5), replies) == 0);

    std::vector<std::size_t> changed;
    CHECK (engine.drainChanges (changed) >= 2);
    CHECK (replies.empty());
}

TEST_CASE ("osc: non-finite or textual values change nothing", "[osc]")
{
    ParameterSet set;
    buildFullPool (set);
    control::ControlEngine engine;
    osc::Dispatcher d (engine, set);
    std::vector<std::vector<std::uint8_t>> replies;
    const auto before = set.get (probe::masterVolume);
    CHECK (d.handleMessage (msg ("/neptune/master/volume", std::nan("")), replies) == 0);
    osc::Message text;
    text.address = "/neptune/master/volume";
    text.args.push_back ({ 's', 0.0, "0.5" });
    CHECK (d.handleMessage (text, replies) == 0);
    CHECK (d.handleMessage (osc::Message { "/neptune/master/volume", {} }, replies) == 0);
    CHECK (set.get (probe::masterVolume) == Approx (before));
}

TEST_CASE ("osc: an address assigned in the control table is reached too", "[osc]")
{
    ParameterSet set;
    buildFullPool (set);
    control::ControlEngine engine;
    control::Config cfg;
    control::Binding b;
    b.source.kind = control::SourceKind::Osc;
    b.source.address = "/mon/fader";
    b.target.param = probe::masterPan;
    cfg.bindings.push_back (b);
    engine.configure (cfg, set);
    osc::Dispatcher d (engine, set);
    std::vector<std::vector<std::uint8_t>> replies;
    CHECK (d.handleMessage (msg ("/mon/fader", 1.0), replies) == 1);
    CHECK (set.get (probe::masterPan) == Approx (100.0));
}

TEST_CASE ("osc: the catalogue goes back as bundles under the datagram size, filtered by prefix", "[osc]")
{
    ParameterSet set;
    buildFullPool (set);
    control::ControlEngine engine;
    osc::Dispatcher d (engine, set);

    std::vector<std::vector<std::uint8_t>> replies;
    d.handleMessage (osc::Message { "/neptune/list", { { 's', 0.0, "/neptune/master" } } }, replies);
    REQUIRE (replies.size() >= 2);
    std::vector<osc::Message> all;
    for (const auto& r : replies)
    {
        CHECK (r.size() <= 1300);
        REQUIRE (osc::parsePacket (r.data(), r.size(), all));
    }
    REQUIRE (all.size() >= 2);
    CHECK (all.back().address == "/neptune/list/done");
    CHECK (all.front().address == "/neptune/list/item");
    CHECK (all.front().args[0].text.rfind ("/neptune/master", 0) == 0);
    CHECK (static_cast<std::size_t> (all.back().args[0].number) == all.size() - 1);

    replies.clear();
    d.handleMessage (osc::Message { "/neptune/list", {} }, replies);
    all.clear();
    for (const auto& r : replies) osc::parsePacket (r.data(), r.size(), all, 100000);
    CHECK (static_cast<std::size_t> (all.back().args[0].number) == set.size());   // tout le pool est adressable
}

TEST_CASE ("osc: the rate limiter caps messages per window and recovers", "[osc]")
{
    osc::RateLimiter lim (3, 100);
    CHECK (lim.allow (0));
    CHECK (lim.allow (10));
    CHECK (lim.allow (20));
    CHECK_FALSE (lim.allow (30));
    CHECK (lim.dropped() == 1);
    CHECK (lim.allow (120));

    ParameterSet set;
    buildFullPool (set);
    control::ControlEngine engine;
    osc::Dispatcher d (engine, set);
    std::vector<std::vector<std::uint8_t>> replies;
    std::vector<std::vector<std::uint8_t>> many;
    for (int i = 0; i < 10; ++i) many.push_back (osc::encode (msg ("/neptune/master/volume", 0.5)));
    const auto bundle = osc::encodeBundle (many);
    osc::RateLimiter small (4, 1000);
    CHECK (d.handlePacket (bundle.data(), bundle.size(), replies, &small, 0) == 4);
}

TEST_CASE ("osc: server settings are off by default and survive JSON", "[osc]")
{
    control::Config c;
    CHECK_FALSE (c.settings.oscEnabled);
    CHECK_FALSE (c.settings.oscAllInterfaces);
    c.settings.oscEnabled = true;
    c.settings.oscAllInterfaces = true;
    c.settings.oscPort = 9100;
    const auto back = control::configFromJson (control::toJson (c));
    CHECK (back.settings.oscEnabled);
    CHECK (back.settings.oscAllInterfaces);
    CHECK (back.settings.oscPort == 9100);
    CHECK_FALSE (control::configFromJson (control::toJson (control::Config {})).settings.oscAllInterfaces);
}
