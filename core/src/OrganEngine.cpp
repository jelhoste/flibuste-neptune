#include "neptune/core/OrganEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include <nlohmann/json.hpp>

#include "neptune/core/OrganCore.h"

namespace neptune::core::organ
{
using nlohmann::json;

OrganLive::OrganLive()
{
    for (auto& m : drawbars) m.fill (0);
    drawbars[0] = { 8, 8, 6, 0, 0, 0, 0, 0, 0 };
}

OrganLive liveFromValues (const double* v) noexcept
{
    OrganLive l;
    auto db = [] (double x) { return std::clamp (static_cast<int> (std::lround (x)), 0, 8); };
    for (int i = 0; i < kDrawbars; ++i)
    {
        l.drawbars[0][static_cast<std::size_t> (i)] = db (v[0 + i]);
        l.drawbars[1][static_cast<std::size_t> (i)] = db (v[9 + i]);
    }
    l.drawbars[2][0] = db (v[18]);
    l.drawbars[2][2] = db (v[19]);
    l.percussionOn = v[20] >= 0.5;
    l.percussionSoft = v[21] >= 0.5;
    l.percussionFast = v[22] >= 0.5;
    l.percussionThird = v[23] >= 0.5;
    l.vibratoMode = std::clamp (static_cast<int> (std::lround (v[24])), 0, kVibratoModes - 1);
    l.vibratoUpper = v[25] >= 0.5;
    l.vibratoLower = v[26] >= 0.5;
    l.leslieSpeed = std::clamp (static_cast<int> (std::lround (v[27])), 0, 2);
    l.leslieMix = static_cast<float> (std::clamp (v[28] / 100.0, 0.0, 1.0));
    l.driveOn = v[29] >= 0.5;
    l.driveInput = static_cast<float> (std::clamp (v[30] / 100.0, 0.0, 1.0));
    l.driveCharacter = static_cast<float> (std::clamp (v[31] / 100.0, 0.0, 1.0));
    l.driveOutput = static_cast<float> (std::clamp (v[32] / 100.0, 0.0, 1.0));
    l.reverbMix = static_cast<float> (std::clamp (v[33] / 100.0, 0.0, 1.0));
    l.vibratoDepth = static_cast<float> (std::clamp (v[34] / 100.0, 0.0, 1.0));
    return l;
}

// ---------------------------------------------------------------------------------------------------- description

OrganDescription sanitised (OrganDescription d)
{
    for (auto& c : d.channel) c = std::clamp (c, 0, 16);
    d.splitLower = std::clamp (d.splitLower, 0, 127);
    d.splitUpper = std::clamp (d.splitUpper, 0, 127);
    for (auto& t : d.transpose) t = std::clamp (t, -24, 24);
    d.tuningCents = std::clamp (d.tuningCents, -100, 100);
    d.hornSlowRpm = std::clamp (d.hornSlowRpm, 5.0, 200.0);
    d.hornFastRpm = std::clamp (d.hornFastRpm, 100.0, 900.0);
    d.drumSlowRpm = std::clamp (d.drumSlowRpm, 5.0, 100.0);
    d.drumFastRpm = std::clamp (d.drumFastRpm, 60.0, 600.0);
    d.hornAcc = std::clamp (d.hornAcc, 0.05, 2.0);
    d.hornDec = std::clamp (d.hornDec, 0.05, 2.0);
    d.drumAcc = std::clamp (d.drumAcc, 0.5, 10.0);
    d.drumDec = std::clamp (d.drumDec, 0.5, 10.0);
    d.hornLevel = std::clamp (d.hornLevel, 0.0, 1.0);
    for (auto& row : d.taperDb)
        for (auto& v : row) v = std::clamp (v, -24, 12);
    return d;
}

std::string toJson (const OrganDescription& d)
{
    json o;
    o["channels"] = d.channel;
    o["split_lower"] = d.splitLower;
    o["split_upper"] = d.splitUpper;
    o["transpose"] = d.transpose;
    o["tuning_cents"] = d.tuningCents;
    o["wiring"] = { { "extended", d.extended }, { "foldback", d.foldback }, { "taper_db", d.taperDb } };
    o["leslie"] = { { "horn_slow_rpm", d.hornSlowRpm }, { "horn_fast_rpm", d.hornFastRpm }, { "drum_slow_rpm", d.drumSlowRpm },
                    { "drum_fast_rpm", d.drumFastRpm }, { "horn_acc", d.hornAcc }, { "horn_dec", d.hornDec }, { "drum_acc", d.drumAcc },
                    { "drum_dec", d.drumDec }, { "horn_level", d.hornLevel } };
    return o.dump();
}

OrganDescription organFromJson (std::string_view text)
{
    OrganDescription d;
    const auto o = json::parse (text.begin(), text.end(), nullptr, false);
    if (! o.is_object()) return d;
    auto num = [&o] (const char* key, int fallback)
    {
        const auto it = o.find (key);
        return it != o.end() && it->is_number() ? it->get<int>() : fallback;
    };
    auto arr = [&o] (const char* key, std::array<int, kManuals>& out)
    {
        const auto it = o.find (key);
        if (it == o.end() || ! it->is_array() || it->size() != kManuals) return;
        for (std::size_t i = 0; i < kManuals; ++i)
            if ((*it)[i].is_number()) out[i] = (*it)[i].get<int>();
    };
    arr ("channels", d.channel);
    d.splitLower = num ("split_lower", d.splitLower);
    d.splitUpper = num ("split_upper", d.splitUpper);
    arr ("transpose", d.transpose);
    d.tuningCents = num ("tuning_cents", d.tuningCents);
    if (const auto w = o.find ("wiring"); w != o.end() && w->is_object())
    {
        if (const auto it = w->find ("extended"); it != w->end() && it->is_boolean()) d.extended = it->get<bool>();
        if (const auto it = w->find ("foldback"); it != w->end() && it->is_boolean()) d.foldback = it->get<bool>();
        if (const auto it = w->find ("taper_db"); it != w->end() && it->is_array() && it->size() == 9)
            for (std::size_t b = 0; b < 9; ++b)
                if ((*it)[b].is_array() && (*it)[b].size() == 5)
                    for (std::size_t k = 0; k < 5; ++k)
                        if ((*it)[b][k].is_number()) d.taperDb[b][k] = (*it)[b][k].get<int>();
    }
    if (const auto l = o.find ("leslie"); l != o.end() && l->is_object())
    {
        auto real = [&l] (const char* key, double fallback)
        {
            const auto it = l->find (key);
            return it != l->end() && it->is_number() && std::isfinite (it->get<double>()) ? it->get<double>() : fallback;
        };
        d.hornSlowRpm = real ("horn_slow_rpm", d.hornSlowRpm);
        d.hornFastRpm = real ("horn_fast_rpm", d.hornFastRpm);
        d.drumSlowRpm = real ("drum_slow_rpm", d.drumSlowRpm);
        d.drumFastRpm = real ("drum_fast_rpm", d.drumFastRpm);
        d.hornAcc = real ("horn_acc", d.hornAcc);
        d.hornDec = real ("horn_dec", d.hornDec);
        d.drumAcc = real ("drum_acc", d.drumAcc);
        d.drumDec = real ("drum_dec", d.drumDec);
        d.hornLevel = real ("horn_level", d.hornLevel);
    }
    return sanitised (d);
}

// ---------------------------------------------------------------------------------------------------- notes

int organKeyFor (const OrganDescription& d, int channel, int note) noexcept
{
    if (note < 0 || note > 127) return -1;
    const int ch = channel + 1;
    int manual = -1;
    if (d.channel[0] != 0 && ch == d.channel[0])
    {
        manual = 0;
        if (d.splitLower > 0 && note < d.splitLower) manual = 2;
        else if (d.splitUpper > 0 && note < d.splitUpper) manual = 1;
    }
    else if (d.channel[1] != 0 && ch == d.channel[1]) manual = 1;
    else if (d.channel[2] != 0 && ch == d.channel[2]) manual = 2;
    if (manual < 0) return -1;
    static const int keyBase[kManuals] = { 0, 64, 128 }, firstNote[kManuals] = { 36, 36, 24 }, count[kManuals] = { 61, 61, 32 };
    const int key = keyBase[manual] + note + d.transpose[static_cast<std::size_t> (manual)] - firstNote[manual];
    return key >= keyBase[manual] && key < keyBase[manual] + count[manual] ? key : -1;
}

// ---------------------------------------------------------------------------------------------------- voix

struct OrganVoices::Impl
{
    nb3_engine* engine = nullptr;
    std::atomic<nb3_engine*> pending { nullptr };   // orgue accordé autrement, construit par le thread de message
    std::atomic<nb3_engine*> retired { nullptr };   // orgue remplacé, à libérer par le thread de message
    int tuningCents = 0;                            // accord de l'orgue en cours ou en attente (thread de message)
    bool extended = false, foldback = true;         // câblage idem
    std::array<std::array<int, 5>, 9> taperDb {};

