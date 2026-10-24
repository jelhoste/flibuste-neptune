#include <catch2/catch_test_macros.hpp>

#include "neptune/core/Engine.h"
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

TEST_CASE ("Facade: menu, action and curve widgets round-trip, snapshot is bounded")
{
    facade::Facade f;
    auto a = facade::defaultWidget (facade::WidgetType::Action, 1, 10, 10);
    a.snapshot = { { 9000, 8.0 }, { 9001, 0.0 }, { 9002, 3.0 } };
    f.widgets.push_back (a);
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Menu, 2, 200, 10));
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Curve, 3, 300, 100));
    CHECK (facade::fromJson (facade::toJson (f)) == f);

    f.widgets[0].snapshot.assign (200, { 5, 1.0 });
    f.widgets[0].snapshot.push_back ({ 0, 1.0 });
    CHECK (facade::sanitised (f).widgets[0].snapshot.size() == facade::maxSnapshot);
}

TEST_CASE ("Facade: align, distribute and grid tools")
{
    facade::Facade f;
    for (int i = 0; i < 4; ++i)
    {
        auto w = facade::defaultWidget (facade::WidgetType::Button, i + 1, 40 + i * 150, 30 + i * 70);
        f.widgets.push_back (w);
    }
    const std::set<int> all { 1, 2, 3, 4 };

    auto g = f;
    facade::alignWidgets (g, all, facade::Align::Left);
    for (const auto& w : g.widgets) CHECK (w.x == 40);
    g = f;
    facade::alignWidgets (g, all, facade::Align::Bottom);
    for (const auto& w : g.widgets) CHECK (w.y + w.h == 30 + 3 * 70 + 44);

    g = f;
    const int xs[] = { 0, 100, 300, 880 };
    for (std::size_t i = 0; i < 4; ++i) g.widgets[i].x = xs[i];
    facade::distributeWidgets (g, all, true);
    CHECK (g.widgets[0].x == 0);
    CHECK (g.widgets[3].x + g.widgets[3].w >= 998);
    CHECK (g.widgets[1].x - (g.widgets[0].x + g.widgets[0].w) == g.widgets[2].x - (g.widgets[1].x + g.widgets[1].w));

    g = f;
    facade::arrangeGrid (g, all, 2);
    CHECK (g.widgets[0].x == g.widgets[2].x);
    CHECK (g.widgets[1].x > g.widgets[0].x);
    CHECK (g.widgets[2].y > g.widgets[0].y);
    CHECK (g.widgets[1].y == g.widgets[0].y);

    g = f;
    facade::alignWidgets (g, { 1 }, facade::Align::Left); // un seul widget : sans effet
    CHECK (g == f);
}

namespace
{
facade::Widget zoneAt (int id, int x, int y, int w, int h, facade::ZoneLayout layout)
{
    auto z = facade::defaultWidget (facade::WidgetType::Zone, id, x, y);
    z.w = w; z.h = h;
    z.layout = static_cast<int> (layout);
    return z;
}
}

TEST_CASE ("Facade zones: widgets dropped inside are adopted, widgets outside are not")
{
    facade::Facade f;
    f.widgets.push_back (zoneAt (1, 100, 100, 400, 200, facade::ZoneLayout::Free));
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, 2, 150, 150));   // centre dans la zone
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, 3, 700, 400));   // dehors
    facade::arrange (f, { 1, 2, 3 });
    CHECK (f.find (2)->parent == 1);
    CHECK (f.find (3)->parent == 0);

    // sortir de la zone la détache
    f.find (2)->x = 800;
    facade::arrange (f, { 2 });
    CHECK (f.find (2)->parent == 0);

    // la plus petite zone gagne
    f.widgets.push_back (zoneAt (4, 120, 120, 150, 120, facade::ZoneLayout::Free));
    f.find (2)->x = 150; f.find (2)->y = 150;
    facade::arrange (f, { 4, 2 });
    CHECK (f.find (2)->parent == 4);
    CHECK (f.find (4)->parent == 1);
    // une zone ne peut jamais devenir son propre ancêtre
    CHECK (facade::descendants (f, 1).count (4) == 1);
    CHECK (facade::descendants (f, 4).count (1) == 0);
}

TEST_CASE ("Facade zones: moving a zone carries its content")
{
    facade::Facade f;
    f.widgets.push_back (zoneAt (1, 100, 100, 400, 200, facade::ZoneLayout::Free));
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Button, 2, 150, 150));
    facade::arrange (f, { 1, 2 });
    facade::moveWidgets (f, { 1 }, 30, 20);
    CHECK (f.find (1)->x == 130);
    CHECK (f.find (2)->x == 180);
    CHECK (f.find (2)->y == 170);
}

