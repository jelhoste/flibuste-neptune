#include "neptune/core/FmVoices.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

namespace neptune::core::fm
{
using nlohmann::json;

FmDescription sanitised (FmDescription d)
{
    d.patch.sanitise();
    d.masterTuneCents = std::clamp (d.masterTuneCents, -100, 100);
    d.maxVoices = std::clamp (d.maxVoices, 1, kMaxVoices);
    d.glideMs = std::clamp (d.glideMs, 0, kMaxGlideMs);
    d.pitchBendRange = std::clamp (d.pitchBendRange, 0, 12);
    d.wheelPitchSens = std::clamp (d.wheelPitchSens, 0, 15);
    d.wheelAmpSens = std::clamp (d.wheelAmpSens, 0, 15);
    d.unisonVoices = std::clamp (d.unisonVoices, 1, kMaxUnison);
    return d;
}

std::string toJson (const FmDescription& in)
{
    const auto d = sanitised (in);
    json o;
    json bytes = json::array();
    for (int i = 0; i < kPatchBytes; ++i) bytes.push_back (d.patch.data[static_cast<std::size_t> (i)]);
    o["patch"] = std::move (bytes);
    o["master_tune_cents"] = d.masterTuneCents;
    o["polyphony"] = toString (d.polyphony);
    o["max_voices"] = d.maxVoices;
    o["voice_steal"] = toString (d.stealMode);
    o["glide_ms"] = d.glideMs;
    o["pitch_bend_range"] = d.pitchBendRange;
    o["wheel_pitch_sens"] = d.wheelPitchSens;
    o["wheel_amp_sens"] = d.wheelAmpSens;
    o["unison_voices"] = d.unisonVoices;
    return o.dump();
}

FmDescription fmFromJson (std::string_view text)
{
    FmDescription d;
    const auto doc = json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return d;
    auto num = [&doc] (const char* k, int def)
    {
        const auto it = doc.find (k);
        return it != doc.end() && it->is_number() ? static_cast<int> (std::clamp (it->get<double>(), -1.0e6, 1.0e6)) : def;
    };
    if (const auto it = doc.find ("patch"); it != doc.end() && it->is_array() && it->size() >= static_cast<std::size_t> (kVoiceDataBytes))
        for (std::size_t i = 0; i < it->size() && i < static_cast<std::size_t> (kPatchBytes); ++i)
            if ((*it)[i].is_number()) d.patch.data[i] = static_cast<std::uint8_t> (std::clamp ((*it)[i].get<int>(), 0, 255));
    d.masterTuneCents = num ("master_tune_cents", d.masterTuneCents);
    if (const auto it = doc.find ("polyphony"); it != doc.end() && it->is_string())
    {
        const auto v = it->get<std::string>();
        d.polyphony = v == "mono" ? PolyphonyMode::Mono : v == "legato" ? PolyphonyMode::Legato : PolyphonyMode::Poly;
    }
    d.maxVoices = num ("max_voices", d.maxVoices);
    if (const auto it = doc.find ("voice_steal"); it != doc.end() && it->is_string())
    {
        const auto v = it->get<std::string>();
        d.stealMode = v == "quietest" ? VoiceStealMode::Quietest : v == "keep_low" ? VoiceStealMode::KeepLow
                    : v == "keep_high" ? VoiceStealMode::KeepHigh : VoiceStealMode::Oldest;
    }
    d.glideMs = num ("glide_ms", d.glideMs);
    d.pitchBendRange = num ("pitch_bend_range", d.pitchBendRange);
    d.wheelPitchSens = num ("wheel_pitch_sens", d.wheelPitchSens);
    d.wheelAmpSens = num ("wheel_amp_sens", d.wheelAmpSens);
    d.unisonVoices = num ("unison_voices", d.unisonVoices);
    return sanitised (std::move (d));
}

// ---------------------------------------------------------------------------------------------------- voix

FmVoices::FmVoices() : voices_ (kMaxVoices + 8) {}
FmVoices::~FmVoices() = default;

void FmVoices::reserve()
{
    if (reserved_.load (std::memory_order_acquire)) return;
    for (auto& v : voices_)
        for (auto& n : v.notes) n = std::make_unique<Note>();
    reserved_.store (true, std::memory_order_release);
}

void FmVoices::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    allSoundOff();
}