    nb3_engine* build (double rate) const
    {
        nb3_wiring w {};
        w.foldback = ! extended || foldback ? 1 : 0;
        w.taperOn = extended ? 1 : 0;
        for (std::size_t b = 0; b < 9; ++b)
            for (std::size_t k = 0; k < 5; ++k) w.taperDb[b][k] = extended ? static_cast<float> (taperDb[b][k]) : 0.0f;
        return nb3_create_ex (rate, 440.0 * std::pow (2.0, tuningCents / 1200.0), &w);
    }
    double sampleRate = 0.0;
    std::array<std::int16_t, 16 * 128> held; // clavier de setBfree par (canal, note) ; -1 : aucune
    std::array<std::uint8_t, 160> keyCount {};  // nombre de notes qui tiennent chaque touche
    OrganLive applied;
    bool haveApplied = false;
    std::vector<float> scratch;
    OrganDescription fallback;

    static constexpr int kChunk = 2048;
    Impl() : scratch (2 * kChunk) { held.fill (-1); }
    ~Impl()
    {
        if (engine != nullptr) nb3_destroy (engine);
        if (auto* p = pending.exchange (nullptr)) nb3_destroy (p);
        if (auto* r = retired.exchange (nullptr)) nb3_destroy (r);
    }

    static int cc (float v01) { return static_cast<int> (std::lround (std::clamp (v01, 0.0f, 1.0f) * 127.0f)); }

