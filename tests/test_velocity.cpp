// Réponse de vélocité (courbe) et fondu enchaîné entre couches (palier 1, jalon 5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/SamplerVoices.h"
#include "neptune/core/VelocityResponse.h"

using namespace neptune::core;
using Catch::Matchers::WithinAbs;

namespace
{
std::shared_ptr<const SampleData> constant (float v, std::int64_t frames = 24000)
{
    auto s = std::make_shared<SampleData>();
    s->channels = 1;
    s->frames = frames;
    s->sampleRate = 48000.0;
    s->pcm32.assign (static_cast<std::size_t> (frames), v);
    return s;
}

Zone layer (float level, int velLow, int velHigh, int keyLow = 0, int keyHigh = 127, int rr = 0, int group = 0)
{
    Zone z;
    z.sample = constant (level);
    z.keyLow = keyLow; z.keyHigh = keyHigh;
    z.velLow = velLow; z.velHigh = velHigh;
    z.mode = PlayMode::OneShot;
    z.loopStart = 0;
    z.loopEnd = z.sample->frames;
    z.roundRobin = rr;
    z.groupId = group;
    return z;
}

// Rendu d'un Note On : somme du premier bloc (les échantillons sont constants).
float renderOnce (Instrument& inst, int key, float velocity, int* voices = nullptr)
{
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (&inst);
    v.noteOn (0, key, velocity);
    if (voices != nullptr) *voices = v.activeVoices();
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    v.render (l.data(), r.data(), 64, 1.0f, 1.0f, 0.0);
    return l[40];
}
} // namespace

// ---------------------------------------------------------------- courbe

TEST_CASE ("velocity curve: linear by default, endpoints fixed, always monotone")
{
    VelocityCurve lin;
    for (double x : { 0.0, 0.25, 0.5, 1.0 }) CHECK_THAT (evaluate (lin, x), WithinAbs (x, 1e-12));

    VelocityCurve hard;
    hard.shape = 0.7;
    VelocityCurve soft;
    soft.shape = -0.7;
    CHECK (evaluate (hard, 0.5) < 0.5);
    CHECK (evaluate (soft, 0.5) > 0.5);
    for (const auto& c : { hard, soft })
    {
        CHECK_THAT (evaluate (c, 0.0), WithinAbs (0.0, 1e-12));
        CHECK_THAT (evaluate (c, 1.0), WithinAbs (1.0, 1e-12));
        double prev = -1.0;
        for (int k = 0; k <= 200; ++k)
        {
            const double y = evaluate (c, k / 200.0);
            CHECK (y >= prev - 1e-12);
            prev = y;
        }
    }
}

TEST_CASE ("velocity curve with points passes through them and never overshoots")
{
    VelocityCurve c;
    c.points = { { 0.5, 0.8 }, { 0.3, 0.1 }, { 0.9, 0.85 } }; // désordonnés
    c = sanitised (c);
    REQUIRE (c.points.size() == 3);
    CHECK (c.points[0].first < c.points[1].first);
    CHECK_THAT (evaluate (c, 0.3), WithinAbs (0.1, 1e-9));
    CHECK_THAT (evaluate (c, 0.5), WithinAbs (0.8, 1e-9));
    CHECK_THAT (evaluate (c, 0.9), WithinAbs (0.85, 1e-9));
    double prev = -1.0;
    for (int k = 0; k <= 500; ++k)
    {
        const double y = evaluate (c, k / 500.0);
        CHECK (y >= prev - 1e-12);
        CHECK (y >= 0.0);
        CHECK (y <= 1.0);
        prev = y;
    }
}