void FmVoices::setDescription (const FmDescription* d) noexcept
{
    desc_ = d;
    if (d != nullptr) lfo_.reset (d->patch);
    numHeld_ = 0;
}

void FmVoices::allSoundOff() noexcept
{
    for (auto& v : voices_)
    {
        v.active = v.keydown = v.sustained = v.releasing = v.stolen = false;
        v.level = 0.0f;
    }
    numHeld_ = 0;
    heldKeys_ = 0;
    blockPos_ = kBlock;
}

int FmVoices::activeVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active; }));
}

int FmVoices::soundingVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(), [] (const Voice& v) { return v.active && ! v.stolen; }));
}

int FmVoices::heldVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices_.begin(), voices_.end(),
                                            [] (const Voice& v) { return v.active && ! v.releasing && ! v.stolen; }));
}

namespace
{
bool sacrificedBefore (bool relA, float levA, std::uint64_t ordA, bool relB, float levB, std::uint64_t ordB) noexcept
{
    if (relA != relB) return relA;
    if (levA != levB) return levA < levB;
    return ordA < ordB;
}
} // namespace

bool FmVoices::weakestVoice (bool& releasing, float& level, std::uint64_t& order) const noexcept
{
    const Voice* best = nullptr;
    for (const auto& v : voices_)
    {
        if (! v.active || v.stolen) continue;
        if (best == nullptr || sacrificedBefore (v.releasing, v.level, v.order, best->releasing, best->level, best->order)) best = &v;
    }
    if (best == nullptr) return false;
    releasing = best->releasing;
    level = best->level;
    order = best->order;
    return true;
}

void FmVoices::stealWeakest() noexcept
{
    Voice* best = nullptr;
    for (auto& v : voices_)
    {
        if (! v.active || v.stolen) continue;
        if (best == nullptr || sacrificedBefore (v.releasing, v.level, v.order, best->releasing, best->level, best->order)) best = &v;
    }
    if (best != nullptr) steal (*best);
}

void FmVoices::steal (Voice& v) noexcept
{
    v.stolen = true;
    v.fadeStep = static_cast<float> (1.0 / std::max (1.0, 0.005 * sampleRate_)); // 5 ms
}

void FmVoices::releaseVoice (Voice& v) noexcept
{
    if (! v.active || v.releasing || v.stolen) return;
    v.releasing = true;
    v.releasedBlocks = 0;
    for (int k = 0; k < v.unison; ++k)
        if (v.notes[static_cast<std::size_t> (k)]) v.notes[static_cast<std::size_t> (k)]->keyUp();
}

void FmVoices::releaseAll() noexcept
{
    for (auto& v : voices_)
    {
        v.keydown = false;
        v.sustained = false;
        releaseVoice (v);
    }
    numHeld_ = 0;
    heldKeys_ = 0;
}

void FmVoices::panic() noexcept
{
    for (auto& v : voices_)
        if (v.active && ! v.stolen) steal (v);
    numHeld_ = 0;
    heldKeys_ = 0;
}

void FmVoices::setSustain (bool on) noexcept
{
    sustain_ = on;
    if (on) return;
    for (auto& v : voices_)
        if (v.sustained && ! v.keydown)
        {
            v.sustained = false;
            releaseVoice (v);
        }
}

void FmVoices::pushHeld (int key, int channel, int velocity) noexcept
{
    removeHeld (key, channel);
    if (numHeld_ == kMaxHeld)
    {
        for (int i = 1; i < kMaxHeld; ++i) held_[i - 1] = held_[i];
        --numHeld_;
    }
    held_[numHeld_++] = { key, channel, velocity };
}

void FmVoices::removeHeld (int key, int channel) noexcept
{
    for (int i = 0; i < numHeld_; ++i)
        if (held_[i].key == key && held_[i].channel == channel)
        {
            for (int j = i + 1; j < numHeld_; ++j) held_[j - 1] = held_[j];
            --numHeld_;
            return;
        }
}

