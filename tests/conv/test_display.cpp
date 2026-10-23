#include "neptune/core/conv/ir_display.hpp"
#include <chrono>
#include <cstdio>
#include <random>
using namespace llc;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
typedef std::vector<float> V;
static IRBuffer buf(std::vector<V> ch, double sr = 48000) { IRBuffer b; b.sampleRate = sr; b.ch = std::move(ch); return b; }
static float at(const IRDisplay& d, int t, double f) { const double x = std::log(f / d.fMin) / std::log(d.fMax / d.fMin) * double(d.spectrumDb[t].size() - 1); return d.spectrumDb[t][size_t(std::lround(x))]; }


static void testEditorDisplay() {
    std::printf("[display] editor axis and B / E markers\n");
    std::mt19937 rng(5); std::uniform_real_distribution<float> u(-1, 1);
    V h(96000); for (size_t i = 0; i < h.size(); ++i) h[i] = u(rng) * std::exp(-4.0f * float(i) / 96000.0f); h[0] = 1.0f;
    V z(100, 0.f); z.insert(z.end(), h.begin(), h.end());                 // 100 samples of silence, then 2 s of IR
    IRBuffer raw; raw.sampleRate = 48000; raw.ch = {z};
    auto run = [&](IROptions o) { return computeIREditorDisplay(raw, o, InputMode::Sum); };
    IROptions o;                                                         // defaults: auto trim on, Energy normalisation
    auto e = run(o);
    CHECK(e.valid && std::fabs(e.axisMs - 2000.0) < 0.1, "axis %g ms (want 2000: the leading silence is not part of it)", e.axisMs);
    CHECK(std::fabs(e.originMs - 100.0 * 1000.0 / 48000.0) < 0.01, "origin %g", e.originMs);
    CHECK(e.keptStartMs == 0.0 && std::fabs(e.keptEndMs - e.axisMs) < 1e-6 && e.endAtAxisEnd, "End OFF: E marker at the end (%g / %g)", e.keptEndMs, e.axisMs);
    CHECK(std::fabs(e.kept.durationMs - 2000.0) < 0.1, "kept length %g", e.kept.durationMs);
    CHECK(std::fabs(e.base.peak - e.kept.peak) < 1e-4, "base and kept share one scale (%g vs %g)", e.base.peak, e.kept.peak);

    o = IROptions(); o.endMs = 1500; e = run(o);
    CHECK(std::fabs(e.axisMs - 2000.0) < 0.1 && std::fabs(e.keptEndMs - 1500.0) < 0.1 && !e.endAtAxisEnd, "End = 1500 ms: E at %g, axis %g (the axis must not shrink)", e.keptEndMs, e.axisMs);
    CHECK(std::fabs(e.kept.durationMs - 1500.0) < 0.1, "kept length %g", e.kept.durationMs);
    o.endMs = 5000; e = run(o);
    CHECK(e.endAtAxisEnd && std::fabs(e.keptEndMs - e.axisMs) < 1e-6, "End beyond the IR: marker clamps to the end (%g)", e.keptEndMs);
    o = IROptions(); o.beginMs = 200; o.endMs = 1500; e = run(o);
    CHECK(std::fabs(e.keptStartMs - 200.0) < 0.1 && std::fabs(e.keptEndMs - 1500.0) < 0.1 && std::fabs(e.kept.durationMs - 1300.0) < 0.1, "Begin 200 / End 1500: %g .. %g, kept %g", e.keptStartMs, e.keptEndMs, e.kept.durationMs);
    CHECK(std::fabs((e.keptEndMs - e.keptStartMs) - e.kept.durationMs) < 0.1, "markers span = kept duration");
    o = IROptions(); o.autoLength = true; o.lengthDb = -30; e = run(o);
    CHECK(e.keptEndMs < e.axisMs - 100.0 && e.keptEndMs > 100.0 && !e.endAtAxisEnd && std::fabs((e.keptEndMs - e.keptStartMs) - e.kept.durationMs) < 0.1, "auto length moves E to %g (axis %g)", e.keptEndMs, e.axisMs);
    o = IROptions(); o.reverse = true; o.endMs = 1500; e = run(o);
    CHECK(std::fabs(e.keptEndMs - 1500.0) < 0.1 && e.keptStartMs == 0.0, "Reverse does not move the markers");
    size_t baseMax = 0, keptMax = 0; for (size_t i = 0; i < e.base.waveMax[0].size(); ++i) { if (e.base.waveMax[0][i] > e.base.waveMax[0][baseMax]) baseMax = i; if (e.kept.waveMax[0][i] > e.kept.waveMax[0][keptMax]) keptMax = i; }
    CHECK(baseMax < 40 && keptMax > e.kept.waveMax[0].size() - 40, "Reverse: the base keeps the original orientation (peak at %zu), the kept IR is mirrored (peak at %zu)", baseMax, keptMax);
    o = IROptions(); o.stretch = 2.0; e = run(o);
    CHECK(std::fabs(e.axisMs - 4000.0) < 1.5 && e.endAtAxisEnd, "stretch 2: axis %g (resampling pre-ringing moves the automatic trim by up to ~1 ms)", e.axisMs);
    o = IROptions(); o.stretch = 0.5; e = run(o);
    CHECK(std::fabs(e.axisMs - 1000.0) < 1.5 && e.endAtAxisEnd, "stretch 0.5: axis %g", e.axisMs);
    IRBuffer raw44; raw44.sampleRate = 44100; raw44.ch = {V(88200, 0.2f)}; raw44.ch[0][0] = 1.0f;     // 2 s at 44.1 kHz, played at 48 kHz
    o = IROptions(); auto e44 = computeIREditorDisplay(raw44, o, InputMode::Sum);
    CHECK(e44.valid && std::fabs(e44.axisMs - 2000.0) < 0.1 && e44.endAtAxisEnd, "44.1 kHz source: axis %g ms", e44.axisMs);
    o = IROptions(); o.attackMs = 100; o.decayDb = 30; e = run(o);
    CHECK(e.kept.peak <= e.base.peak + 1e-4, "envelope can only lower the kept IR");
    IRBuffer silent; silent.ch = {V(100, 0.f)}; CHECK(!computeIREditorDisplay(silent, IROptions(), InputMode::Sum).valid, "silent IR -> invalid");
}

