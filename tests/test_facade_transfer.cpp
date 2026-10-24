#include <catch2/catch_test_macros.hpp>

#include "neptune/core/FacadeTransfer.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;

TEST_CASE ("facade transfer: track settings follow the target track, global ones do not move")
{
    using facade::remapParamTrack;
    // volume, synthé, FM, orgue, emplacements et modulateurs : même réglage sur la piste cible
    CHECK (remapParamTrack (probe::trackParam (2, probe::kVolume), 2, 6) == probe::trackParam (6, probe::kVolume));
    CHECK (remapParamTrack (mixer::synthParam (3, mixer::kLevelOsc2), 3, 9) == mixer::synthParam (9, mixer::kLevelOsc2));
    CHECK (remapParamTrack (mixer::fmParam (1, mixer::kFmBrightness), 1, 15) == mixer::fmParam (15, mixer::kFmBrightness));
    CHECK (remapParamTrack (mixer::organParam (4, mixer::kOrgLeslieSpeed), 4, 0) == mixer::organParam (0, mixer::kOrgLeslieSpeed));
    CHECK (remapParamTrack (mixer::zoneSlotParam (5, 7), 5, 8) == mixer::zoneSlotParam (8, 7));
    CHECK (remapParamTrack (mixer::modulatorParam (2, 3), 2, 1) == mixer::modulatorParam (1, 3));

    // réglages d'instrument : identifiants historiques de la piste 0 <-> bloc des autres pistes
    CHECK (remapParamTrack (sampler::gain, 0, 4) == mixer::samplerParam (4, mixer::kGain));
    CHECK (remapParamTrack (sampler::filter2Resonance, 0, 4) == mixer::samplerParam (4, mixer::kFilter2Resonance));
    CHECK (remapParamTrack (mixer::samplerParam (4, mixer::kTuneCents), 4, 0) == sampler::tuneCents);
    CHECK (remapParamTrack (mixer::samplerParam (4, mixer::kTuneCents), 4, 7) == mixer::samplerParam (7, mixer::kTuneCents));

    // globaux et inconnus : inchangés
    CHECK (remapParamTrack (probe::masterVolume, 2, 6) == probe::masterVolume);
    CHECK (remapParamTrack (mixer::busParam (3, mixer::kBusVolume), 2, 6) == mixer::busParam (3, mixer::kBusVolume));
    CHECK (remapParamTrack (mixer::parallelParam (1, mixer::kParVolume), 2, 6) == mixer::parallelParam (1, mixer::kParVolume));
    CHECK (remapParamTrack (13005, 2, 6) == 13005);   // macro 6
    CHECK (remapParamTrack (987654, 2, 6) == 987654);
    // pistes hors plage ou identiques : rien ne change
    CHECK (remapParamTrack (probe::trackParam (2, probe::kPan), 2, 2) == probe::trackParam (2, probe::kPan));
    CHECK (remapParamTrack (probe::trackParam (2, probe::kPan), 2, 40) == probe::trackParam (2, probe::kPan));
    CHECK (remapParamTrack (probe::trackParam (2, probe::kPan), -1, 3) == probe::trackParam (2, probe::kPan));
}

TEST_CASE ("facade transfer: widgets and snapshots are rebranched, image paths are visited")
{
    facade::Facade f;
    auto k = facade::defaultWidget (facade::WidgetType::Knob, 1, 10, 10);
    k.param = probe::trackParam (3, probe::kVolume);
    auto a = facade::defaultWidget (facade::WidgetType::Action, 2, 200, 10);
    a.snapshot = { { mixer::organParam (3, 0), 8.0 }, { probe::masterVolume, -6.0 } };
    f.widgets = { k, a };
    facade::remapTrack (f, 3, 11);
    CHECK (f.widgets[0].param == probe::trackParam (11, probe::kVolume));
    CHECK (f.widgets[1].snapshot[0].first == mixer::organParam (11, 0));
    CHECK (f.widgets[1].snapshot[1].first == probe::masterVolume);

    f.image = "/a/fond.png";
    facade::ImageStyle s;
    s.id = 1; s.off = "pack:p!off.svg"; s.handle = "/b/poignee.svg";
    f.styles.push_back (s);
    std::vector<std::string> seen;
    facade::forEachImagePath (f, [&seen] (std::string& p) { seen.push_back (p); p += "!"; });
    CHECK (seen == std::vector<std::string> ({ "/a/fond.png", "pack:p!off.svg", "/b/poignee.svg" }));
    CHECK (f.image == "/a/fond.png!");
    CHECK (f.styles[0].on.empty());
}