FmVoices::Voice* FmVoices::allocate (int key, bool& dropNew) noexcept
{
    dropNew = false;
    const int limit = desc_ != nullptr && desc_->polyphony == PolyphonyMode::Poly ? desc_->maxVoices : 1;
    // limite de notes tenues : on fait s'éteindre une voix selon le mode de vol
    for (int guard = 0; heldVoices() >= limit && guard < kMaxVoices; ++guard)
    {
        Voice* victim = nullptr;
        const auto mode = desc_ != nullptr ? desc_->stealMode : VoiceStealMode::Oldest;
        for (auto& v : voices_)
        {
            if (! v.active || v.releasing || v.stolen) continue;
            if (victim == nullptr) { victim = &v; continue; }
            switch (mode)
            {
                case VoiceStealMode::Oldest: if (v.order < victim->order) victim = &v; break;
                case VoiceStealMode::Quietest: if (v.level < victim->level) victim = &v; break;
                case VoiceStealMode::KeepLow: if (v.key > victim->key) victim = &v; break;       // on vole la plus aiguë
                case VoiceStealMode::KeepHigh: if (v.key < victim->key) victim = &v; break;      // on vole la plus grave
            }
        }
        if (victim == nullptr) break;
        if ((mode == VoiceStealMode::KeepLow && victim->key <= key) || (mode == VoiceStealMode::KeepHigh && victim->key >= key))
        {
            dropNew = true; // la nouvelle note est elle-même la plus à sacrifier
            return nullptr;
        }
        steal (*victim);
    }
    for (auto& v : voices_)
        if (! v.active) return &v;
    // plus de place du tout : la plus ancienne voix déjà en fondu, sinon la plus ancienne
    Voice* best = nullptr;
    for (auto& v : voices_)
        if (v.stolen && (best == nullptr || v.order < best->order)) best = &v;
    if (best == nullptr)
        for (auto& v : voices_)
            if (best == nullptr || v.order < best->order) best = &v;
    return best;
}

void FmVoices::startVoice (Voice& v, int channel, int key, int velocity, double startGlide) noexcept
{
    const auto& d = *desc_;
    Patch p = d.patch;
    // macro « vitesse des enveloppes » : décale les 4 vitesses de chaque opérateur (1 qrate = 64 / 41 pas de vitesse DX7)
    const int rateShift = static_cast<int> (std::lround (12.5 * static_cast<double> (std::clamp (blockLive_.envSpeed, -1.0f, 1.0f))));
    if (rateShift != 0)
        for (int op = 0; op < kOperators; ++op)
            for (int r = 0; r < 4; ++r)
                p.setOp (op, kRate1 + r, p.op (op, kRate1 + r) + rateShift);
    const int midi = std::clamp (key + (p.global (kTransposeOffset) - 24), 0, 127);
    v.active = true;
    v.keydown = true;
    v.sustained = false;
    v.releasing = false;
    v.stolen = false;
    v.fade = 1.0f;
    v.channel = channel;
    v.key = key;
    v.velocity = velocity;
    v.baseKey = key;
    v.order = ++counter_;
    v.unison = std::clamp (d.unisonVoices, 1, kMaxUnison);
    v.level = 0.0f;
    v.silentBlocks = 0;
    v.releasedBlocks = 0;
    v.glide = startGlide;
    v.glideTarget = 0.0;
    for (int k = 0; k < v.unison; ++k)
        if (v.notes[static_cast<std::size_t> (k)]) v.notes[static_cast<std::size_t> (k)]->start (p, midi, velocity);
}

void FmVoices::noteOn (int channel, int key, float velocity) noexcept
{
    if (desc_ == nullptr || ! reserved()) return;
    const int vel = std::clamp (static_cast<int> (std::lround (velocity * 127.0f)), 1, 127);
    if (heldKeys_ == 0) lfo_.keyDown(); // le LFO redémarre à la première touche
    ++heldKeys_;

    if (desc_->polyphony != PolyphonyMode::Poly)
    {
        Voice& v = voices_[0];
        const bool sounding = v.active && ! v.stolen && v.keydown;
        pushHeld (key, channel, vel);
        if (sounding && desc_->polyphony == PolyphonyMode::Legato)
        {
            // legato : la voix continue ; la hauteur glisse vers la nouvelle touche
            v.key = key;
            v.channel = channel;
            v.glideTarget = static_cast<double> (key - v.baseKey);
            if (desc_->glideMs <= 0) v.glide = v.glideTarget;
            return;
        }
        // mono (ou première touche) : la note repart ; le glissando part de la hauteur précédente si une voix sonnait
        const double from = v.active && ! v.stolen ? v.glide + static_cast<double> (v.key - key) : 0.0;
        for (std::size_t i = 1; i < voices_.size(); ++i) // une éventuelle queue d'une voix de poly précédente
            if (voices_[i].active && ! voices_[i].stolen) steal (voices_[i]);
        startVoice (v, channel, key, vel, desc_->glideMs > 0 ? from : 0.0);
        return;
    }

    bool dropNew = false;
    Voice* v = allocate (key, dropNew);
    if (v == nullptr || dropNew) { heldKeys_ = std::max (0, heldKeys_ - 1); return; }
    startVoice (*v, channel, key, vel, 0.0);
}