TEST_CASE ("velocity curve sanitising rejects garbage")
{
    VelocityCurve c;
    c.shape = std::nan ("");
    c.points = { { std::nan (""), 0.5 }, { 0.5, std::nan ("") }, { 0.5, 0.9 }, { 0.505, 0.3 } /* trop près */, { 0.7, 0.2 } /* recule */,
                 { 1.0, 1.0 } /* sur le bord */, { -3.0, 5.0 } /* hors plage */ };
    c = sanitised (c);
    CHECK (c.shape == 0.0);
    double lastX = 0.0, lastY = 0.0;
    for (const auto& p : c.points)
    {
        CHECK (p.first - lastX >= VelocityCurve::minPointGap - 1e-12);
        CHECK (p.first < 1.0);
        CHECK (p.second >= lastY);
        CHECK (p.second <= 1.0);
        lastX = p.first; lastY = p.second;
    }
    CHECK (c.points.size() <= VelocityCurve::maxPoints);

    VelocityCurve many;
    for (int i = 1; i < 100; ++i) many.points.push_back ({ i / 100.0, i / 100.0 });
    CHECK (sanitised (many).points.size() <= VelocityCurve::maxPoints);
}

TEST_CASE ("velocity table matches the curve and interpolates")
{
    VelocityCurve c;
    c.shape = 0.5;
    const auto lut = buildLut (c);
    CHECK (lut[0] == 0.0f);
    CHECK (lut[127] == 1.0f);
    CHECK_THAT (lookup (lut, 64.0f / 127.0f), WithinAbs (evaluate (c, 64.0 / 127.0), 1e-6));
    CHECK_THAT (lookup (lut, 64.5f / 127.0f), WithinAbs (evaluate (c, 64.5 / 127.0), 1e-3));
    CHECK (lookup (lut, -1.0f) == 0.0f);
    CHECK (lookup (lut, 7.0f) == 1.0f);
}

// ---------------------------------------------------------------- fondus

TEST_CASE ("layer fades: touching layers get an automatic fade, nested layers none, width is clamped")
{
    // trois couches jointives : 1-40, 41-80, 81-127
    const std::vector<LayerKey> keys = { { 0, 127, 1, 40, 0, false }, { 0, 127, 41, 80, 0, false }, { 0, 127, 81, 127, 0, false } };
    auto f = computeLayerFades (keys, 0);
    CHECK_FALSE (f[0].hasOut);
    CHECK (f[1].activeLo == 40.5f);
    CHECK (f[1].activeHi == 80.5f);

    f = computeLayerFades (keys, 10);
    CHECK (f[0].hasOut);
    CHECK (f[1].hasIn);
    CHECK (f[1].hasOut);
    CHECK_THAT (f[0].outStart, WithinAbs (40.5 - 5, 1e-4));
    CHECK_THAT (f[0].outEnd, WithinAbs (40.5 + 5, 1e-4));
    CHECK_THAT (f[1].inStart, WithinAbs (f[0].outStart, 1e-4)); // les deux côtés du fondu coïncident
    CHECK_THAT (f[1].inEnd, WithinAbs (f[0].outEnd, 1e-4));
    CHECK (f[1].activeLo < 40.5f);
    CHECK (f[0].activeHi > 40.5f);

    // largeur énorme : bornée par les couches (la couche du milieu fait 40 de large -> demi-largeur max 20)
    f = computeLayerFades (keys, 64);
    CHECK_THAT (f[1].inEnd - f[1].inStart, WithinAbs (40.0, 1e-4));
    CHECK (f[1].inEnd <= f[1].outStart + 1e-4f); // les deux fondus d'une couche ne se recouvrent jamais

    // imbriquées : aucune
    const std::vector<LayerKey> nested = { { 0, 127, 1, 127, 0, false }, { 0, 127, 30, 90, 0, false } };
    f = computeLayerFades (nested, 20);
    for (const auto& x : f) { CHECK_FALSE (x.hasIn); CHECK_FALSE (x.hasOut); }

    // Groupes ou notes différents : jamais de fondu entre eux
    const std::vector<LayerKey> other = { { 0, 127, 1, 40, 0, false }, { 0, 127, 41, 127, 5, false }, { 10, 20, 41, 127, 0, false } };
    f = computeLayerFades (other, 20);
    for (const auto& x : f) { CHECK_FALSE (x.hasIn); CHECK_FALSE (x.hasOut); }
}

