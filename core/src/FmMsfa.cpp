// Moteur FM : compilation de msfa (third_party/msfa, Apache 2.0) en un seul fichier, dans l'espace de noms `msfa`, pour qu'aucun
// symbole global (macro N, modèles min / max, classes Env / Lfo / Sin...) n'apparaisse dans le reste du projet.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
 #include <libkern/OSAtomic.h>
#endif
#if defined(__aarch64__) || defined(__ARM_NEON__)
 #include <arm_neon.h>
#endif

#include <algorithm>
#include <mutex>
#include <vector>

#include "neptune/core/FmEngine.h"

#if defined(_MSC_VER)
 #pragma warning (push)
 #pragma warning (disable : 4244 4267 4305 4018 4100 4189 4456 4458 4701 4702)
#elif defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wunused-parameter"
 #pragma GCC diagnostic ignored "-Wunused-variable"
 #pragma GCC diagnostic ignored "-Wsign-compare"
 #pragma GCC diagnostic ignored "-Wparentheses"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wold-style-cast"
 #pragma GCC diagnostic ignored "-Wextra"
#endif

namespace msfa
{
#include "../../third_party/msfa/synth.h"
#include "../../third_party/msfa/aligned_buf.h"
#include "../../third_party/msfa/controllers.h"
#include "../../third_party/msfa/exp2.h"
#include "../../third_party/msfa/sin.h"
#include "../../third_party/msfa/freqlut.h"
#include "../../third_party/msfa/lfo.h"
#include "../../third_party/msfa/env.h"
#include "../../third_party/msfa/pitchenv.h"
#include "../../third_party/msfa/fm_op_kernel.h"
#include "../../third_party/msfa/fm_core.h"
#include "../../third_party/msfa/dx7note.h"
#include "../../third_party/msfa/exp2.cc"
#include "../../third_party/msfa/sin.cc"
#include "../../third_party/msfa/freqlut.cc"
#include "../../third_party/msfa/lfo.cc"
#include "../../third_party/msfa/env.cc"
#include "../../third_party/msfa/pitchenv.cc"
#include "../../third_party/msfa/fm_op_kernel.cc"
#include "../../third_party/msfa/fm_core.cc"
#include "../../third_party/msfa/dx7note.cc"
#include "../../third_party/msfa/patch.cc"
} // namespace msfa

#if defined(_MSC_VER)
 #pragma warning (pop)
#elif defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif

#undef N
#undef LG_N

