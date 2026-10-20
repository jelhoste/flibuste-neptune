#include "neptune/core/StockModule.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Mixer.h"
#include "neptune/core/ChorusModule.h"
#include "neptune/core/CompressorModule.h"
#include "neptune/core/DelayModule.h"
#include "neptune/core/EqualizerModule.h"
#include "neptune/core/ShaperModule.h"
#include "neptune/core/UtilityModule.h"

namespace neptune::core
{
ModuleRunner::ModuleRunner (std::unique_ptr<StockModule> module) : module_ (std::move (module))
{
    std::string error;
    auto trim = [] (ParamId id, const char* name)
    {
        auto s = ParamSpec::continuous (id, name, Unit::Decibel, -24.0, 24.0, 0.0);
        s.automatable = true;
        return s;
    };
    ok_ = params_.add (trim (stock::inputTrim, "Input Trim"), &error)
       && params_.add (trim (stock::outputTrim, "Output Trim"), &error)
       && params_.add (ParamSpec::continuous (stock::mix, "Mix", Unit::Percent, 0.0, 100.0, module_->defaultMixPercent()), &error)
       && params_.add (ParamSpec::toggle (stock::bypass, "Bypass"), &error)
       && module_->declareParams (params_, &error);
    idxIn_ = params_.indexOf (stock::inputTrim);
    idxOut_ = params_.indexOf (stock::outputTrim);
    idxMix_ = params_.indexOf (stock::mix);
    idxBypass_ = params_.indexOf (stock::bypass);
    module_->bind (params_);
}

void ModuleRunner::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max (1, maxBlockSize);
    dryL_.assign (static_cast<std::size_t> (maxBlock_), 0.0f);
    dryR_.assign (static_cast<std::size_t> (maxBlock_), 0.0f);
    const auto ringSize = static_cast<std::size_t> (module_->maxLatencySamples (sampleRate_) + 2);
    ringL_.assign (ringSize, 0.0f);
    ringR_.assign (ringSize, 0.0f);
    ringPos_ = 0;
    ringLatency_ = 0;
    module_->prepare (sampleRate_, maxBlock_);
    reset();
}

void ModuleRunner::reset() noexcept
{
    module_->reset();
    lastIn_ = lastOut_ = 1.0f;
    lastMix_ = params_.getAt (idxBypass_) >= 0.5 ? 0.0f : static_cast<float> (params_.getAt (idxMix_) / 100.0);
    idle_ = false;
}

void ModuleRunner::process (float* left, float* right, int n, bool bypassed) noexcept
{
    if (maxBlock_ <= 0 || n <= 0) return;
    if (n > maxBlock_) // bloc plus long que prévu : par morceaux (sans allocation)
    {
        for (int off = 0; off < n; off += maxBlock_) process (left + off, right + off, std::min (maxBlock_, n - off), bypassed);
        return;
    }
    const float mixTarget = bypassed ? 0.0f : static_cast<float> (std::clamp (params_.getAt (idxMix_), 0.0, 100.0) / 100.0);
    // Latence du module : le signal sec (mix, bypass) est retardé d'autant, pour que tout reste aligné et que la latence ne change pas au bypass.
    const int latency = ringL_.empty() ? 0 : std::min (module_->latencySamples (params_), static_cast<int> (ringL_.size()) - 2);
    if (latency != ringLatency_)
    {
        std::fill (ringL_.begin(), ringL_.end(), 0.0f);
        std::fill (ringR_.begin(), ringR_.end(), 0.0f);
        ringLatency_ = latency;
    }
    auto delayed = [this, n, latency] (const float* in, float* out, std::vector<float>& ring)
    {
        if (latency <= 0) { std::copy (in, in + n, out); return; }
        const std::size_t size = ring.size();
        for (int i = 0; i < n; ++i)
        {
            const std::size_t w = (ringPos_ + static_cast<std::size_t> (i)) % size;
            ring[w] = in[i];
            out[i] = ring[(w + size - static_cast<std::size_t> (latency)) % size];
        }
    };
    if (mixTarget == 0.0f && lastMix_ == 0.0f)
    {
        if (! idle_) { module_->reset(); idle_ = true; } // contourné et fondu terminé : plus de calcul, le signal passe tel quel (retardé de la latence)
        if (latency > 0)
        {
            delayed (left, dryL_.data(), ringL_);
            delayed (right, dryR_.data(), ringR_);
            ringPos_ = (ringPos_ + static_cast<std::size_t> (n)) % ringL_.size();
            std::copy (dryL_.begin(), dryL_.begin() + n, left);
            std::copy (dryR_.begin(), dryR_.begin() + n, right);
        }
        return;
    }
    idle_ = false;

    const auto un = static_cast<std::size_t> (n);
    delayed (left, dryL_.data(), ringL_);       // dryL_ / dryR_ : le signal d'entrée retardé de la latence du module
    delayed (right, dryR_.data(), ringR_);
    if (latency > 0) ringPos_ = (ringPos_ + un) % ringL_.size();

    const float inGain = mixer::dbToGain (std::clamp (params_.getAt (idxIn_), -24.0, 24.0), -1000.0);
    const float outGain = mixer::dbToGain (std::clamp (params_.getAt (idxOut_), -24.0, 24.0), -1000.0);
    const float inStep = (inGain - lastIn_) / static_cast<float> (n);
    float g = lastIn_;
    for (int i = 0; i < n; ++i, g += inStep) { left[i] *= g; right[i] *= g; }
    lastIn_ = inGain;

    if (module_->wantsKey()) module_->setKey (keyL_, keyR_);
    module_->process (left, right, n, params_);

    const float outStep = (outGain - lastOut_) / static_cast<float> (n);
    const float mixStep = (mixTarget - lastMix_) / static_cast<float> (n);
    float go = lastOut_, m = lastMix_;
    for (int i = 0; i < n; ++i, go += outStep, m += mixStep)
    {
        left[i] = dryL_[static_cast<std::size_t> (i)] * (1.0f - m) + left[i] * go * m;
        right[i] = dryR_[static_cast<std::size_t> (i)] * (1.0f - m) + right[i] * go * m;
    }
    (void) un;
    lastOut_ = outGain;
    lastMix_ = mixTarget;
}

// ---- registre ----
const std::vector<ModuleInfo>& availableModules()
{
    static const std::vector<ModuleInfo> modules = { { "utility", "module.utility" }, { "shaper", "module.shaper" }, { "compressor", "module.compressor" }, { "equalizer", "module.equalizer" }, { "delay", "module.delay" }, { "chorus", "module.chorus" } };
    return modules;
}

std::unique_ptr<StockModule> createModule (std::string_view typeId)
{
    if (typeId == "utility") return std::make_unique<UtilityModule>();
    if (typeId == "shaper") return std::make_unique<ShaperModule>();
    if (typeId == "compressor") return std::make_unique<CompressorModule>();
    if (typeId == "equalizer") return std::make_unique<EqualizerModule>();
    if (typeId == "delay") return std::make_unique<DelayModule>();
    if (typeId == "chorus") return std::make_unique<ChorusModule>();
    return nullptr;
}

std::shared_ptr<InsertInstance> createInsert (std::string_view typeId, double sampleRate, int maxBlockSize)
{
    auto module = createModule (typeId);
    if (module == nullptr) return nullptr;
    auto inst = std::make_shared<InsertInstance> (std::string (typeId), std::move (module));
    if (! inst->runner.ok()) return nullptr;
    if (sampleRate > 0.0) inst->runner.prepare (sampleRate, maxBlockSize);
    return inst;
}
} // namespace neptune::core