TEST_CASE ("layer fades: explicit overlap crossfades over the whole shared range")
{
    const std::vector<LayerKey> keys = { { 0, 127, 1, 70, 0, false }, { 0, 127, 50, 127, 0, false } };
    const auto f = computeLayerFades (keys, 0);
    CHECK_THAT (f[0].outStart, WithinAbs (49.5, 1e-4));
    CHECK_THAT (f[0].outEnd, WithinAbs (70.5, 1e-4));
    CHECK_THAT (f[1].inStart, WithinAbs (49.5, 1e-4));
    CHECK_THAT (f[1].inEnd, WithinAbs (70.5, 1e-4));
    for (const bool linear : { false, true })
        for (float v = 49.5f; v <= 70.5f; v += 0.5f)
        {
            const float a = layerGain (f[0], v, linear), b = layerGain (f[1], v, linear);
            if (linear) CHECK_THAT (a + b, WithinAbs (1.0, 1e-4));       // amplitude constante
            else CHECK_THAT (a * a + b * b, WithinAbs (1.0, 1e-4));      // puissance constante
        }
    CHECK (layerGain (f[0], 30.0f, false) == 1.0f);
    CHECK (layerGain (f[1], 30.0f, false) == 0.0f);
    CHECK (layerGain (f[1], 100.0f, false) == 1.0f);
    CHECK (layerGain (f[0], 100.0f, false) == 0.0f);
}

// ---------------------------------------------------------------- moteur

TEST_CASE ("without fade, disjoint layers behave as before: one voice per note")
{
    Instrument inst;
    inst.zones = { layer (0.2f, 1, 64), layer (0.6f, 65, 127) };
    inst.finalize();
    int voices = 0;
    CHECK (renderOnce (inst, 60, 30.0f / 127.0f, &voices) == 0.2f * (30.0f / 127.0f));
    CHECK (voices == 1);
    CHECK (renderOnce (inst, 60, 100.0f / 127.0f, &voices) == 0.6f * (100.0f / 127.0f));
    CHECK (voices == 1);
}

TEST_CASE ("automatic fade plays both layers at the boundary with complementary gains")
{
    Instrument inst;
    inst.zones = { layer (1.0f, 1, 64), layer (1.0f, 65, 127) };
    inst.fadeWidth = 10; // fondu de 59.5 à 69.5 autour de 64.5
    inst.finalize();

    int voices = 0;
    // au centre du fondu (64.5 n'est pas une vélocité entière : 64 et 65 sont à ±0,5 du centre)
    const float lo = renderOnce (inst, 60, 64.0f / 127.0f, &voices); // 64 : encore couche basse, début de descente
    CHECK (voices == 2);
    const float hi = renderOnce (inst, 60, 65.0f / 127.0f, &voices);
    CHECK (voices == 2);
    CHECK (lo > 0.0f);
    CHECK (hi > 0.0f);
    // valeurs exactes : fondu de 59.5 à 69.5, puissance constante (cos pour la couche basse, sin pour la haute)
    const double t64 = (64.0 - 59.5) / 10.0, t65 = (65.0 - 59.5) / 10.0, hp = 1.57079632679489661923;
    CHECK_THAT (lo, WithinAbs ((64.0 / 127.0) * (std::cos (t64 * hp) + std::sin (t64 * hp)), 1e-5));
    CHECK_THAT (hi, WithinAbs ((65.0 / 127.0) * (std::cos (t65 * hp) + std::sin (t65 * hp)), 1e-5));
    // loin de la frontière : une seule couche, pleine puissance (à la vélocité près)
    CHECK (renderOnce (inst, 60, 20.0f / 127.0f, &voices) == 20.0f / 127.0f);
    CHECK (voices == 1);
    CHECK (renderOnce (inst, 60, 120.0f / 127.0f, &voices) == 120.0f / 127.0f);
    CHECK (voices == 1);

    // puissance constante : à une vélocité donnée, somme des carrés des gains = 1 (mesurée sur les deux échantillons
    // identiques, au volume de vélocité près)
    for (int vel = 60; vel <= 69; ++vel)
    {
        const float x = static_cast<float> (vel) / 127.0f;
        const float out = renderOnce (inst, 60, x, &voices);
        const float g = out / x; // gain de couche cumulé (les deux samples valent 1)
        // un seul des deux sons au bord extérieur, deux au milieu : g est entre ~0.7 et ~1.41
        CHECK (g >= 0.0f);
        CHECK (g <= 1.42f);
    }
}