namespace neptune::core::fm
{
namespace
{
std::mutex gInitMutex;
double gInitRate = 0.0;
} // namespace

void initTables (double sampleRate)
{
    if (sampleRate <= 0.0) sampleRate = 48000.0;
    std::lock_guard<std::mutex> lock (gInitMutex);
    if (gInitRate == sampleRate) return;
    gInitRate = sampleRate;
    msfa::Freqlut::init (sampleRate);
    msfa::Exp2::init();
    msfa::Tanh::init();
    msfa::Sin::init();
    msfa::Lfo::init (sampleRate);
    msfa::PitchEnv::init (sampleRate);
    msfa::Env::init_sr (sampleRate);
}

// ---------------------------------------------------------------------------------------------------- LFO

struct Lfo::Impl { msfa::Lfo lfo; };
Lfo::Lfo() : impl_ (std::make_unique<Impl>()) { Patch p = Patch::initVoice(); reset (p); }
Lfo::~Lfo() = default;

void Lfo::reset (const Patch& p)
{
    char params[6];
    for (int i = 0; i < 6; ++i) params[i] = static_cast<char> (p.data[static_cast<std::size_t> (kLfoOffset + i)]);
    // msfa : params[0..3] vitesse, délai, PMD, AMD ; params[4] sync ; params[5] forme. Le patch les range 137..142 dans cet ordre.
    impl_->lfo.reset (params);
}

void Lfo::keyDown() { impl_->lfo.keydown(); }
std::int32_t Lfo::sample() { return impl_->lfo.getsample(); }
std::int32_t Lfo::delay() { return impl_->lfo.getdelay(); }

// ---------------------------------------------------------------------------------------------------- Note

struct Note::Impl
{
    msfa::Dx7Note note;
    msfa::Controllers controllers {};
};

Note::Note() : impl_ (std::make_unique<Impl>()) {}
Note::~Note() = default;

void Note::start (const Patch& patch, int midiNote, int velocity)
{
    char raw[kPatchBytes];
    for (int i = 0; i < kPatchBytes; ++i) raw[i] = static_cast<char> (patch.data[static_cast<std::size_t> (i)]);
    impl_->note.init (raw, std::clamp (midiNote, 0, 127), std::clamp (velocity, 0, 127));
}

void Note::keyUp() { impl_->note.keyup(); }
void Note::setDetune (std::int32_t q) { impl_->note.set_detune (q); }
void Note::setGlide (std::int32_t q) { impl_->note.set_glide (q); }
void Note::setLevelOffset (int op, std::int32_t q) { if (op >= 0 && op < 6) impl_->note.set_level_offset (op, q); }
void Note::setFeedback (int fb) { impl_->note.set_feedback (std::clamp (fb, 0, 7)); }

void Note::compute (std::int32_t* buf, std::int32_t lfoValue, std::int32_t lfoDelay, const Controls& c)
{
    auto& ctl = impl_->controllers;
    ctl.values_[msfa::kControllerPitch] = std::clamp (c.pitchBend, 0, 16383);
    ctl.pitchbend_range_ = std::clamp (c.pitchBendRange, 0, 12);
    ctl.wheel_pitch_depth_ = std::clamp (c.wheelPitchDepth, 0, 255);
    ctl.wheel_amp_depth_ = std::clamp (c.wheelAmpDepth, 0, 99);
    impl_->note.compute (buf, lfoValue, lfoDelay, &ctl);
}

// ---------------------------------------------------------------------------------------------------- algorithmes

namespace
{
std::array<AlgorithmGraph, kNumAlgorithms> buildGraphs()
{
    std::array<AlgorithmGraph, kNumAlgorithms> graphs;
    for (int a = 0; a < kNumAlgorithms; ++a)
    {
        auto& g = graphs[static_cast<std::size_t> (a)];
        // bus 1 et 2 : opérateurs qui y écrivent actuellement (un opérateur sans « ADD » remplace le contenu du bus)
        std::vector<int> writers[3];
        int fbOut = -1, fbIn = -1;
        for (int op = 0; op < 6; ++op)
        {
            const int flags = msfa::algorithms[a].ops[op];
            const int inbus = (flags >> 4) & 3, outbus = flags & 3;
            const bool add = (flags & msfa::OUT_BUS_ADD) != 0;
            if (inbus != 0)
                for (const int w : writers[inbus]) g.edges.push_back ({ w, op });
            if ((flags & msfa::FB_IN) != 0) fbIn = op;
            if ((flags & msfa::FB_OUT) != 0) fbOut = op;
            if (outbus == 0) g.carrierMask = static_cast<std::uint8_t> (g.carrierMask | (1 << op));
            else
            {
                if (! add) writers[outbus].clear();
                writers[outbus].push_back (op);
            }
        }
        if (fbOut >= 0 && fbIn >= 0) { g.feedbackFrom = fbOut; g.feedbackTo = fbIn; }
    }
    return graphs;
}
} // namespace

int keyboardLevelScaling (const Patch& p, int op, int midiNote)
{
    if (op < 0 || op >= kOperators) return 0;
    return msfa::ScaleLevel (std::clamp (midiNote, 0, 127), p.op (op, kBreakPoint), p.op (op, kLeftDepth), p.op (op, kRightDepth),
                             p.op (op, kLeftCurve), p.op (op, kRightCurve));
}

const AlgorithmGraph& algorithmGraph (int algorithm)
{
    static const auto graphs = buildGraphs();
    return graphs[static_cast<std::size_t> (std::clamp (algorithm, 0, kNumAlgorithms - 1))];
}
} // namespace neptune::core::fm
