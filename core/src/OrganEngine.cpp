#include "neptune/core/OrganEngine.h"

#include <algorithm>
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

// ---------------------------------------------------------------------------------------------------- description

OrganDescription sanitised (OrganDescription d)
{
    for (auto& c : d.channel) c = std::clamp (c, 0, 16);
    d.splitLower = std::clamp (d.splitLower, 0, 127);
    d.splitUpper = std::clamp (d.splitUpper, 0, 127);
    for (auto& t : d.transpose) t = std::clamp (t, -24, 24);
    d.tuningCents = std::clamp (d.tuningCents, -100, 100);
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
    double sampleRate = 0.0;
    std::array<std::int16_t, 16 * 128> held; // clavier de setBfree par (canal, note) ; -1 : aucune
    std::array<std::uint8_t, 160> keyCount {};  // nombre de notes qui tiennent chaque touche
    OrganLive applied;
    bool haveApplied = false;
    std::vector<float> scratch;
    OrganDescription fallback;

    static constexpr int kChunk = 2048;
    Impl() : scratch (2 * kChunk) { held.fill (-1); }
    ~Impl() { if (engine != nullptr) nb3_destroy (engine); }

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
    impl_->engine = nb3_create (sampleRate);
    impl_->sampleRate = sampleRate;
    impl_->haveApplied = false;
    impl_->held.fill (-1);
    impl_->keyCount.fill (0);
    if (impl_->engine == nullptr) return false;
    reserved_.store (true, std::memory_order_release);
    return true;
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