TEST_CASE ("round-robin counters stay independent per velocity layer")
{
    Instrument inst;
    inst.zones = { layer (0.1f, 1, 64, 0, 127, 1), layer (0.2f, 1, 64, 0, 127, 2), layer (0.4f, 65, 127, 0, 127, 1), layer (0.8f, 65, 127, 0, 127, 2) };
    inst.finalize();
    Instrument::Pick picks[Instrument::maxPicks];
    // trois notes graves puis une forte : la couche forte commence à son premier alternatif
    CHECK (inst.selectLayers (60, 10.0f / 127.0f, false, picks, 16) == 1);
    CHECK (picks[0].zone->roundRobin == 1);
    CHECK (inst.selectLayers (60, 10.0f / 127.0f, false, picks, 16) == 1);
    CHECK (picks[0].zone->roundRobin == 2);
    CHECK (inst.selectLayers (60, 100.0f / 127.0f, false, picks, 16) == 1);
    CHECK (picks[0].zone->roundRobin == 1);
}

TEST_CASE ("stacked layers (nested ranges) both play, release layers are separate")
{
    Instrument inst;
    Zone rel = layer (0.3f, 1, 127);
    rel.onRelease = true;
    inst.zones = { layer (0.5f, 1, 127), layer (0.25f, 40, 90), rel };
    inst.finalize();
    Instrument::Pick picks[Instrument::maxPicks];
    CHECK (inst.selectLayers (60, 60.0f / 127.0f, false, picks, 16) == 2); // empilées
    CHECK (inst.selectLayers (60, 100.0f / 127.0f, false, picks, 16) == 1);
    CHECK (inst.selectLayers (60, 60.0f / 127.0f, true, picks, 16) == 1);  // relâchement : sa propre famille
    CHECK (inst.selectLayers (60, 60.0f / 127.0f, true, nullptr, 16) == 0);
}

TEST_CASE ("velocity curve changes both layer choice and volume")
{
    Instrument inst;
    inst.zones = { layer (1.0f, 1, 64), layer (1.0f, 65, 127) };
    VelocityCurve soft;
    soft.shape = -0.7;
    inst.velocityLut = buildLut (soft);
    inst.velocityLinear = false;
    inst.finalize();
    // 40/127 en entrée donne beaucoup plus : la couche haute est choisie et le volume est plus fort
    const float shaped = inst.shapeVelocity (40.0f / 127.0f);
    CHECK (shaped > 65.0f / 127.0f);
    SamplerVoices v;
    v.prepare (48000.0);
    v.setInstrument (&inst);
    v.noteOn (0, 60, 40.0f / 127.0f);
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    v.render (l.data(), r.data(), 64, 1.0f, 1.0f, 0.0);
    CHECK_THAT (l[40], WithinAbs (shaped, 1e-5));
}

// ---------------------------------------------------------------- description