TEST_CASE ("Facade zones: row, column and grid layouts")
{
    facade::Facade f;
    f.widgets.push_back (zoneAt (1, 100, 100, 600, 300, facade::ZoneLayout::Row));
    auto& z = f.widgets.back();
    z.text = "";
    z.gap = 10;
    for (int i = 0; i < 3; ++i)
    {
        auto k = facade::defaultWidget (facade::WidgetType::Knob, 2 + i, 150 + i * 130, 150 + i * 20);   // 90 x 110
        f.widgets.push_back (k);
    }
    facade::arrange (f, { 1, 2, 3, 4 });
    REQUIRE (f.find (2)->parent == 1);
    // ligne, alignement en haut : même y, espacés de gap
    CHECK (f.find (2)->y == f.find (3)->y);
    CHECK (f.find (3)->x - (f.find (2)->x + f.find (2)->w) == 10);
    CHECK (f.find (2)->x == 110);

    // alignement au centre : centrés verticalement dans la zone
    f.find (1)->align = static_cast<int> (facade::CrossAlign::Centre);
    facade::applyLayouts (f);
    CHECK (f.find (2)->y == 100 + facade::zonePad + (300 - 2 * facade::zonePad - 110) / 2);

    // répartition égale : premier au bord gauche, dernier au bord droit
    f.find (1)->justify = static_cast<int> (facade::Justify::Spread);
    facade::applyLayouts (f);
    CHECK (f.find (2)->x == 110);
    CHECK (f.find (4)->x + f.find (4)->w <= 100 + 600 - facade::zonePad);
    CHECK (f.find (4)->x + f.find (4)->w >= 100 + 600 - facade::zonePad - 3);

    // colonne : même x, ordre vertical conservé
    f.find (1)->layout = static_cast<int> (facade::ZoneLayout::Column);
    f.find (1)->align = static_cast<int> (facade::CrossAlign::Start);
    f.find (1)->justify = 0;
    facade::applyLayouts (f);
    CHECK (f.find (2)->x == f.find (3)->x);
    CHECK (f.find (3)->y > f.find (2)->y);

    // matrice à 2 colonnes : 3 widgets -> 2 + 1
    f.find (1)->layout = static_cast<int> (facade::ZoneLayout::Grid);
    f.find (1)->columns = 2;
    facade::applyLayouts (f);
    CHECK (f.find (2)->y == f.find (3)->y);
    CHECK (f.find (4)->x == f.find (2)->x);
    CHECK (f.find (4)->y > f.find (2)->y);
}

TEST_CASE ("Facade zones: duplicate keeps the content together, same size for all")
{
    facade::Facade f;
    f.widgets.push_back (zoneAt (1, 50, 50, 300, 200, facade::ZoneLayout::Free));
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, 2, 80, 90));
    f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, 3, 500, 90));
    facade::arrange (f, { 1, 2, 3 });
    const auto fresh = facade::duplicateWidgets (f, { 1 }, 24);
    REQUIRE (fresh.size() == 1);
    const int copyZone = *fresh.begin();
    CHECK (f.widgets.size() == 5);
    int kids = 0;
    for (const auto& w : f.widgets) if (w.parent == copyZone) ++kids;
    CHECK (kids == 1);

    f.find (2)->w = 40; f.find (2)->h = 50;
    facade::matchSize (f, 2);
    for (const auto& w : f.widgets)
        if (w.type == facade::WidgetType::Knob) { CHECK (w.w == 40); CHECK (w.h == 50); }
    CHECK (f.find (1)->w == 300);   // les zones ne sont pas touchées

    // JSON : les champs de zone voyagent, et un parent invalide est écarté
    CHECK (facade::fromJson (facade::toJson (f)) == f);
    f.find (2)->parent = 999;
    CHECK (facade::sanitised (f).find (2)->parent == 0);
}

