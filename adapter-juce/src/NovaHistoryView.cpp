#include "NovaHistoryView.h"

#include <algorithm>
#include <cmath>

using namespace neptune::core;
using neptune::core::theme::Token;
namespace cm = neptune::core::compressor;

namespace nova
{
namespace
{
constexpr int kMaxPoints = 300, kPausedThreshold = 6, kTooMuchResetThreshold = 64;
constexpr float kMissingDb = -10000.0f;
// Retard d'affichage : l'hôte livre l'audio par rafales ; garder en réserve quelques dizaines de millisecondes de données évite les trous (donc les saccades)
// sans qu'on puisse le voir : seul le dessin de la compression compte, pas sa précision temporelle.
constexpr double kDisplayDelayMs = 50.0;
constexpr int kPointsPerSecond[4] = { 40, 30, 20, 15 };
constexpr double kTimeLength[4] = { 6.0, 9.0, 12.0, 18.0 };
constexpr float kMaxDbValues[5] = { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f };
constexpr float kMinDbValues[6] = { -9.0f, -18.0f, -36.0f, -54.0f, -72.0f, -120.0f };

// Nombre de points par seconde : le plus proche multiple de la valeur préférée qui divise la fréquence d'échantillonnage (comme ZL)
int choosePointsPerSecond (int sampleRate, int preferred, int timeLength)
{
    if (sampleRate <= 0 || preferred <= 0 || timeLength <= 0) return preferred;
    const int maxPps = kMaxPoints / timeLength;
    for (int candidate = preferred; candidate <= maxPps; ++candidate)
        if (sampleRate % candidate == 0) return candidate;
    return preferred;
}

// ------------------------------------------------------------------------------------------------ fenêtre de réglages
class SettingsPanel : public juce::Component
{
public:
    SettingsPanel (const Skin& s, ParamHost& host, const std::function<juce::String (const char*)>& tx) : skin_ (s), title_ (tx ("nv.an_title"))
    {
        struct C { ParamId id; const char* label; std::vector<juce::String> choices; };
        const C choices[6] = { { cm::anTime, "nv.an_time", { "6 s", "9 s", "12 s", "18 s" } },
                               { cm::anMove, "nv.an_scroll", { "Sync", "Slow", "Roll" } },
                               { cm::anMaxDb, "nv.an_max", { "6 dB", "0 dB", "-6 dB", "-12 dB", "-24 dB" } },
                               { cm::anMinDb, "nv.an_min", { "-9 dB", "-18 dB", "-36 dB", "-54 dB", "-72 dB", "-120 dB" } },
                               { cm::anMag, "nv.an_mag", { "Peak", "RMS" } },
                               { cm::anStereo, "nv.an_ch", { "M+S", "L+R", "Left", "Right", "Mid", "Side" } } };
        for (const auto& c : choices)
        {
            auto p = std::make_unique<Pill> (skin_);
            p->bindChoice (host, c.id, tx (c.label), c.choices);
            addAndMakeVisible (*p);
            pills_.push_back (std::move (p));
        }
        struct T { ParamId id; const char* label; };
        for (const auto& t : { T { cm::anShowIn, "nv.an_in" }, T { cm::anShowOut, "nv.an_out" }, T { cm::anShowRed, "nv.an_red" }, T { cm::anShowKey, "nv.an_key" } })
        {
            auto p = std::make_unique<Pill> (skin_);
            p->bindToggle (host, t.id, tx (t.label));
            addAndMakeVisible (*p);
            pills_.push_back (std::move (p));
        }
        setSize (14 * 2 + 160 * 2 + 8, 14 + 22 + 5 * 38 + 6);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (skin_.colour (Token::BgCard));
        g.setColour (skin_.colour (Token::Accent));
        g.setFont (skin_.font (12.0f, true));
        g.drawText (title_, 14, 10, getWidth() - 28, 18, juce::Justification::centredLeft);
    }
    void resized() override
    {
        for (std::size_t i = 0; i < pills_.size(); ++i)
        {
            const int col = static_cast<int> (i % 2), row = static_cast<int> (i / 2);
            pills_[i]->setBounds (14 + col * 168, 14 + 22 + row * 38, 160, 30);
        }
    }

private:
    Skin skin_;   // copie : la fenêtre peut survivre à l'éditeur qui l'a ouverte
    juce::String title_;
    std::vector<std::unique_ptr<Pill>> pills_;
};
} // namespace

// ------------------------------------------------------------------------------------------------ vue
CompressorHistoryView::CompressorHistoryView (const Skin& s, ParamHost& h, const CompressorModule* m)
    : skin_ (s), host_ (h), module_ (m), vblank_ (this, [this] { step (juce::Time::getMillisecondCounterHiRes() * 0.001); })
{
    if (module_ != nullptr) module_->analyzer().setActive (true);
    readSettings();
}

CompressorHistoryView::~CompressorHistoryView()
{
    if (module_ != nullptr) module_->analyzer().setActive (false);
}

void CompressorHistoryView::showSettings (juce::Component* anchor, const Skin& skin, ParamHost& host, const std::function<juce::String (const char*)>& tx)
{
    auto* top = anchor->getTopLevelComponent();
    if (top == nullptr) return;
    juce::CallOutBox::launchAsynchronously (std::make_unique<SettingsPanel> (skin, host, tx), top->getLocalArea (anchor, anchor->getLocalBounds()), top);
}

void CompressorHistoryView::readSettings()
{
    const auto& p = host_.parameters();
    auto idx = [&p] (ParamId id, int hi) { return juce::jlimit (0, hi, static_cast<int> (std::lround (p.get (id)))); };
    const int oldTime = timeIdx_, oldMove = moveType_;
    timeIdx_ = idx (cm::anTime, 3);
    moveType_ = idx (cm::anMove, 2);
    maxDb_ = kMaxDbValues[idx (cm::anMaxDb, 4)];
    minDb_ = kMinDbValues[idx (cm::anMinDb, 5)];
    mag_ = idx (cm::anMag, 1) == 1 ? MagType::RMS : MagType::Peak;
    stereo_ = static_cast<StereoType> (idx (cm::anStereo, 5));
    showIn_ = p.get (cm::anShowIn) >= 0.5;
    showOut_ = p.get (cm::anShowOut) >= 0.5;
    showRed_ = p.get (cm::anShowRed) >= 0.5;
    showKey_ = p.get (cm::anShowKey) >= 0.5;
    const int dir = idx (cm::direction, 3);
    centreReduction_ = dir == 1 || dir == 3;   // gonflement et shape : la réduction part du milieu
    if (oldTime != timeIdx_ || oldMove != moveType_) sampleRate_ = 0.0;   // reconfiguration au prochain pas
}

juce::Rectangle<float> CompressorHistoryView::plotArea() const
{
    const auto b = getLocalBounds().toFloat();
    return { b.getX() + skin_.px (6.0f), b.getY() + skin_.px (8.0f), b.getWidth() - skin_.px (6.0f + 40.0f), b.getHeight() - skin_.px (16.0f) };
}

juce::Rectangle<float> CompressorHistoryView::squareArea() const
{
    const auto p = plotArea();
    const float r = std::min (p.getWidth(), p.getHeight());
    return { p.getRight() - r, p.getY(), r, r };   // à droite : c'est là que le signal entre, donc là qu'on voit l'action de la courbe
}

float CompressorHistoryView::yOf (float db) const noexcept
{
    const auto p = plotArea();
    const float range = minDb_ - maxDb_;
    return p.getY() + (db - maxDb_) / range * p.getHeight();
}
float CompressorHistoryView::dbOfY (float y) const noexcept
{
    const auto p = plotArea();
    return maxDb_ + (y - p.getY()) / p.getHeight() * (minDb_ - maxDb_);
}
float CompressorHistoryView::xOfIn (float db) const
{
    const auto s = squareArea();
    return s.getX() + (db - minDb_) / (maxDb_ - minDb_) * s.getWidth();
}
float CompressorHistoryView::dbOfX (float x) const
{
    const auto s = squareArea();
    return minDb_ + (x - s.getX()) / s.getWidth() * (maxDb_ - minDb_);
}

juce::Point<float> CompressorHistoryView::handle() const
{
    const double thr = std::clamp (host_.parameters().get (cm::threshold), static_cast<double> (minDb_), static_cast<double> (maxDb_));
    const double out = module_ != nullptr ? module_->curveOutputDb (host_.parameters(), thr) : thr;
    return { xOfIn (static_cast<float> (thr)), yOf (static_cast<float> (std::clamp (out, static_cast<double> (minDb_), static_cast<double> (maxDb_)))) };
}

bool CompressorHistoryView::nearHandle (juce::Point<float> p) const { return handle().getDistanceFrom (p) < skin_.px (14.0f); }

void CompressorHistoryView::resetHistory()
{
    first_ = true;
    missing_ = 0;
    rollNext_ = 0;
    std::fill (preYs_.begin(), preYs_.end(), 0.0f);
    mappingInit_ = false;
}

// ---- correspondance dB -> pixel : à chaque changement d'échelle ou de taille, les ordonnées déjà calculées sont recalées (comme ZL)
void CompressorHistoryView::updateYMapping()
{
    const auto bound = plotArea();
    const float range = minDb_ - maxDb_;
    const float height = std::max (bound.getHeight(), 1.0f);
    const float nextScale = std::fabs (range) > 1.0e-6f ? height / range : 0.0f;
    const float nextBias = std::fma (-maxDb_, nextScale, bound.getY());
    const float nextRedBias = centreReduction_ ? bound.getCentreY() : bound.getY();
    if (mappingInit_ && ! first_)
    {
        const bool scaleChanged = std::fabs (nextScale - scale_) > 1.0e-6f, biasChanged = std::fabs (nextBias - bias_) > 1.0e-6f, redChanged = std::fabs (nextRedBias - redBias_) > 1.0e-6f;
        if ((scaleChanged || biasChanged || redChanged) && std::fabs (scale_) > 1.0e-9f)
        {
            const float k = nextScale / scale_;
            const float magBias = std::fma (-bias_, k, nextBias);
            const float rBias = std::fma (-redBias_, k, nextRedBias);
            if (scaleChanged || biasChanged)
                for (auto* v : { &preYs_, &outYs_, &keyYs_ })
                    for (auto& y : *v) y = std::fma (y, k, magBias);
            if (scaleChanged || redChanged)
                for (auto& y : redYs_) y = std::fma (y, k, rBias);
        }
    }
    scale_ = nextScale;
    bias_ = nextBias;
    redBias_ = nextRedBias;
    mappingInit_ = true;
}

void CompressorHistoryView::updateXs (double now)
{
    const auto bound = plotArea();
    const std::size_t n = xs_.size();
    if (moveType_ == 2)   // Roll : les points restent en place, la tête avance
    {
        const double dx = static_cast<double> (bound.getWidth()) / static_cast<double> (n - 1);
        for (std::size_t i = 0; i < n; ++i) xs_[i] = bound.getX() + static_cast<float> (static_cast<double> ((rollNext_ + i) % n) * dx);
        return;
    }
    const double scale = static_cast<double>(bound.getWidth()) / timeLength_;
    const double dx = secondPerPoint_ * scale;
    double x0 = static_cast<double> (bound.getX()) - (now - startTime_) * scale;
    if (moveType_ == 1)   // Slow : la tête avance lentement puis revient vite
    {
        constexpr double fastLength = 0.5, restart = 0.25, sweep = 1.0 - restart;
        const double slowLength = timeLength_ - fastLength;
        const double phase = std::fmod (std::max (0.0, now - motionStart_), timeLength_);
        const double head = phase < slowLength ? restart + sweep * phase / slowLength : 1.0 - sweep * (phase - slowLength) / fastLength;
        x0 += head * static_cast<double> (bound.getWidth()) - static_cast<double> (n - 1) * dx;
    }
    for (std::size_t i = 0; i < n; ++i, x0 += dx) xs_[i] = static_cast<float> (x0);
}

void CompressorHistoryView::rebuildPaths()
{
    const auto bound = plotArea();
    inPath_.clear(); outPath_.clear(); keyPath_.clear(); redPath_.clear();
    const std::size_t n = preYs_.size();
    if (n < 2) return;
    inPath_.startNewSubPath (xs_[0], bound.getBottom());
    inPath_.lineTo (xs_[0], preYs_[0]);
    outPath_.startNewSubPath (xs_[0], outYs_[0]);
    keyPath_.startNewSubPath (xs_[0], keyYs_[0]);
    redPath_.startNewSubPath (xs_[0], redYs_[0]);
    for (std::size_t i = 1; i < n; ++i)
    {
        if (xs_[i] < xs_[i - 1])   // Roll : la tête recommence à gauche
        {
            inPath_.lineTo (xs_[i - 1], bound.getBottom());
            inPath_.closeSubPath();
            inPath_.startNewSubPath (xs_[i], bound.getBottom());
            inPath_.lineTo (xs_[i], preYs_[i]);
            outPath_.startNewSubPath (xs_[i], outYs_[i]);
            keyPath_.startNewSubPath (xs_[i], keyYs_[i]);
            redPath_.startNewSubPath (xs_[i], redYs_[i]);
            continue;
        }
        inPath_.lineTo (xs_[i], preYs_[i]);
        outPath_.lineTo (xs_[i], outYs_[i]);
        keyPath_.lineTo (xs_[i], keyYs_[i]);
        redPath_.lineTo (xs_[i], redYs_[i]);
    }
    inPath_.lineTo (xs_[n - 1], bound.getBottom());
}

void CompressorHistoryView::step (double now)
{
    if (module_ == nullptr || ! isVisible()) return;
    readSettings();
    auto& ring = module_->analyzer();
    const double sr = ring.sampleRate();
    const int maxBlock = ring.maxBlock();
    if (sampleRate_ <= 0.0 || std::fabs (sampleRate_ - sr) > 0.1 || maxBlock_ != maxBlock)
    {
        sampleRate_ = sr;
        maxBlock_ = maxBlock;
        timeLength_ = kTimeLength[timeIdx_];
        const int rounded = static_cast<int> (std::lround (sr));
        pointsPerSecond_ = choosePointsPerSecond (rounded, kPointsPerSecond[timeIdx_], static_cast<int> (std::lround (timeLength_)));
        samplesPerPoint_ = std::max (1, rounded / pointsPerSecond_);
        numPoints_ = static_cast<std::size_t> (pointsPerSecond_) * static_cast<std::size_t> (timeLength_);
        secondPerPoint_ = timeLength_ / static_cast<double> (numPoints_);
        const std::size_t size = moveType_ == 2 ? numPoints_ : numPoints_ + 2;
        for (auto* v : { &xs_, &preYs_, &outYs_, &keyYs_, &redYs_ }) v->assign (size, 0.0f);
        first_ = true;
        missing_ = 0;
        rollNext_ = 0;
        mappingInit_ = false;
        readPos_ = ring.writePos();
    }
    updateYMapping();
    const float missingY = std::fma (kMissingDb, scale_, bias_);
    const std::uint64_t w = ring.writePos();
    if (readPos_ > w) readPos_ = w;
    std::int64_t ready = static_cast<std::int64_t> (w - readPos_);
    if (ready > static_cast<std::int64_t> (AnalyzerRing::kCapacity / 2))   // lecteur trop en retard : on saute pour ne jamais lire de données écrasées
    {
        readPos_ = w - static_cast<std::uint64_t> (samplesPerPoint_) * 2;
        ready = samplesPerPoint_ * 2;
    }
    if (! first_ && now - startTime_ > 2.0 * timeLength_)   // fenêtre cachée longtemps : on repart de zéro
    {
        first_ = true;
        readPos_ = w;
        ready = 0;
    }
    auto push = [] (std::vector<float>& v, float y) { std::rotate (v.begin(), v.begin() + 1, v.end()); v.back() = y; };
    if (! first_)
    {
        while (now - startTime_ > secondPerPoint_)
        {
            if (ready >= samplesPerPoint_)
            {
                const int n = samplesPerPoint_;
                preDb_ = analyzerLevelDb (ring, cm::kAnPre, readPos_, n, mag_, stereo_);
                outDb_ = analyzerLevelDb (ring, cm::kAnPost, readPos_, n, mag_, stereo_);
                keyDb_ = analyzerLevelDb (ring, cm::kAnKey, readPos_, n, mag_, stereo_);
                reductionDb_ = analyzerLevelDb (ring, cm::kAnCompressed, readPos_, n, mag_, stereo_) - preDb_;
                readPos_ += static_cast<std::uint64_t> (n);
                ready -= n;
                missing_ = 0;
            }
            else if (missing_ < kPausedThreshold) ++missing_;
            else if (missing_ == kPausedThreshold)
            {
                const auto shift = static_cast<std::ptrdiff_t> (preYs_.size()) - kPausedThreshold;
                std::fill (preYs_.begin() + shift, preYs_.end(), missingY);
                std::fill (outYs_.begin() + shift, outYs_.end(), missingY);
                std::fill (keyYs_.begin() + shift, keyYs_.end(), missingY);
                std::fill (redYs_.begin() + shift, redYs_.end(), redBias_);
            }
            const bool tooMany = missing_ >= kPausedThreshold;
            push (preYs_, tooMany ? missingY : std::fma (preDb_, scale_, bias_));
            push (outYs_, tooMany ? missingY : std::fma (outDb_, scale_, bias_));
            push (keyYs_, tooMany ? missingY : std::fma (keyDb_, scale_, bias_));
            push (redYs_, tooMany ? redBias_ : std::fma (reductionDb_, scale_, redBias_));
            if (moveType_ == 2) rollNext_ = (rollNext_ + 1) % xs_.size();
            startTime_ += secondPerPoint_;
        }
        const std::int64_t threshold = 2 * static_cast<std::int64_t> (std::max (maxBlock_, samplesPerPoint_)) + static_cast<std::int64_t> (sampleRate_ * kDisplayDelayMs * 0.001);
        if (ready > threshold)   // trop de données accumulées (hôte en rafales) : on en jette une partie, comme ZL
        {
            tooMuch_ += static_cast<int> ((ready - threshold) / samplesPerPoint_);
            if (tooMuch_ > kTooMuchResetThreshold) { readPos_ += static_cast<std::uint64_t> (ready - threshold); tooMuch_ = 0; }
        }
        else tooMuch_ = 0;
    }
    else if (ready >= samplesPerPoint_ + static_cast<std::int64_t> (sampleRate_ * kDisplayDelayMs * 0.001))
    {
        first_ = false;
        startTime_ = now;
        motionStart_ = now;
        std::fill (preYs_.begin(), preYs_.end(), missingY);
        std::fill (outYs_.begin(), outYs_.end(), missingY);
        std::fill (keyYs_.begin(), keyYs_.end(), missingY);
        std::fill (redYs_.begin(), redYs_.end(), redBias_);
    }

    if (! first_)
    {
        updateXs (now);
        rebuildPaths();
    }
    // courbe de transfert : reconstruite seulement si un réglage ou l'échelle a changé
    std::uint32_t key = static_cast<std::uint32_t> (timeIdx_ * 7 + juce::roundToInt (maxDb_) * 13 + juce::roundToInt (minDb_) * 17 + getWidth() * 31 + getHeight());
    for (const auto id : { cm::direction, cm::threshold, cm::ratio, cm::kneeWidth, cm::curve, cm::floorDb, cm::style }) key = key * 31u + host_.parameters().version (id);
    if (key != curveKey_) { curveKey_ = key; curveValid_ = false; }
    // pas de repeinture inutile quand tout est vide et immobile
    const bool idle = first_ ? curveValid_ : missing_ > static_cast<int> (numPoints_) + 10 && curveValid_;
    if (! idle) repaint (getLocalBounds());
    if (missing_ <= static_cast<int> (numPoints_) + 10 && ! first_) ++activeFrames_;
}

void CompressorHistoryView::rebuildCurve()
{
    curve_.clear();
    if (module_ == nullptr) return;
    const auto s = squareArea();
    const int n = 200;
    const double db0 = minDb_, db1 = maxDb_;
    // un point hors du carré, à gauche, prolonge la diagonale sous la plage (comme ZL)
    {
        const double dbIn = db0 - (db1 - db0);
        curve_.startNewSubPath (xOfIn (static_cast<float> (dbIn)), yOf (static_cast<float> (module_->curveOutputDb (host_.parameters(), dbIn))));
    }
    for (int i = 0; i < n; ++i)
    {
        const double dbIn = db0 + (db1 - db0) * i / (n - 1);
        curve_.lineTo (xOfIn (static_cast<float> (dbIn)), yOf (static_cast<float> (module_->curveOutputDb (host_.parameters(), dbIn))));
    }
    (void) s;
}

// Couches fixes mises en cache (fond et grille ; dégradé, diagonale, seuil et courbe de transfert) : elles ne changent qu'avec la taille, l'échelle ou les
// réglages de la courbe, alors que l'historique est redessiné à chaque image. Les traits en pointillés en particulier coûtent cher à reconstruire.
void CompressorHistoryView::renderBackground (juce::Graphics& g) const
{
    const auto b = getLocalBounds().toFloat();
    skin_.fill (g, Token::StageBg, b, skin_.px (6.0f));
    const auto plot = plotArea();
    g.setFont (skin_.font (9.0f));
    for (int k = 0; k <= 6; ++k)
    {
        const float y = plot.getY() + plot.getHeight() * static_cast<float> (k) / 6.0f;
        g.setColour (skin_.colour (Token::Grid, 0.55f));
        g.fillRect (plot.getX(), y - skin_.px (0.5f), plot.getWidth(), std::max (1.0f, skin_.px (1.0f)));
        const float db = maxDb_ + (minDb_ - maxDb_) * static_cast<float> (k) / 6.0f;
        if (k > 0 && std::fabs (db - std::round (db)) < 0.01f)
        {
            g.setColour (skin_.colour (Token::TextDim2));
            g.drawText (juce::String (static_cast<int> (std::round (db))), juce::Rectangle<float> (plot.getRight() + skin_.px (2.0f), y - skin_.px (13.0f), skin_.px (34.0f), skin_.px (12.0f)),
                        juce::Justification::centredRight);
        }
    }
}

void CompressorHistoryView::renderOverlay (juce::Graphics& g)
{
    if (module_ == nullptr) return;
    const auto plot = plotArea();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (plot.toNearestInt());
    const auto sq = squareArea();
    // dégradé de séparation : fond presque opaque à gauche du carré, transparent à droite, pour que la courbe reste lisible sur l'historique
    g.setGradientFill (juce::ColourGradient (skin_.colour (Token::StageBg, 0.92f), sq.getX(), 0.0f, skin_.colour (Token::StageBg, 0.0f), sq.getX() + sq.getWidth() * 0.5f, 0.0f, false));
    g.fillRect (juce::Rectangle<float> (sq.getX(), plot.getY(), sq.getWidth() * 0.5f, plot.getHeight()));
    if (! curveBuilt_) { rebuildCurve(); curveBuilt_ = true; }
    {
        juce::Path unity;
        unity.startNewSubPath (xOfIn (minDb_), yOf (minDb_));
        unity.lineTo (xOfIn (maxDb_), yOf (maxDb_));
        juce::Path dashed;
        const float dash[] = { skin_.px (4.0f), skin_.px (4.0f) };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, unity, dash, 2);
        g.setColour (skin_.colour (Token::TextDim2, 0.6f));
        g.fillPath (dashed);
    }
    const double thr = std::clamp (host_.parameters().get (cm::threshold), static_cast<double> (minDb_), static_cast<double> (maxDb_));
    const float tx = xOfIn (static_cast<float> (thr));
    g.setColour (skin_.colour (Token::Accent, 0.06f));
    g.fillRect (juce::Rectangle<float> (tx, plot.getY(), sq.getRight() - tx, plot.getHeight()));
    {
        juce::Path line;
        line.startNewSubPath (tx, plot.getY());
        line.lineTo (tx, plot.getBottom());
        juce::Path dashed;
        const float dash[] = { skin_.px (3.0f), skin_.px (4.0f) };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dash, 2);
        g.setColour (skin_.colour (Token::Accent, 0.55f));
        g.fillPath (dashed);
    }
    g.setColour (skin_.colour (Token::Accent));   // orangée, comme la maquette
    g.strokePath (curve_, juce::PathStrokeType (skin_.px (2.4f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CompressorHistoryView::paint (juce::Graphics& g)
{
    const float ds = static_cast<float> (g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto size = getLocalBounds();
    if (! curveValid_ || bgImage_.isNull() || bgImage_.getWidth() != juce::roundToInt (static_cast<float> (size.getWidth()) * ds) || bgImage_.getHeight() != juce::roundToInt (static_cast<float> (size.getHeight()) * ds))
    {
        const int w = juce::roundToInt (static_cast<float> (size.getWidth()) * ds), h = juce::roundToInt (static_cast<float> (size.getHeight()) * ds);
        if (w <= 0 || h <= 0) return;
        bgImage_ = juce::Image (juce::Image::ARGB, w, h, true);
        overlayImage_ = juce::Image (juce::Image::ARGB, w, h, true);
        { juce::Graphics ig (bgImage_); ig.addTransform (juce::AffineTransform::scale (ds)); renderBackground (ig); }
        curveBuilt_ = false;
        { juce::Graphics og (overlayImage_); og.addTransform (juce::AffineTransform::scale (ds)); renderOverlay (og); }
        curveValid_ = true;
    }
    g.drawImage (bgImage_, size.toFloat(), juce::RectanglePlacement::stretchToFit);
    if (module_ == nullptr) return;

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plotArea().toNearestInt());
        const auto stroke = juce::PathStrokeType (skin_.px (1.7f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        if (! first_)
        {
            if (showIn_) { g.setColour (skin_.colour (Token::Accent, 0.16f)); g.fillPath (inPath_); }
            if (showKey_) { g.setColour (skin_.colour (Token::Band1, 0.9f)); g.strokePath (keyPath_, stroke); }
            if (showOut_) { g.setColour (skin_.colour (Token::Accent2, 0.95f)); g.strokePath (outPath_, stroke); }
            if (showRed_) { g.setColour (skin_.colour (Token::Pink, 0.95f)); g.strokePath (redPath_, stroke); }
        }
    }
    g.drawImage (overlayImage_, size.toFloat(), juce::RectanglePlacement::stretchToFit);

    const auto h = handle();
    const float hr = skin_.px (6.0f);
    g.setColour (skin_.colour (Token::Hring));
    g.fillEllipse (h.x - hr - 2.0f, h.y - hr - 2.0f, hr * 2 + 4.0f, hr * 2 + 4.0f);
    g.setColour (skin_.colour (Token::Handle));
    g.fillEllipse (h.x - hr, h.y - hr, hr * 2, hr * 2);
    g.setColour (skin_.colour (Token::Accent));
    g.drawEllipse (h.x - hr, h.y - hr, hr * 2, hr * 2, skin_.px (1.5f));
}

void CompressorHistoryView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (nearHandle (e.position) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void CompressorHistoryView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! nearHandle (e.position)) return;
    dragging_ = true;
    host_.uiBeginEdit (cm::threshold);
    mouseDrag (e);
}

void CompressorHistoryView::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging_) return;
    const auto* spec = host_.parameters().find (cm::threshold);
    if (spec == nullptr) return;
    host_.uiSetNormalised (cm::threshold, spec->toNormalized (std::clamp (static_cast<double> (dbOfX (e.position.x)), spec->min, spec->max)));
    repaint();
}

void CompressorHistoryView::mouseUp (const juce::MouseEvent&)
{
    if (dragging_) host_.uiEndEdit (cm::threshold);
    dragging_ = false;
}

void CompressorHistoryView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (nearHandle (e.position)) host_.uiResetToDefault (cm::threshold);
}

// Molette sur le panneau : seuil ; avec Ctrl ou Cmd : ratio (comme ZL)
void CompressorHistoryView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f) return;
    const int dir = w.deltaY > 0.0f ? 1 : -1;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        setParam (host_, cm::ratio, std::clamp (host_.parameters().get (cm::ratio) * std::pow (1.06, dir), 1.0, 100.0));
    else
        setParam (host_, cm::threshold, std::clamp (host_.parameters().get (cm::threshold) + 0.5 * dir, -100.0, 0.0));
}
} // namespace nova
