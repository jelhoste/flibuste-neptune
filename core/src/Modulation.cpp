#include "neptune/core/Modulation.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "neptune/core/Control.h"
#include "neptune/core/DescriptionJson.h"

namespace neptune::core::modulation
{
using nlohmann::json;

bool isBipolar (SourceKind k) noexcept { return k == SourceKind::PitchBend || k == SourceKind::Lfo; }

namespace
{
double clampD (double v, double lo, double hi) { return std::isfinite (v) ? std::clamp (v, lo, hi) : lo; }
} // namespace

Matrix sanitised (Matrix m)
{
    for (auto& l : m.lfos)
    {
        if (static_cast<int> (l.shape) < 0 || static_cast<int> (l.shape) >= numLfoShapes) l.shape = LfoShape::Sine;
        l.rateHz = clampD (l.rateHz, 0.01, 50.0);
        l.phase = clampD (l.phase, 0.0, 1.0);
    }
    for (auto& e : m.envelopes) e.env = neptune::core::sanitised (e.env);
    std::vector<Route> kept;
    for (auto r : m.routes)
    {
        const int kind = static_cast<int> (r.source.kind);
        if (kind <= 0 || kind >= numSourceKinds || r.dest == 0) continue;
        switch (r.source.kind)
        {
            case SourceKind::ControlChange: r.source.index = std::clamp (r.source.index, 0, 127); break;
            case SourceKind::Macro: r.source.index = std::clamp (r.source.index, 0, control::kMacros - 1); break;
            case SourceKind::Lfo: r.source.index = std::clamp (r.source.index, 0, kLfos - 1); break;
            case SourceKind::Envelope: r.source.index = std::clamp (r.source.index, 0, kEnvelopes - 1); break;
            default: r.source.index = 0; break;
        }
        const bool usesChannel = r.source.kind == SourceKind::ControlChange || r.source.kind == SourceKind::PitchBend || r.source.kind == SourceKind::Aftertouch;
        r.source.channel = usesChannel ? std::clamp (r.source.channel, 0, 16) : 0;
        r.amount = clampD (r.amount, -1.0, 1.0);
        if (kept.size() < kMaxRoutes) kept.push_back (r);
    }
    m.routes = std::move (kept);
    return m;
}

std::string toJson (const Matrix& m)
{
    json o;
    json lfos = json::array();
    for (const auto& l : m.lfos) lfos.push_back ({ { "shape", static_cast<int> (l.shape) }, { "rate", l.rateHz }, { "phase", l.phase } });
    o["lfos"] = std::move (lfos);
    json envs = json::array();
    for (const auto& e : m.envelopes) envs.push_back ({ { "envelope", detail::envelopeJson (e.env) } });
    o["envelopes"] = std::move (envs);
    json routes = json::array();
    for (const auto& r : m.routes)
    {
        json e;
        e["kind"] = static_cast<int> (r.source.kind);
        e["index"] = r.source.index;
        e["channel"] = r.source.channel;
        e["dest"] = r.dest;
        e["amount"] = r.amount;
        if (r.centred) e["centred"] = true;
        routes.push_back (std::move (e));
    }
    o["routes"] = std::move (routes);
    return o.dump();
}

Matrix fromJson (std::string_view text)
{
    Matrix m;
    const auto o = json::parse (text.begin(), text.end(), nullptr, false);
    if (! o.is_object()) return m;
    auto num = [] (const json& j, const char* key, double def) { const auto it = j.find (key); return it != j.end() && it->is_number() ? it->get<double>() : def; };
    if (const auto it = o.find ("lfos"); it != o.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < m.lfos.size(); ++i)
        {
            const auto& j = (*it)[i];
            if (! j.is_object()) continue;
            m.lfos[i].shape = static_cast<LfoShape> (static_cast<int> (num (j, "shape", 0)));
            m.lfos[i].rateHz = num (j, "rate", 1.0);
            m.lfos[i].phase = num (j, "phase", 0.0);
        }
    if (const auto it = o.find ("envelopes"); it != o.end() && it->is_array())
        for (std::size_t i = 0; i < it->size() && i < m.envelopes.size(); ++i)
        {
            const auto& j = (*it)[i];
            if (! j.is_object()) continue;
            if (j.contains ("envelope")) { m.envelopes[i].env = detail::envelopeFrom (j); continue; }
            // premier format (attaque, decay, sustain, relâchement seuls)
            auto& e = m.envelopes[i].env;
            e.attack.ms = num (j, "a", 10.0);
            e.decay.ms = num (j, "d", 200.0);
            e.sustain = num (j, "s", 0.7);
            e.release.ms = num (j, "r", 300.0);
        }
    if (const auto it = o.find ("routes"); it != o.end() && it->is_array())
        for (const auto& j : *it)
        {
            if (! j.is_object() || m.routes.size() >= kMaxRoutes * 2) continue;
            Route r;
            r.source.kind = static_cast<SourceKind> (static_cast<int> (num (j, "kind", 0)));
            r.source.index = static_cast<int> (num (j, "index", 0));
            r.source.channel = static_cast<int> (num (j, "channel", 0));
            const double dest = num (j, "dest", 0);
            r.dest = dest > 0 && dest < 4.0e9 ? static_cast<ParamId> (dest) : 0;
            r.amount = num (j, "amount", 0.5);
            if (const auto c = j.find ("centred"); c != j.end() && c->is_boolean()) r.centred = c->get<bool>();
            m.routes.push_back (r);
        }
    return sanitised (std::move (m));
}

std::shared_ptr<const Compiled> compile (const Matrix& in, const ParameterSet& set)
{
    const Matrix m = sanitised (in);
    auto c = std::make_shared<Compiled>();
    c->lfos = m.lfos;
    for (std::size_t i = 0; i < m.envelopes.size(); ++i) c->envelopes[i] = EnvelopeRuntime (m.envelopes[i].env);
    for (const auto& r : m.routes)
    {
        CompiledRoute cr;
        cr.source = r.source;
        cr.amount = r.amount;
        cr.centred = r.centred;
        if (isInsertDest (r.dest))
        {
            cr.insertSlot = insertSlotOf (r.dest);   // le module est vérifié (continu, modulable) par le moteur à chaque bloc
            cr.insertParam = insertParamOf (r.dest);
        }
        else
        {
            cr.dest = set.indexOf (r.dest);
            if (cr.dest == ParameterSet::npos) continue;
            const auto& spec = set.spec (cr.dest);
            if (spec.kind != Kind::Continuous || ! spec.modulatable || spec.hidden) continue;   // seuls les paramètres continus se modulent
        }
        if (r.source.kind == SourceKind::Macro)
        {
            cr.macro = set.indexOf (control::macroParamId (r.source.index));
            if (cr.macro == ParameterSet::npos) continue;
        }
        c->routes.push_back (cr);
    }
    return c;
}

// ------------------------------------------------------------------------------------------------------------------ Modulator

void Modulator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Modulator::reset() noexcept
{
    lfoPhase_.fill (0.0);
    lfoHeld_.fill (0.0);
    env_.fill (EnvelopeRunner {});
    lastGate_ = false;
}

namespace
{
double lfoValue (LfoShape shape, double p, double held) noexcept
{
    switch (shape)
    {
        case LfoShape::Sine:       return std::sin (2.0 * 3.14159265358979323846 * p);
        case LfoShape::Triangle:   { const double q = std::fmod (p + 0.25, 1.0); return 2.0 * std::abs (2.0 * q - 1.0) - 1.0; }   // part de 0 en montant
        case LfoShape::SawUp:      return 2.0 * p - 1.0;
        case LfoShape::SawDown:    return 1.0 - 2.0 * p;
        case LfoShape::Square:     return p < 0.5 ? 1.0 : -1.0;
        case LfoShape::SampleHold: return held;
    }
    return 0.0;
}
} // namespace

void Modulator::process (int frames, bool gate, const midi::State& midi, const ParameterSet& set, std::vector<std::size_t>& touched, const InsertSink* inserts) noexcept
{
    if (compiled_ == nullptr || compiled_->routes.empty() || frames <= 0) { lastGate_ = gate; return; }
    const double dt = static_cast<double> (frames) / sampleRate_;
    const auto& c = *compiled_;

    // LFO : avance des phases (un nouveau tirage pour l'échantillonné-bloqué à chaque tour)
    std::array<double, kLfos> lfo {};
    for (int i = 0; i < kLfos; ++i)
    {
        const auto& d = c.lfos[static_cast<std::size_t> (i)];
        double& ph = lfoPhase_[static_cast<std::size_t> (i)];
        ph += d.rateHz * dt;
        if (ph >= 1.0)
        {
            ph = std::fmod (ph, 1.0);
            rng_ = rng_ * 1664525u + 1013904223u;
            lfoHeld_[static_cast<std::size_t> (i)] = static_cast<double> (rng_ >> 8) / 8388608.0 - 1.0;   // -1..1
        }
        lfo[static_cast<std::size_t> (i)] = lfoValue (d.shape, std::fmod (ph + d.phase, 1.0), lfoHeld_[static_cast<std::size_t> (i)]);
    }

    // enveloppes : le front montant de `gate` lance l'enveloppe, le front descendant son relâchement
    std::array<double, kEnvelopes> env {};
    for (int i = 0; i < kEnvelopes; ++i)
    {
        const auto& d = c.envelopes[static_cast<std::size_t> (i)];
        auto& r = env_[static_cast<std::size_t> (i)];
        if (gate && ! lastGate_) r.begin (d, sampleRate_);
        if (! gate && lastGate_ && ! r.finished()) r.noteOff (d, sampleRate_);
        if (! r.finished()) r.advance (d, sampleRate_, frames);
        env[static_cast<std::size_t> (i)] = r.finished() ? 0.0 : static_cast<double> (r.value());
    }
    lastGate_ = gate;

    auto channelRange = [] (int channel, int& first, int& last) { if (channel == 0) { first = 0; last = 15; } else { first = last = channel - 1; } };
    for (const auto& r : c.routes)
    {
        double v = 0.0;
        switch (r.source.kind)
        {
            case SourceKind::ControlChange:
            {
                int first = 0, last = 0;
                channelRange (r.source.channel, first, last);
                int best = 0;
                for (int ch = first; ch <= last; ++ch) best = std::max (best, static_cast<int> (midi.cc[static_cast<std::size_t> (ch)][static_cast<std::size_t> (r.source.index)]));
                v = best / 127.0;
                break;
            }
            case SourceKind::PitchBend:
            {
                int first = 0, last = 0;
                channelRange (r.source.channel, first, last);
                double best = 0.0;
                for (int ch = first; ch <= last; ++ch)
                {
                    const double b = (static_cast<int> (midi.bend[static_cast<std::size_t> (ch)]) - 64) / 64.0;
                    if (std::abs (b) > std::abs (best)) best = b;
                }
                v = best;
                break;
            }
            case SourceKind::Aftertouch:
            {
                int first = 0, last = 0;
                channelRange (r.source.channel, first, last);
                int best = 0;
                for (int ch = first; ch <= last; ++ch) best = std::max (best, static_cast<int> (midi.pressure[static_cast<std::size_t> (ch)]));
                v = best / 127.0;
                break;
            }
            case SourceKind::Macro: v = set.getNormalizedAt (r.macro); break;
            case SourceKind::Lfo: v = lfo[static_cast<std::size_t> (r.source.index)]; break;
            case SourceKind::Envelope: v = env[static_cast<std::size_t> (r.source.index)]; break;
            case SourceKind::None: break;
        }
        if (r.centred && ! isBipolar (r.source.kind)) v = v * 2.0 - 1.0;
        if (r.insertSlot >= 0)
        {
            if (inserts != nullptr && inserts->add != nullptr) inserts->add (inserts->context, r.insertSlot, r.insertParam, static_cast<float> (r.amount * v));
            continue;
        }
        set.addModOffsetAt (r.dest, static_cast<float> (r.amount * v));
        if (touched.size() < touched.capacity()) touched.push_back (r.dest);
    }
}
} // namespace neptune::core::modulation