TEST_CASE ("track velocity curve reshapes incoming MIDI velocity for a synth track and survives the state round trip")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    mixer::StripSettings strip;
    strip.velocityCurve.ceiling = 0.5;
    engine.setTrackSettings (0, strip);
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    float* out[2] = { l.data(), r.data() };
    engine.process (out, 2, 64);   // le thread audio adopte la courbe

    Engine::NoteTrace t;
    engine.noteOn (0, 60, 1.0f);
    REQUIRE (engine.popNoteTrace (t));
    CHECK (t.velocity >= 63);
    CHECK (t.velocity <= 65);

    strip.velocityCurve = VelocityCurve {};
    engine.setTrackSettings (0, strip);
    engine.process (out, 2, 64);
    engine.noteOn (0, 62, 1.0f);
    REQUIRE (engine.popNoteTrace (t));
    CHECK (t.velocity == 127);

    auto m = std::make_unique<MixerState>();
    m->tracks[1].strip.velocityCurve.floor = 0.2;
    m->tracks[1].strip.velocityCurve.smooth = false;
    m->tracks[1].strip.velocityCurve.points = { { 0.5, 0.3 } };
    const auto back = std::make_unique<MixerState> (mixerFromJson (toJson (*m)));
    CHECK (back->tracks[1].strip.velocityCurve == m->tracks[1].strip.velocityCurve);
    CHECK (back->tracks[2].strip.velocityCurve.isLinear());
}

TEST_CASE ("Facade: circle arrangement puts the reference where asked and keeps the order around the centre")
{
    facade::Facade f;
    // quatre potards en carré, dans le sens horaire à partir du haut gauche : 1 (haut-gauche), 2 (haut-droite), 3 (bas-droite), 4 (bas-gauche)
    const int pos[4][2] = { { 100, 100 }, { 500, 100 }, { 500, 400 }, { 100, 400 } };
    for (int i = 0; i < 4; ++i) f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, i + 1, pos[i][0], pos[i][1]));
    const std::set<int> all { 1, 2, 3, 4 };

    auto centreX = [] (const facade::Widget& w) { return w.x + w.w / 2.0; };
    auto centreY = [] (const facade::Widget& w) { return w.y + w.h / 2.0; };

    auto g = f;
    facade::arrangeCircle (g, all, 1, facade::CircleStart::Top);
    // 4 widgets : 1 en haut, puis 2 à droite, 3 en bas, 4 à gauche (sens horaire)
    CHECK (centreY (*g.find (1)) < centreY (*g.find (2)));
    CHECK (centreX (*g.find (2)) > centreX (*g.find (1)));
    CHECK (centreY (*g.find (3)) > centreY (*g.find (2)));
    CHECK (centreX (*g.find (4)) < centreX (*g.find (1)));
    CHECK (std::abs (centreX (*g.find (1)) - centreX (*g.find (3))) <= 2.0);   // 1 et 3 sont diamétralement opposés

    // le même ensemble, mais 3 en référence et placé à gauche : 3 à gauche, 4 en haut, 1 à droite, 2 en bas
    g = f;
    facade::arrangeCircle (g, all, 3, facade::CircleStart::Left);
    CHECK (centreX (*g.find (3)) < centreX (*g.find (4)));
    CHECK (centreY (*g.find (4)) < centreY (*g.find (3)));
    CHECK (centreX (*g.find (1)) > centreX (*g.find (4)));
    CHECK (centreY (*g.find (2)) > centreY (*g.find (1)));

    // moins de trois widgets : sans effet
    g = f;
    facade::arrangeCircle (g, { 1, 2 }, 1, facade::CircleStart::Top);
    CHECK (g == f);
}

TEST_CASE ("Facade zones: circle layout is stable once applied")
{
    facade::Facade f;
    f.widgets.push_back (zoneAt (1, 100, 100, 500, 400, facade::ZoneLayout::Circle));
    for (int i = 0; i < 5; ++i) f.widgets.push_back (facade::defaultWidget (facade::WidgetType::Knob, 2 + i, 160 + i * 60, 200 + (i % 2) * 40));
    f.find (1)->justify = static_cast<int> (facade::CircleStart::Top);
    facade::arrange (f, { 1, 2, 3, 4, 5, 6 });
    for (int i = 2; i <= 6; ++i) REQUIRE (f.find (i)->parent == 1);
    const auto first = f;
    facade::applyLayouts (f);
    for (int i = 2; i <= 6; ++i)
    {
        CHECK (f.find (i)->x == first.find (i)->x);
        CHECK (f.find (i)->y == first.find (i)->y);
    }
    // le premier (le plus proche de l'angle de départ) est en haut, au centre de la zone
    int topmost = 2;
    for (int i = 3; i <= 6; ++i) if (f.find (i)->y < f.find (topmost)->y) topmost = i;
    CHECK (std::abs ((f.find (topmost)->x + f.find (topmost)->w / 2) - (100 + 500 / 2)) <= 2);
}