void FmVoices::noteOff (int channel, int key) noexcept
{
    if (desc_ == nullptr) return;
    heldKeys_ = std::max (0, heldKeys_ - 1);
    if (desc_->polyphony != PolyphonyMode::Poly)
    {
        removeHeld (key, channel);
        Voice& v = voices_[0];
        if (! v.active || v.stolen) return;
        if (numHeld_ > 0)
        {
            // une touche encore tenue reprend la main (legato : on glisse vers elle ; mono : elle repart)
            const auto back = held_[numHeld_ - 1];
            if (v.key != back.key)
            {
                if (desc_->polyphony == PolyphonyMode::Legato && v.keydown)
                {
                    v.key = back.key;
                    v.channel = back.channel;
                    v.glideTarget = static_cast<double> (back.key - v.baseKey);
                    if (desc_->glideMs <= 0) v.glide = v.glideTarget;
                }
                else
                    startVoice (v, back.channel, back.key, back.velocity,
                                desc_->glideMs > 0 ? v.glide + static_cast<double> (v.key - back.key) : 0.0);
            }
            return;
        }
        if (v.keydown)
        {
            v.keydown = false;
            if (sustain_) v.sustained = true; else releaseVoice (v);
        }
        return;
    }
    for (auto& v : voices_)
        if (v.active && v.keydown && ! v.stolen && v.channel == channel && v.key == key)
        {
            v.keydown = false;
            if (sustain_) v.sustained = true; else releaseVoice (v);
        }
}

// ---------------------------------------------------------------------------------------------------- rendu