TEST_CASE ("velocity settings are saved, bounded when read, and undone as part of the settings")
{
    InstrumentDescription d;
    d.velocityFadeWidth = 12;
    d.velocityFadeLinear = true;
    d.velocityCurve.points = { { 0.3, 0.1 }, { 0.7, 0.9 } };
    const auto back = instrumentFromJson (toJson (d));
    CHECK (back.velocityFadeWidth == 12);
    CHECK (back.velocityFadeLinear);
    CHECK (back.velocityCurve == d.velocityCurve);

    CHECK (instrumentFromJson (toJson (InstrumentDescription {})).velocityCurve.isLinear());
    CHECK (instrumentFromJson (R"({"velocity_fade_width": 9999, "zones": []})").velocityFadeWidth == InstrumentDescription::maxVelocityFadeWidth);
    CHECK (instrumentFromJson (R"({"velocity_fade_width": -5, "zones": []})").velocityFadeWidth == 0);
    CHECK (instrumentFromJson (R"({"velocity_curve": {"shape": 99, "points": [[2, 3], "x"]}, "zones": []})").velocityCurve.shape == 1.0);

    InstrumentDescription current;
    const auto before = current;
    auto get = [&] { return current; };
    auto set = [&] (const InstrumentDescription& x) { current = x; };
    UndoManager um;
    um.perform (std::make_unique<InstrumentEditCommand> (current, d, get, set, "Velocity"));
    CHECK (current == d);
    um.undo();
    CHECK (current == before);
    um.redo();
    CHECK (current == d);
}

TEST_CASE ("building an instrument carries the velocity settings")
{
    InstrumentDescription d;
    d.velocityFadeWidth = 8;
    d.velocityFadeLinear = true;
    d.velocityCurve.shape = 0.4;
    const auto inst = buildInstrument (d); // aucune zone : instrument vide mais réglé
    REQUIRE (inst != nullptr);
    CHECK (inst->fadeWidth == 8);
    CHECK (inst->fadeLinear);
    CHECK_FALSE (inst->velocityLinear);
    CHECK (inst->shapeVelocity (0.5f) < 0.5f);
}

TEST_CASE ("velocity curve floor and ceiling remap the whole dynamic range")
{
    VelocityCurve c;
    c.floor = 0.2;
    c.ceiling = 0.8;
    CHECK_THAT (evaluate (c, 0.0), WithinAbs (0.2, 1e-12));
    CHECK_THAT (evaluate (c, 1.0), WithinAbs (0.8, 1e-12));
    CHECK_THAT (evaluate (c, 0.5), WithinAbs (0.5, 1e-12)); // courbe linéaire : milieu à mi-chemin
    CHECK (! c.isLinear());
    const auto lut = buildLut (c);
    CHECK_THAT (lut[127], WithinAbs (0.8, 1e-6));
    CHECK (lut[0] == 0.0f);

    VelocityCurve bad;
    bad.floor = 5.0;
    bad.ceiling = -3.0;
    bad = sanitised (bad);
    CHECK (bad.floor <= 1.0 - VelocityCurve::minSpan + 1e-12);
    CHECK (bad.ceiling >= bad.floor + VelocityCurve::minSpan - 1e-12);
    CHECK (bad.ceiling <= 1.0);
}

TEST_CASE ("velocity curve without smoothing joins its points with straight segments")
{
    VelocityCurve c;
    c.points = { { 0.5, 0.2 } };
    c.smooth = false;
    CHECK_THAT (evaluate (c, 0.25), WithinAbs (0.1, 1e-12));   // milieu du premier segment (0,0)-(0.5,0.2)
    CHECK_THAT (evaluate (c, 0.75), WithinAbs (0.6, 1e-12));   // milieu du second segment (0.5,0.2)-(1,1)
    c.smooth = true;
    CHECK (std::abs (evaluate (c, 0.75) - 0.6) > 1e-6);          // lissée : autre forme, mais toujours monotone
    CHECK (evaluate (c, 0.75) >= evaluate (c, 0.5));
}
