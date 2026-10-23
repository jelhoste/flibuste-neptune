#include <catch2/catch_test_macros.hpp>

#include "neptune/core/Facade.h"
#include "neptune/core/MixerState.h"

using namespace neptune::core;

TEST_CASE ("Facade: a default facade is empty and round-trips")
{
    const facade::Facade f;
    CHECK (f.widgets.empty());
    CHECK (facade::fromJson (facade::toJson (f)) == f);
    CHECK (facade::fromJson ("") == f);
    CHECK (facade::fromJson ("[1,2]") == f);
}

TEST_CASE ("Facade: widgets survive a JSON round trip")
{
    facade::Facade f;
    f.image = "/home/x/fond.png";
    f.fit = facade::ImageFit::Fill;
    auto k = facade::defaultWidget (facade::WidgetType::Knob, f.freeId(), 40, 50);
    k.param = 1234;
    k.text = "Cutoff";
    f.widgets.push_back (k);
    auto b = facade::defaultWidget (facade::WidgetType::Button, f.freeId(), 300, 200);
    b.param = 77;
    f.widgets.push_back (b);
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Label, f.freeId(), 10, 10));
    CHECK (facade::fromJson (facade::toJson (f)) == f);
    CHECK (f.freeId() == 4);
}

TEST_CASE ("Facade: bounds are applied")
{
    facade::Facade f;
    facade::Widget w;
    w.id = 5;
    w.x = 5000; w.y = -30; w.w = 2; w.h = 99999;
    w.text = std::string (500, 'a');
    f.widgets.push_back (w);
    f.widgets.push_back (w); // même identifiant : renuméroté
    f.image = std::string (5000, 'b');
    const auto s = facade::sanitised (f);
    REQUIRE (s.widgets.size() == 2);
    CHECK (s.widgets[0].w == facade::minWidgetSize);
    CHECK (s.widgets[0].h == facade::canvasHeight);
    CHECK (s.widgets[0].x == facade::canvasWidth - facade::minWidgetSize);
    CHECK (s.widgets[0].y == 0);
    CHECK (s.widgets[0].text.size() == facade::maxTextLength);
    CHECK (s.widgets[0].id != s.widgets[1].id);
    CHECK (s.image.size() == facade::maxImagePath);
}

TEST_CASE ("Facade: too many widgets are cut")
{
    facade::Facade f;
    for (int i = 0; i < facade::maxWidgets + 20; ++i) f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Label, i + 1, 0, 0));
    CHECK (facade::sanitised (f).widgets.size() == static_cast<std::size_t> (facade::maxWidgets));
}

TEST_CASE ("Facade: stored with the track in the mixer state")
{
    auto m = std::make_unique<MixerState>();
    auto& strip = m->tracks[2].strip;
    strip.facade.image = "fond.jpg";
    auto w = facade::defaultWidget (facade::WidgetType::Knob, 1, 100, 100);
    w.param = 4321;
    strip.facade.widgets.push_back (w);
    const auto back = std::make_unique<MixerState> (mixerFromJson (toJson (*m)));
    CHECK (back->tracks[2].strip.facade == strip.facade);
    CHECK (back->tracks[3].strip.facade == facade::Facade());
    // un état sans Façade n'écrit rien de plus
    CHECK (toJson (MixerState {}).find ("facade") == std::string::npos);
}