TEST_CASE ("track MIDI channels can be a set: normal forms, state round trip and routing")
{
    mixer::StripSettings s;
    CHECK (mixer::midiChannelsOf (s) == 0xFFFFu);                       // tous par défaut
    mixer::setMidiChannels (s, (1u << 0) | (1u << 2));                  // canaux 1 et 3
    CHECK (s.midiChannelMask == 5u);
    CHECK (s.midiChannel == 0);
    mixer::setMidiChannels (s, 1u << 6);                                // un seul : forme classique
    CHECK (s.midiChannel == 7);
    CHECK (s.midiChannelMask == 0u);
    mixer::setMidiChannels (s, 0xFFFFu);
    CHECK (s.midiChannel == 0);
    CHECK (s.midiChannelMask == 0u);

    mixer::StripSettings wild;
    wild.midiChannelMask = 0x1FFFFu | 0x20000u;                         // hors plage : ramené à « tous »
    CHECK (mixer::sanitisedStrip (wild, false, 0).midiChannelMask == 0u);
    wild.midiChannelMask = 0x10u;                                       // un seul bit : canal 5
    wild = mixer::sanitisedStrip (wild, false, 0);
    CHECK (wild.midiChannel == 5);
    CHECK (wild.midiChannelMask == 0u);

    auto m = std::make_unique<MixerState>();
    mixer::setMidiChannels (m->tracks[3].strip, (1u << 1) | (1u << 9) | (1u << 15));
    const auto back = std::make_unique<MixerState> (mixerFromJson (toJson (*m)));
    CHECK (back->tracks[3].strip.midiChannelMask == m->tracks[3].strip.midiChannelMask);
    CHECK (mixer::midiChannelsOf (back->tracks[3].strip) == ((1u << 1) | (1u << 9) | (1u << 15)));

    Engine engine;
    engine.prepare (48000.0, 256);
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    mixer::StripSettings strip;
    mixer::setMidiChannels (strip, (1u << 0) | (1u << 2));              // canaux 1 et 3 (indices 0 et 2)
    engine.setTrackSettings (0, strip);
    auto playedOnTrack0 = [&engine] (int channel, int key)
    {
        engine.noteOn (channel, key, 0.5f);
        bool played = false;
        Engine::NoteTrace t;
        while (engine.popNoteTrace (t))
            if (t.track == 0 && t.key == key) played = true;
        return played;
    };
    CHECK_FALSE (playedOnTrack0 (1, 60));   // canal 2 : ignoré
    CHECK (playedOnTrack0 (2, 61));         // canal 3 : joué
    CHECK (playedOnTrack0 (0, 62));         // canal 1 : joué
}

TEST_CASE ("Facade keyboards: settings round-trip and are bounded")
{
    facade::Facade f;
    auto k = facade::defaultWidget (facade::WidgetType::Keyboard, 1, 20, 400);
    k.channel = 3; k.firstNote = 48; k.hold = 1; k.velocity = 90;
    f.widgets.push_back (k);
    CHECK (facade::fromJson (facade::toJson (f)) == f);

    f.widgets[0].channel = 99; f.widgets[0].firstNote = -5; f.widgets[0].velocity = 0; f.widgets[0].hold = 7;
    const auto s = facade::sanitised (f);
    CHECK (s.widgets[0].channel == 16);
    CHECK (s.widgets[0].firstNote == 0);
    CHECK (s.widgets[0].velocity == 1);
    CHECK (s.widgets[0].hold == 1);
}

TEST_CASE ("held keys are tracked per MIDI channel for the Facade keyboards")
{
    Engine engine;
    engine.prepare (48000.0, 256);
    engine.setTrackActive (0, true);
    engine.setTrackType (0, TrackType::Synth);
    engine.publishSynth (0, std::make_shared<SynthPatch> (SynthDescription {}));
    engine.noteOnTrack (0, 1, 60, 0.5f);   // canal 2 (indice 1)
    engine.noteOnTrack (0, 2, 72, 0.5f);   // canal 3
    CHECK ((engine.heldChannelWord (0, 1, 0) >> 60 & 1u) == 1u);
    CHECK (engine.heldChannelWord (0, 0, 0) == 0u);
    CHECK ((engine.heldChannelWord (0, 2, 1) >> (72 - 64) & 1u) == 1u);
    CHECK (engine.heldChannelWord (0, 1, 1) == 0u);
    engine.noteOffTrack (0, 1, 60);
    CHECK (engine.heldChannelWord (0, 1, 0) == 0u);
    engine.panic();
    CHECK (engine.heldChannelWord (0, 2, 1) == 0u);
}