void FmVoices::renderBlock (const FmLive& live) noexcept
{
    blockL_.fill (0.0f);
    blockR_.fill (0.0f);
    if (desc_ == nullptr) return;
    const auto& d = *desc_;
    const std::int32_t lfoValue = lfo_.sample();
    const std::int32_t lfoDelay = lfo_.delay();

    Controls c;
    c.pitchBend = live.pitchBend;
    c.pitchBendRange = d.pitchBendRange;
    c.wheelPitchDepth = static_cast<int> (std::lround (255.0 * live.modWheel / 127.0 * d.wheelPitchSens / 15.0));
    c.wheelAmpDepth = static_cast<int> (std::lround (99.0 * live.modWheel / 127.0 * d.wheelAmpSens / 15.0));

    const std::uint8_t carriers = algorithmGraph (d.patch.algorithm()).carrierMask;
    // décalages de niveau en doublements Q24 : brillance sur les modulateurs (±2 doublements = ±12 dB), pas DX7 sur tous
    const auto q24 = static_cast<double> (1 << 24);
    std::array<std::int32_t, kOperators> levelOffset {};
    for (int op = 0; op < kOperators; ++op)
    {
        double off = static_cast<double> (std::clamp (live.opLevel[static_cast<std::size_t> (op)], -99.0f, 99.0f)) * (q24 / 8.0);
        if (((carriers >> op) & 1) == 0) off += static_cast<double> (std::clamp (live.brightness, -1.0f, 1.0f)) * 2.0 * q24;
        levelOffset[static_cast<std::size_t> (op)] = static_cast<std::int32_t> (std::lround (off));
    }
    const int feedback = std::clamp (d.patch.feedback() + static_cast<int> (std::lround (7.0f * std::clamp (live.feedbackDrive, 0.0f, 1.0f))), 0, 7);
    const double tuneCents = static_cast<double> (live.tuneCents) + d.masterTuneCents;
    const double glideStep = d.glideMs > 0 ? static_cast<double> (kBlock) / (d.glideMs * 0.001 * sampleRate_) : 1.0e9; // fraction par bloc

    for (auto& v : voices_)
    {
        if (! v.active) continue;
        // glissando : on rapproche le décalage courant de la cible, à vitesse constante (en demi-tons par bloc)
        if (v.glide != v.glideTarget)
        {
            const double distance = std::abs (v.glideTarget - v.glide);
            const double step = glideStep >= 1.0e8 ? distance : std::max (0.0001, glideStep * 12.0); // 12 demi-tons par durée de glissando
            v.glide = distance <= step ? v.glideTarget : v.glide + (v.glideTarget > v.glide ? step : -step);
        }
        const std::int32_t glideQ = static_cast<std::int32_t> (std::lround (v.glide / 12.0 * q24));
        const float norm = 1.0f / std::sqrt (static_cast<float> (v.unison));
        float peak = 0.0f;
        for (int k = 0; k < v.unison; ++k)
        {
            auto& note = v.notes[static_cast<std::size_t> (k)];
            if (! note) continue;
            const double x = v.unison > 1 ? (2.0 * k / (v.unison - 1)) - 1.0 : 0.0; // -1..1
            const double cents = tuneCents + x * static_cast<double>(live.unisonDetuneCents);
            note->setDetune (static_cast<std::int32_t> (std::lround (cents / 1200.0 * q24)));
            note->setGlide (glideQ);
            for (int op = 0; op < kOperators; ++op) note->setLevelOffset (op, levelOffset[static_cast<std::size_t> (op)]);
            note->setFeedback (feedback);
            scratch_.fill (0);
            note->compute (scratch_.data(), lfoValue, lfoDelay, c);
            // répartition stéréo à puissance constante
            const double pan = x * static_cast<double>(std::clamp (live.unisonSpread, 0.0f, 1.0f));
            const double theta = (pan + 1.0) * 0.78539816339;
            const float gl = static_cast<float> (std::cos (theta)) * 1.41421356f; // au centre : gain 1 des deux côtés
            const float gr = static_cast<float> (std::sin (theta)) * 1.41421356f;
            float fade = v.fade;
            for (int i = 0; i < kBlock; ++i)
            {
                if (v.stolen) fade = std::max (0.0f, fade - v.fadeStep);
                const float s = static_cast<float> (scratch_[static_cast<std::size_t> (i)]) * kOutputScale * norm * fade;
                blockL_[static_cast<std::size_t> (i)] += s * gl;
                blockR_[static_cast<std::size_t> (i)] += s * gr;
                peak = std::max (peak, std::abs (s));
                if (k == v.unison - 1 && i == kBlock - 1) v.fade = fade;
            }
        }
        v.level = peak;
        if (v.stolen && v.fade <= 0.0f) { v.active = false; continue; }
        if (v.releasing)
        {
            ++v.releasedBlocks;
            v.silentBlocks = peak < 1.0e-6f ? v.silentBlocks + 1 : 0;
            // fin de note : silence soutenu après le relâchement, ou 20 s (un niveau final non nul ne s'éteint jamais seul)
            if (v.silentBlocks >= 8) v.active = false;
            else if (v.releasedBlocks * kBlock > static_cast<int> (20.0 * sampleRate_)) steal (v);
        }
    }
}

void FmVoices::render (float* left, float* right, int numFrames, float gainStart, float gainEnd, const FmLive& live) noexcept
{
    if (left == nullptr || numFrames <= 0) return;
    const float step = (gainEnd - gainStart) / static_cast<float> (numFrames);
    float gain = gainStart;
    blockLive_ = live;
    for (int i = 0; i < numFrames; ++i, gain += step)
    {
        if (blockPos_ >= kBlock)
        {
            renderBlock (live);
            blockPos_ = 0;
        }
        const float l = blockL_[static_cast<std::size_t> (blockPos_)], r = blockR_[static_cast<std::size_t> (blockPos_)];
        ++blockPos_;
        left[i] += l * gain;
        if (right != nullptr) right[i] += r * gain; else left[i] += r * gain;
    }
}
} // namespace neptune::core::fm
