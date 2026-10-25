#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include "neptune/core/GlobalPreset.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/State.h"
#include "neptune/core/TrackExport.h"

using namespace neptune::core;

namespace
{
std::string sampleState()
{
    ParameterSet set;
    std::string error;
    probe::buildPool (set, 4, &error);
    MixerState m = MixerState::freshProject();
    m.tracks[0].strip.name = "Lead";
    facade::Widget w;
    w.param = probe::trackParam (0, probe::kPan);
    m.tracks[0].strip.facade.widgets.push_back (w);
    m.tracks[0].strip.facade.image = "/img/fond.png";
    PerformanceProfile p;
    return state::save (set, "instance-1", std::nullopt, &m, R"({"enabled":true,"bindings":[]})");
}
} // namespace

TEST_CASE ("global preset: instance data is removed, the rest of the state is kept")
{
    const auto text = sampleState();
    REQUIRE (text.find ("instance_id") != std::string::npos);
    const auto clean = globalpreset::withoutInstanceData (text);
    const auto doc = nlohmann::json::parse (clean);
    CHECK_FALSE (doc.contains ("instance_id"));
    CHECK_FALSE (doc.contains ("profile_lock"));
    CHECK_FALSE (doc.contains ("control"));
    CHECK (doc.contains ("mixer"));
    CHECK (doc.contains ("parameters"));
    CHECK (globalpreset::withoutInstanceData ("not json") == "not json");
}

TEST_CASE ("global preset: the mixer can be edited inside a state document and the result still loads")
{
    const auto text = sampleState();
    std::string seen;
    const auto edited = globalpreset::editMixer (text, [&seen] (MixerState& m)
    {
        globalpreset::forEachFacade (m, [&seen] (facade::Facade& f) { if (! f.image.empty()) { seen = f.image; f.image = "asset:assets/1_fond.png"; } });
    });
    CHECK (seen == "/img/fond.png");
    ParameterSet set;
    std::string error;
    REQUIRE (probe::buildPool (set, 4, &error));
    const auto loaded = state::load (set, edited);
    REQUIRE (loaded.ok());
    REQUIRE (loaded.mixer.has_value());
    CHECK (loaded.mixer->tracks[0].strip.name == "Lead");
    CHECK (loaded.mixer->tracks[0].strip.facade.image == "asset:assets/1_fond.png");
    CHECK (loaded.mixer->tracks[0].strip.facade.widgets.size() == 1);
    CHECK (globalpreset::editMixer ("{}", [] (MixerState&) { FAIL ("no mixer: nothing to edit"); }) == "{}");
}
