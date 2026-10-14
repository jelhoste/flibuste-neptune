#include "neptune/core/StockModule.h"

#include <algorithm>
#include <cmath>

#include "neptune/core/Mixer.h"
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
       && params_.add (ParamSpec::continuous (stock::mix, "Mix", Unit::Percent, 0.0, 100.0, 100.0), &error)
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
    if (mixTarget == 0.0f && lastMix_ == 0.0f)
    {
        if (! idle_) { module_->reset(); idle_ = true; } // contourné et fondu terminé : plus de calcul, le signal passe tel quel
        return;
    }
    idle_ = false;

    const auto un = static_cast<std::size_t> (n);
    std::copy (left, left + n, dryL_.begin());
    std::copy (right, right + n, dryR_.begin());

    const float inGain = mixer::dbToGain (std::clamp (params_.getAt (idxIn_), -24.0, 24.0), -1000.0);
    const float outGain = mixer::dbToGain (std::clamp (params_.getAt (idxOut_), -24.0, 24.0), -1000.0);
    const float inStep = (inGain - lastIn_) / static_cast<float> (n);
    float g = lastIn_;
    for (int i = 0; i < n; ++i, g += inStep) { left[i] *= g; right[i] *= g; }
    lastIn_ = inGain;

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
    static const std::vector<ModuleInfo> modules = { { "utility", "module.utility" } };
    return modules;
}

std::unique_ptr<StockModule> createModule (std::string_view typeId)
{
    if (typeId == "utility") return std::make_unique<UtilityModule>();
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