int main() {
    testEditorDisplay();
    std::printf("[display] wave and spectrum data for the UI\n");
    { V h(4800, 0.f); h[0] = 1.0f; auto d = computeIRDisplay(buf({h}), InputMode::Sum);
      CHECK(d.valid && d.traces == 1 && d.waveMax[0].size() == 800 && d.spectrumDb[0].size() == 256, "shape");
      CHECK(d.waveMax[0][0] == 1.0f && d.waveMax[0][1] == 0.0f && d.waveMin[0][0] == 0.0f, "delta wave columns");
      CHECK(std::fabs(d.durationMs - 100.0) < 1e-9 && d.lengthSamples == 4800 && d.peak == 1.0f, "duration %g", d.durationMs);
      double worst = 0; for (float v : d.spectrumDb[0]) worst = std::max(worst, std::fabs(double(v))); CHECK(worst < 0.1, "delta spectrum must be flat 0 dB (worst %g)", worst);
      CHECK(d.waveMax[1] == d.waveMax[0], "mono IR: both traces identical"); }
    { V h = {0.5f, 0.5f}; auto d = computeIRDisplay(buf({h}), InputMode::Sum);          // |H| = |cos(pi f / fs)|
      for (double f : {20.0, 1000.0, 5000.0, 12000.0, 20000.0}) { const double want = 20 * std::log10(std::fabs(std::cos(M_PI * f / 48000.0))); CHECK(std::fabs(at(d, 0, f) - want) < 0.35, "FIR lowpass @%g Hz: want %.2f got %.2f", f, want, at(d, 0, f)); } }
    { V a(3000, 0.f), b(3000, 0.f), c(3000, 0.f), e(3000, 0.f); a[10] = 1; b[20] = 0.5f; c[30] = 0.25f; e[40] = 0.125f;   // 4-channel IR: where is each impulse drawn ?
      auto col = [&](const IRDisplay& d, int t) { for (size_t i = 0; i < d.waveMax[t].size(); ++i) if (d.waveMax[t][i] != 0.f) return d.waveMax[t][i]; return 0.f; };
      IRBuffer ir = buf({a, b, c, e});
      auto s = computeIRDisplay(ir, InputMode::Stereo), m = computeIRDisplay(ir, InputMode::Sum), l = computeIRDisplay(ir, InputMode::Left), r = computeIRDisplay(ir, InputMode::Right);
      CHECK(s.traces == 2 && col(s, 0) == 1.0f && col(s, 1) == 0.125f, "true stereo / Stereo input: L = LL, R = RR");
      CHECK(l.waveMax[0].size() == 800 && col(l, 0) == 1.0f && col(l, 1) == 0.5f, "Left input: L = h0, R = h1");
      CHECK(col(r, 0) == 0.25f && col(r, 1) == 0.125f, "Right input: L = h2, R = h3");
      float m0 = 0, m1 = 0; for (float v : m.waveMax[0]) m0 = std::max(m0, v); for (float v : m.waveMax[1]) m1 = std::max(m1, v);
      CHECK(m0 == 1.0f && m1 == 0.5f, "Sum input: L = h0+h2 (impulses at different times), R = h1+h3 (%g, %g)", m0, m1); }
    { auto d = computeIRDisplay(buf({V(10, 0.3f)}), InputMode::Sum, 800, 256); CHECK(d.valid && d.waveMax[0].size() == 800, "10-sample IR on 800 columns"); }
    { auto d = computeIRDisplay(IRBuffer{}, InputMode::Sum); CHECK(!d.valid, "empty IR"); }
    { IRDisplayWorker w; uint32_t seen = 0; IRDisplay d;
      V h(2000, 0.f); h[5] = 1.0f;
      w.request([h](IRBuffer& ir, InputMode& m) { std::this_thread::sleep_for(std::chrono::milliseconds(150)); ir.sampleRate = 48000; ir.ch = {V(10, 0.1f)}; m = InputMode::Sum; return true; });
      w.request([h](IRBuffer& ir, InputMode& m) { ir.sampleRate = 48000; ir.ch = {h}; m = InputMode::Sum; return true; });          // replaces the first one
      bool got = false; for (int i = 0; i < 200 && !got; ++i) { got = w.fetch(d, seen); if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
      CHECK(got && d.valid && d.lengthSamples == 2000 && d.version == 1, "worker result: got=%d len=%zu version=%u", int(got), d.lengthSamples, d.version);
      CHECK(!w.fetch(d, d.version), "no new data -> fetch() must return false");
      w.request([](IRBuffer&, InputMode&) { return false; }); std::this_thread::sleep_for(std::chrono::milliseconds(100));
      IRDisplay e; CHECK(w.fetch(e, d.version) && !e.valid, "failed job -> invalid display with a new version"); }
    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL DISPLAY TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