    void applyChanges (const OrganLive& n, int tuningCents)
    {
        const OrganLive& a = applied;
        const bool first = ! haveApplied;
        for (int m = 0; m < kManuals; ++m)
            if (first || n.drawbars[static_cast<std::size_t> (m)] != a.drawbars[static_cast<std::size_t> (m)])
            {
                int v[kDrawbars];
                for (int i = 0; i < kDrawbars; ++i) v[i] = n.drawbars[static_cast<std::size_t> (m)][static_cast<std::size_t> (i)];
                nb3_set_drawbars (engine, m, v);
            }
        if (first || n.percussionOn != a.percussionOn) nb3_control (engine, "percussion.enable", n.percussionOn ? 127 : 0);
        if (first || n.percussionSoft != a.percussionSoft) nb3_control (engine, "percussion.volume", n.percussionSoft ? 127 : 0);
        if (first || n.percussionFast != a.percussionFast) nb3_control (engine, "percussion.decay", n.percussionFast ? 127 : 0);
        // « harmonique » de setBfree : valeur haute = 2e harmonique, basse = 3e
        if (first || n.percussionThird != a.percussionThird) nb3_control (engine, "percussion.harmonic", n.percussionThird ? 0 : 127);
        if (first || n.vibratoMode != a.vibratoMode)
        {
            if (n.vibratoMode > 0) nb3_control (engine, "vibrato.knob", (n.vibratoMode - 1) * 23);
        }
        if (first || n.vibratoMode != a.vibratoMode || n.vibratoUpper != a.vibratoUpper || n.vibratoLower != a.vibratoLower)
        {
            nb3_control (engine, "vibrato.upper", n.vibratoMode > 0 && n.vibratoUpper ? 127 : 0);
            nb3_control (engine, "vibrato.lower", n.vibratoMode > 0 && n.vibratoLower ? 127 : 0);
        }
        if (first || n.leslieSpeed != a.leslieSpeed)
        {
            // « rotary.speed-preset » : u / 43 = 0 lent, 1 arrêt, 2 rapide
            const int u = n.leslieSpeed == kLeslieBrake ? 43 : n.leslieSpeed == kLeslieTremolo ? 86 : 0;
            nb3_control (engine, "rotary.speed-preset", u);
        }
        if (first || n.leslieMix != a.leslieMix) nb3_set_leslie_mix (engine, n.leslieMix);
        if (first || n.driveOn != a.driveOn) nb3_control (engine, "overdrive.enable", n.driveOn ? 127 : 0);
        if (first || n.driveInput != a.driveInput) nb3_control (engine, "overdrive.inputgain", cc (n.driveInput));
        if (first || n.driveCharacter != a.driveCharacter) nb3_control (engine, "overdrive.character", cc (n.driveCharacter));
        if (first || n.driveOutput != a.driveOutput) nb3_control (engine, "overdrive.outputgain", cc (n.driveOutput));
        if (first || n.reverbMix != a.reverbMix) nb3_control (engine, "reverb.mix", cc (n.reverbMix));
        // le vibrato / chorus : la profondeur change les tables du scanner (le vibrato.knob reste tel quel)
        if (first || n.vibratoDepth != a.vibratoDepth) nb3_set_vibrato_depth (engine, n.vibratoDepth * 2.0f);
        (void) tuningCents;
        applied = n;
        haveApplied = true;
    }
};

OrganVoices::OrganVoices() : impl_ (new Impl) {}
OrganVoices::~OrganVoices() { delete impl_; }

bool OrganVoices::reserve (double sampleRate)
{
    if (impl_->engine != nullptr && impl_->sampleRate == sampleRate) return true;
    reserved_.store (false, std::memory_order_release);
    if (impl_->engine != nullptr) { nb3_destroy (impl_->engine); impl_->engine = nullptr; }
    if (auto* p = impl_->pending.exchange (nullptr)) nb3_destroy (p);
    impl_->engine = impl_->build (sampleRate);
    impl_->sampleRate = sampleRate;
    impl_->haveApplied = false;
    impl_->held.fill (-1);
    impl_->keyCount.fill (0);
    if (impl_->engine == nullptr) return false;
    reserved_.store (true, std::memory_order_release);
    return true;
}

void OrganVoices::requestBuild (const OrganDescription& d)
{
    const int cents = std::clamp (d.tuningCents, -100, 100);
    // en mode classique, le câblage étendu est ignoré : on ne reconstruit rien pour un réglage qui n'agit pas
    const bool ext = d.extended;
    const bool fold = d.foldback;
    const auto taper = d.taperDb;
    if (cents == impl_->tuningCents && ext == impl_->extended && (! ext || (fold == impl_->foldback && taper == impl_->taperDb))) return;
    impl_->tuningCents = cents;
    impl_->extended = ext;
    impl_->foldback = fold;
    impl_->taperDb = taper;
    if (! reserved()) return; // reserve() créera l'orgue avec ces réglages
    auto* fresh = impl_->build (impl_->sampleRate);
    if (fresh == nullptr) return;
    if (auto* old = impl_->pending.exchange (fresh)) nb3_destroy (old); // une demande plus récente remplace celle qui n'a pas été adoptée
}

void OrganVoices::collect()
{
    if (auto* r = impl_->retired.exchange (nullptr)) nb3_destroy (r);
}

void OrganVoices::noteOn (int channel, int note) noexcept
{
    if (! reserved() || channel < 0 || channel > 15 || note < 0 || note > 127) return;
    auto& slot = impl_->held[static_cast<std::size_t> (channel * 128 + note)];
    if (slot >= 0) return; // déjà tenue
    const int key = organKeyFor (desc_ != nullptr ? *desc_ : impl_->fallback, channel, note);
    if (key < 0) return;
    slot = static_cast<std::int16_t> (key);
    if (impl_->keyCount[static_cast<std::size_t> (key)]++ == 0) nb3_key_on (impl_->engine, key);
}

void OrganVoices::noteOff (int channel, int note) noexcept
{
    if (! reserved() || channel < 0 || channel > 15 || note < 0 || note > 127) return;
    auto& slot = impl_->held[static_cast<std::size_t> (channel * 128 + note)];
    if (slot < 0) return;
    const int key = slot;
    slot = -1;
    auto& c = impl_->keyCount[static_cast<std::size_t> (key)];
    if (c > 0 && --c == 0) nb3_key_off (impl_->engine, key);
}

void OrganVoices::allNotesOff() noexcept
{
    if (! reserved()) return;
    impl_->held.fill (-1);
    impl_->keyCount.fill (0);
    nb3_all_keys_off (impl_->engine);
}

void OrganVoices::panic() noexcept { allNotesOff(); }

int OrganVoices::heldKeys() const noexcept
{
    int n = 0;
    for (auto c : impl_->keyCount) n += c > 0 ? 1 : 0;
    return n;
}

void OrganVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const OrganLive& live, bool add) noexcept
{
    if (left == nullptr || numFrames <= 0) return;
    if (! reserved())
    {
        if (! add)
        {
            std::fill (left, left + numFrames, 0.0f);
            if (right != nullptr) std::fill (right, right + numFrames, 0.0f);
        }
        return;
    }
    // orgue accordé autrement : adopté ici (l'ancien est remis au thread de message) ; les touches tenues sont rejouées
    if (impl_->retired.load (std::memory_order_acquire) == nullptr)
        if (auto* fresh = impl_->pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            impl_->retired.store (impl_->engine, std::memory_order_release);
            impl_->engine = fresh;
            impl_->haveApplied = false;
            leslieDirty_ = true;
            for (std::size_t k = 0; k < impl_->keyCount.size(); ++k)
                if (impl_->keyCount[k] > 0) nb3_key_on (impl_->engine, static_cast<int> (k));
        }
    if (leslieDirty_)
    {
        const OrganDescription& d = desc_ != nullptr ? *desc_ : impl_->fallback;
        nb3_leslie les;
        les.hornSlowRpm = static_cast<float> (d.hornSlowRpm);
        les.hornFastRpm = static_cast<float> (d.hornFastRpm);
        les.drumSlowRpm = static_cast<float> (d.drumSlowRpm);
        les.drumFastRpm = static_cast<float> (d.drumFastRpm);
        les.hornAcc = static_cast<float> (d.hornAcc);
        les.hornDec = static_cast<float> (d.hornDec);
        les.drumAcc = static_cast<float> (d.drumAcc);
        les.drumDec = static_cast<float> (d.drumDec);
        les.hornLevel = static_cast<float> (d.hornLevel);
        nb3_set_leslie (impl_->engine, &les);
        leslieDirty_ = false;
        impl_->haveApplied = false; // computeRotationSpeeds a rebasculé la vitesse : on la renvoie
    }
    impl_->applyChanges (live, desc_ != nullptr ? desc_->tuningCents : 0);
    const float step = (gainEnd - gainStart) / static_cast<float> (numFrames);
    float g = gainStart;
    for (int done = 0; done < numFrames;)
    {
        const int n = std::min (Impl::kChunk, numFrames - done);
        float* l = impl_->scratch.data();
        float* r = l + Impl::kChunk;
        nb3_process (impl_->engine, l, r, n);
        for (int i = 0; i < n; ++i, g += step)
        {
            float* outL = left + done + i;
            float* outR = right != nullptr ? right + done + i : nullptr;
            if (add)
            {
                *outL += l[i] * g;
                if (outR != nullptr) *outR += r[i] * g; else *outL += r[i] * g;
            }
            else
            {
                *outL = l[i] * g;
                if (outR != nullptr) *outR = r[i] * g;
            }
        }
        done += n;
    }
}
} // namespace neptune::core::organ
