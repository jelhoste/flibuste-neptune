#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include "neptune/core/FacadeTransfer.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/TrackExport.h"

using namespace neptune::core;

namespace
{
trackexport::Bundle sampleBundle()
{
    trackexport::Bundle b;
    b.track = 2;
    b.state.active = true;
    b.state.type = TrackType::Sampler;
    ZoneDescription z;
    z.samplePath = "/samples/piano_c4.wav";
    b.state.instrument.zones.push_back (z);
    b.state.strip.name = "Piano";
    b.state.strip.inserts[0].type = "convolution";
    b.state.strip.inserts[0].text = "/irs/church.wav";
    b.state.strip.inserts[1].type = "utility";
    modulation::Route own;
    own.source.kind = modulation::SourceKind::Lfo;
    own.dest = probe::trackParam (2, probe::kVolume);
    modulation::Route insert = own;
    insert.dest = modulation::insertDest (1, 3);
    b.state.strip.modulation.routes = { own, insert };
    facade::Widget w;
    w.param = probe::trackParam (2, probe::kPan);
    b.state.strip.facade.widgets.push_back (w);
    b.params[probe::trackParam (2, probe::kVolume)] = 0.5;
    return b;
}
} // namespace

TEST_CASE ("track export: json round trip keeps the track, its files, modulation and facade")
{
    const auto b = sampleBundle();
    const auto back = trackexport::fromJson (trackexport::toJson (b));
    REQUIRE (back.has_value());
    CHECK (back->track == 2);
    CHECK (back->state.strip.name == "Piano");
    REQUIRE (back->state.instrument.zones.size() == 1);
    CHECK (back->state.instrument.zones[0].samplePath == "/samples/piano_c4.wav");
    CHECK (back->state.strip.inserts[0].text == "/irs/church.wav");
    CHECK (back->state.strip.modulation.routes.size() == 2);
    CHECK (back->state.strip.facade.widgets.size() == 1);
    CHECK (back->params == b.params);
    CHECK (back->state.active);
}

TEST_CASE ("track export: unreadable input is refused, bad parameters are dropped")
{
    CHECK_FALSE (trackexport::fromJson ("").has_value());
    CHECK_FALSE (trackexport::fromJson ("[1,2]").has_value());
    CHECK_FALSE (trackexport::fromJson (R"({"format":1})").has_value());
    const auto b = trackexport::fromJson (R"({"track":99,"state":{},"params":{"12":1.5,"x":2,"-3":4,"7":"a"}})");
    REQUIRE (b.has_value());
    CHECK (b->track == mixer::numTracks - 1);
    CHECK (b->params.size() == 1);
    CHECK (b->params.at (12) == 1.5);
}

TEST_CASE ("track export: remapping moves settings, modulation and facade to the target track, insert routes stay")
{
    auto b = sampleBundle();
    trackexport::remapTo (b, 9);
    CHECK (b.track == 9);
    CHECK (b.params.count (probe::trackParam (9, probe::kVolume)) == 1);
    CHECK (b.state.strip.modulation.routes[0].dest == probe::trackParam (9, probe::kVolume));
    CHECK (b.state.strip.modulation.routes[1].dest == modulation::insertDest (1, 3));
    CHECK (b.state.strip.facade.widgets[0].param == probe::trackParam (9, probe::kPan));
}

TEST_CASE ("track export: sample and impulse response paths are visited and can be rewritten")
{
    auto b = sampleBundle();
    std::vector<std::string> seen;
    trackexport::forEachFilePath (b.state, [&seen] (std::string& p) { seen.push_back (p); p = "asset:" + p; });
    CHECK (seen == std::vector<std::string> ({ "/samples/piano_c4.wav", "/irs/church.wav" }));
    CHECK (b.state.instrument.zones[0].samplePath == "asset:/samples/piano_c4.wav");
    CHECK (b.state.strip.inserts[0].text == "asset:/irs/church.wav");
}

TEST_CASE ("track export: owned parameters are those of the track only")
{
    ParameterSet set;
    std::string error;
    REQUIRE (sampler::addParams (set, &error));
    REQUIRE (probe::buildPool (set, 16, &error));
    REQUIRE (mixer::addParams (set, 16, mixer::numBuses, &error));
    const auto ids = trackexport::ownedParams (set, 3);
    auto has = [&ids] (ParamId id) { return std::find (ids.begin(), ids.end(), id) != ids.end(); };
    CHECK (has (probe::trackParam (3, probe::kVolume)));
    CHECK (has (mixer::synthParam (3, mixer::kLevelOsc1)));
    CHECK (! has (probe::trackParam (4, probe::kVolume)));
    CHECK (! has (probe::masterVolume));
    CHECK (trackexport::ownedParams (set, 40).empty());
    CHECK (! trackexport::ownedParams (set, 0).empty());
}