TEST_CASE ("Facade image styles and sliders: round trip, deleted styles are dropped, bounds")
{
    facade::Facade f;
    facade::ImageStyle st;
    st.id = 1; st.name = "Tirant"; st.off = "tirant_ferme.svg"; st.on = "tirant_ouvert.png";
    st.track = "rail.png"; st.handle = "poignee.svg"; st.strip = "potard.png"; st.frames = 64; st.stripHorizontal = true;
    f.styles.push_back (st);
    auto b = facade::defaultWidget (facade::WidgetType::Button, 1, 10, 10);
    b.style = 1;
    f.widgets.push_back (b);
    auto sl = facade::defaultWidget (facade::WidgetType::Slider, 2, 10, 100);
    sl.style = 1; sl.vertical = 1; sl.param = 9000;
    f.widgets.push_back (sl);
    CHECK (facade::fromJson (facade::toJson (f)) == f);
    CHECK (f.freeStyleId() == 2);
    REQUIRE (f.findStyle (1) != nullptr);

    f.styles.clear();                                  // style supprimé : les widgets le lâchent
    CHECK (facade::sanitised (f).widgets[0].style == 0);

    facade::Facade g;
    for (int i = 0; i < facade::maxStyles + 5; ++i) { facade::ImageStyle s; s.id = 7; s.frames = 99999; s.name = std::string (300, 'n'); g.styles.push_back (s); }
    const auto clean = facade::sanitised (g);
    CHECK (clean.styles.size() == static_cast<std::size_t> (facade::maxStyles));
    CHECK (clean.styles[0].frames == facade::maxStripFrames);
    CHECK (clean.styles[0].name.size() == facade::maxTextLength);
    std::set<int> ids;
    for (const auto& s : clean.styles) ids.insert (s.id);
    CHECK (ids.size() == clean.styles.size());         // identifiants uniques
}

TEST_CASE ("Facade notches: custom values, irregular spacing, invert and bounds")
{
    facade::Facade f;
    auto s = facade::defaultWidget (facade::WidgetType::Slider, 1, 10, 10);
    s.stepMode = 2; s.invert = 1; s.param = 7;
    s.notches = { 20.0, 200.0, 440.0, 5000.0, 200.0 };   // doublon et désordre
    auto k = facade::defaultWidget (facade::WidgetType::Knob, 2, 300, 10);
    k.stepMode = 1;
    f.widgets = { s, k };
    const auto clean = facade::sanitised (f);
    CHECK (clean.widgets[0].notches == std::vector<double> ({ 20.0, 200.0, 440.0, 5000.0 }));
    CHECK (facade::fromJson (facade::toJson (clean)) == clean);

    // trop peu de crans : retour au mode automatique
    f.widgets[0].notches = { 3.0 };
    CHECK (facade::sanitised (f).widgets[0].stepMode == 0);
    // bornes
    f.widgets[0].notches.assign (500, 0.0);
    for (std::size_t i = 0; i < 500; ++i) f.widgets[0].notches[i] = static_cast<double> (i);
    CHECK (facade::sanitised (f).widgets[0].notches.size() == facade::maxNotches);

    // position et valeur : crans à égale distance même quand leurs valeurs ne le sont pas
    const std::vector<double> n { 20.0, 200.0, 440.0, 5000.0 };
    CHECK (facade::notchPosition (n, 20.0) == 0.0);
    CHECK (facade::notchPosition (n, 5000.0) == 1.0);
    CHECK (std::abs (facade::notchPosition (n, 430.0) - 2.0 / 3.0) < 1e-12);
    CHECK (facade::notchValueAt (n, 0.0) == 20.0);
    CHECK (facade::notchValueAt (n, 0.34) == 200.0);
    CHECK (facade::notchValueAt (n, 5.0) == 5000.0);

    // crans réguliers : suivent la courbe du paramètre, sans doublon sur un paramètre à pas
    const auto stepped = ParamSpec::stepped (1, "drawbar", Unit::None, 0, 8, 0);
    CHECK (facade::evenNotches (stepped, 9).size() == 9);
    CHECK (facade::evenNotches (stepped, 20).size() == 9);
    const auto hz = ParamSpec::continuous (2, "f", Unit::Hertz, 20.0, 20000.0, 1000.0, Curve::Exponential);
    const auto pts = facade::evenNotches (hz, 3);
    REQUIRE (pts.size() == 3);
    CHECK (std::abs (pts[1] - 632.4555) < 0.01);
}
