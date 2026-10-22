#include "NovaSamplerEditor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "ChokePanel.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SampleMeta.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mx = neptune::core::mixer;

namespace
{
juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// Boucle n d'une zone (0 = la première, les autres sont dans extraLoops) : lecture et écriture uniformes.
int loopCountOf (const ZoneDescription& z) { return 1 + static_cast<int> (z.extraLoops.size()); }
LoopRegion loopView (const ZoneDescription& z, int idx)
{
    idx = std::clamp (idx, 0, loopCountOf (z) - 1);
    if (idx == 0) return { z.loopStart, z.loopEnd, z.loopCrossfade, z.loopRepeats };
    return z.extraLoops[static_cast<std::size_t> (idx - 1)];
}
void setLoopView (ZoneDescription& z, int idx, const LoopRegion& l)
{
    idx = std::clamp (idx, 0, loopCountOf (z) - 1);
    if (idx == 0) { z.loopStart = l.start; z.loopEnd = l.end; z.loopCrossfade = l.crossfade; z.loopRepeats = l.repeats; }
    else z.extraLoops[static_cast<std::size_t> (idx - 1)] = l;
}

juce::Path iconPath (int kind)
{
    juce::Path p;
    if (kind == 0)   // zones : trois rectangles
    {
        p.addRectangle (3.0f, 4.0f, 8.0f, 7.0f);
        p.addRectangle (13.0f, 4.0f, 8.0f, 7.0f);
        p.addRectangle (3.0f, 13.0f, 18.0f, 7.0f);
    }
    else if (kind == 1)   // sample : forme d'onde
    {
        const float h[9] = { 3, 7, 11, 6, 9, 4, 8, 5, 2 };
        for (int i = 0; i < 9; ++i) { const float x = 3.0f + 2.2f * static_cast<float> (i); p.startNewSubPath (x, 12.0f - h[i]); p.lineTo (x, 12.0f + h[i]); }
    }
    else if (kind == 2)   // son : enveloppe
    {
        p.startNewSubPath (2.0f, 20.0f);
        p.lineTo (7.0f, 4.0f);
        p.lineTo (12.0f, 11.0f);
        p.lineTo (18.0f, 11.0f);
        p.lineTo (22.0f, 20.0f);
    }
    else if (kind == 3)   // instrument : curseurs
    {
        for (const float x : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (x, 3.0f); p.lineTo (x, 21.0f); }
        p.addRectangle (3.5f, 8.0f, 5.0f, 3.0f);
        p.addRectangle (9.5f, 14.0f, 5.0f, 3.0f);
        p.addRectangle (15.5f, 5.0f, 5.0f, 3.0f);
    }
    else   // réglages
    {
        for (const float y : { 6.0f, 12.0f, 18.0f }) { p.startNewSubPath (3.0f, y); p.lineTo (21.0f, y); }
        p.addEllipse (14.0f, 4.0f, 4.0f, 4.0f);
        p.addEllipse (6.0f, 10.0f, 4.0f, 4.0f);
        p.addEllipse (12.0f, 16.0f, 4.0f, 4.0f);
    }
    return p;
}
} // namespace

// Clic droit sur la case de chargement d'un Groupe : menu « Assigner à un emplacement ».
struct NovaSamplerEditor::GroupMenuListener : public juce::MouseListener
{
    explicit GroupMenuListener (NovaSamplerEditor& o) : owner (o) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        for (int i = 0; i < owner.groupToggles_.size(); ++i)
            if (owner.groupToggles_[i] == e.eventComponent && static_cast<std::size_t> (i) < owner.groupNames_.size())
            {
                owner.showSlotAssignMenu (-1, owner.groupNames_[static_cast<std::size_t> (i)]);
                return;
            }
    }
    NovaSamplerEditor& owner;
};

// Niveau en dB (RMS) d'une courte fenêtre d'un fichier, à partir de `start` ; faux si illisible ou silencieux.
static bool windowLevelDb (const std::string& path, std::int64_t start, double ms, double& db)
{
    AudioStreamReader reader;
    if (! reader.open (std::filesystem::u8path (path))) return false;
    const auto info = probeAudioFile (std::filesystem::u8path (path));
    const double rate = info.ok && info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const std::int64_t n = std::min<std::int64_t> (std::max<std::int64_t> (64, static_cast<std::int64_t> (ms * rate / 1000.0)), reader.totalFrames() - start);
    if (n < 64 || ! reader.seek (start)) return false;
    std::vector<float> buf (static_cast<std::size_t> (n * reader.channels()));
    const auto got = reader.read (buf.data(), n);
    if (got < 64) return false;
    double sum = 0.0;
    for (std::int64_t i = 0; i < got * reader.channels(); ++i) sum += static_cast<double> (buf[static_cast<std::size_t> (i)]) * buf[static_cast<std::size_t> (i)];
    const double rms = std::sqrt (sum / static_cast<double> (got * reader.channels()));
    if (rms < 1.0e-5) return false;
    db = 20.0 * std::log10 (rms);
    return true;
}

// Fenêtre « Niveaux » : compensation au remplacement de fichier (instrument) et volume de relâchement lié au niveau (zones déclenchées au Note Off).
class LevelsPanel : public juce::Component
{
public:
    std::function<void (bool)> onCompensate;
    std::function<void (bool follow, int meter, double floorDb, double curve, double offsetDb)> onRelease;

    LevelsPanel (const nova::Skin& skin, const i18n::Translator& tr, bool compensate, const ZoneDescription* zone)
        : skin_ (skin), look_ (skin_), tr_ (tr)
    {
        setLookAndFeel (&look_);
        auto text = [&] (const char* key) { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); };
        compensate_.setButtonText (text ("sm.lv_compensate"));
        compensate_.setToggleState (compensate, juce::dontSendNotification);
        compensate_.onClick = [this] { if (onCompensate) onCompensate (compensate_.getToggleState()); };
        follow_.setButtonText (text ("sm.lv_follow"));
        meter_.addItem (text ("sm.lv_peak"), 1);
        meter_.addItem ("RMS", 2);
        meter_.addItem (text ("sm.lv_lufs"), 3);
        meterLabel_.setText (text ("sm.lv_meter"), juce::dontSendNotification);
        floorLabel_.setText (text ("sm.lv_floor"), juce::dontSendNotification);
        curveLabel_.setText (text ("sm.lv_curve"), juce::dontSendNotification);
        offsetLabel_.setText (text ("sm.lv_offset"), juce::dontSendNotification);
        hint_.setText (text ("sm.lv_hint"), juce::dontSendNotification);
        hint_.setFont (juce::FontOptions (11.5f));
        hint_.setJustificationType (juce::Justification::topLeft);
        floor_.setRange (-96.0, -6.0, 1.0);
        floor_.setTextValueSuffix (" dB");
        curve_.setRange (0.25, 4.0, 0.05);
        offset_.setRange (-24.0, 24.0, 0.5);
        offset_.setTextValueSuffix (" dB");
        const bool release = zone != nullptr && zone->onRelease;
        follow_.setToggleState (release && zone->releaseFollowsLevel, juce::dontSendNotification);
        meter_.setSelectedId (zone != nullptr ? zone->releaseMeter + 1 : 3, juce::dontSendNotification);
        floor_.setValue (zone != nullptr ? zone->releaseFloorDb : -48.0, juce::dontSendNotification);
        curve_.setValue (zone != nullptr ? zone->releaseCurve : 1.0, juce::dontSendNotification);
        offset_.setValue (zone != nullptr ? zone->releaseOffsetDb : 0.0, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &follow_, &meter_, &floor_, &curve_, &offset_, &meterLabel_, &floorLabel_, &curveLabel_, &offsetLabel_ }) c->setEnabled (release);
        auto changed = [this] { if (onRelease) onRelease (follow_.getToggleState(), meter_.getSelectedId() - 1, floor_.getValue(), curve_.getValue(), offset_.getValue()); };
        follow_.onClick = changed;
        meter_.onChange = changed;
        floor_.onValueChange = changed;
        curve_.onValueChange = changed;
        offset_.onValueChange = changed;
        for (auto* c : std::initializer_list<juce::Component*> { &compensate_, &follow_, &meter_, &floor_, &curve_, &offset_, &meterLabel_, &floorLabel_, &curveLabel_, &offsetLabel_, &hint_ }) addAndMakeVisible (*c);
        setSize (440, 330);
    }
    ~LevelsPanel() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override { g.fillAll (skin_.colour (Token::BgCard)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        compensate_.setBounds (r.removeFromTop (26));
        r.removeFromTop (10);
        follow_.setBounds (r.removeFromTop (26));
        auto row = [&] (juce::Component& label, juce::Component& c) { auto line = r.removeFromTop (28); label.setBounds (line.removeFromLeft (130)); c.setBounds (line); r.removeFromTop (4); };
        row (meterLabel_, meter_);
        row (floorLabel_, floor_);
        row (curveLabel_, curve_);
        row (offsetLabel_, offset_);
        r.removeFromTop (6);
        hint_.setBounds (r);
    }

private:
    nova::Skin skin_;
    nova::LookAndFeel look_;
    const i18n::Translator& tr_;
    juce::ToggleButton compensate_, follow_;
    juce::ComboBox meter_;
    juce::Slider floor_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, curve_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, offset_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label meterLabel_, floorLabel_, curveLabel_, offsetLabel_, hint_;
};

// Options de la recherche automatique de boucle.
class LoopOptionsPanel : public juce::Component
{
public:
    std::function<void (const neptune::core::sampleedit::LoopSearchOptions&)> onChange;

    LoopOptionsPanel (const nova::Skin& skin, const i18n::Translator& tr, const neptune::core::sampleedit::LoopSearchOptions& o)
        : skin_ (skin), look_ (skin_), o_ (o)
    {
        setLookAndFeel (&look_);
        auto text = [&] (const char* key) { return juce::String::fromUTF8 (std::string (tr.tr (key)).c_str()); };
        period_.setButtonText (text ("sm.lo_period"));
        stable_.setButtonText (text ("sm.lo_stable"));
        spectral_.setButtonText (text ("sm.lo_spectral"));
        around_.setButtonText (text ("sm.lo_around"));
        radiusLabel_.setText (text ("sm.lo_radius"), juce::dontSendNotification);
        hint_.setText (text ("sm.lo_hint"), juce::dontSendNotification);
        hint_.setFont (juce::FontOptions (11.5f));
        hint_.setJustificationType (juce::Justification::topLeft);
        period_.setToggleState (o.period, juce::dontSendNotification);
        stable_.setToggleState (o.stableOnly, juce::dontSendNotification);
        spectral_.setToggleState (o.spectral, juce::dontSendNotification);
        around_.setToggleState (o.aroundCursor, juce::dontSendNotification);
        radius_.setRange (50.0, 3000.0, 10.0);
        radius_.setTextValueSuffix (" ms");
        radius_.setValue (o.radiusMs, juce::dontSendNotification);
        auto changed = [this]
        {
            o_.period = period_.getToggleState(); o_.stableOnly = stable_.getToggleState(); o_.spectral = spectral_.getToggleState();
            o_.aroundCursor = around_.getToggleState(); o_.radiusMs = radius_.getValue();
            radius_.setEnabled (o_.aroundCursor); radiusLabel_.setEnabled (o_.aroundCursor);
            if (onChange) onChange (o_);
        };
        for (auto* t : { &period_, &stable_, &spectral_, &around_ }) t->onClick = changed;
        radius_.onValueChange = changed;
        radius_.setEnabled (o.aroundCursor); radiusLabel_.setEnabled (o.aroundCursor);
        for (auto* c : std::initializer_list<juce::Component*> { &period_, &stable_, &spectral_, &around_, &radius_, &radiusLabel_, &hint_ }) addAndMakeVisible (*c);
        setSize (440, 290);
    }
    ~LoopOptionsPanel() override { setLookAndFeel (nullptr); }
    void paint (juce::Graphics& g) override { g.fillAll (skin_.colour (Token::BgCard)); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        for (auto* t : { &period_, &stable_, &spectral_, &around_ }) { t->setBounds (r.removeFromTop (26)); r.removeFromTop (2); }
        auto line = r.removeFromTop (28);
        radiusLabel_.setBounds (line.removeFromLeft (150));
        radius_.setBounds (line);
        r.removeFromTop (8);
        hint_.setBounds (r);
    }

private:
    nova::Skin skin_;
    nova::LookAndFeel look_;
    neptune::core::sampleedit::LoopSearchOptions o_;
    juce::ToggleButton period_, stable_, spectral_, around_;
    juce::Slider radius_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label radiusLabel_, hint_;
};

// ------------------------------------------------------------------------------------------------ construction
NovaSamplerEditor::NovaSamplerEditor (NeptuneProcessor& p, const theme::Palette& palette, theme::KnobStyle knob, const i18n::Translator& tr, Options options)
    : EditorBase (p, palette, knob, tr, std::move (options), juce::String::fromUTF8 ("SAMPLER")), proc_ (p)
{
    groupMenuListener_ = std::make_unique<GroupMenuListener> (*this);
    addPage (tx ("sm.page_zones"), iconPath (0));
    addPage (tx ("sm.page_sample"), iconPath (1));
    addPage (tx ("sm.page_sound"), iconPath (2));
    addPage (tx ("sm.page_instrument"), iconPath (3));
    addPage (tx ("nv.page_settings"), iconPath (4));
    buildLeftColumn();
    buildZonesPage();
    buildSamplePage();
    buildSoundPage();
    buildInstrumentPage();
    buildSettingsPage (4);
    setPage (0);
    setSize (designWidth, designHeight);
}

NovaSamplerEditor::~NovaSamplerEditor()
{
    // une prise en cours continue : elle est fermée avec le processeur ; le fichier reste dans Edited/Recordings
    footprintCancel_ = true;
    if (footprintJob_.valid()) footprintJob_.wait();
    stretchCancel_ = true;
    if (stretchThread_.joinable()) stretchThread_.join();
    proc_.setVoiceMonitor (false);
}

void NovaSamplerEditor::buildLeftColumn()
{
    outPanel_ = add<nova::Panel> (-1, skin());
    outPanel_->setTitle (tx ("sm.output").toUpperCase());
    addInfo (-1, outPanel_, "hlp.sm_out", 1);
    const int tr = proc_.selectedTrack();
    for (const ParamId id : { mx::samplerParam (tr, mx::kGain), mx::samplerParam (tr, mx::kTuneSemitones), mx::samplerParam (tr, mx::kTuneCents) })
    {
        const auto idx = host().parameters().indexOf (id);
        const juce::String title = idx == ParameterSet::npos ? juce::String() : U (host().parameters().spec (idx).name).toUpperCase();
        auto* k = add<nova::KnobCard> (-1, skin(), host(), id, title, modalOpener());
        k->setPlain (true);
        k->setCompact (true);
        outKnobs_.push_back (k);
    }
    statePanel_ = add<nova::Panel> (-1, skin());
    statePanel_->setTitle (tx ("sm.state").toUpperCase());
    addInfo (-1, statePanel_, "hlp.sm_state", 1);
    statusText_ = add<nova::Text> (-1, skin(), Token::TextDim, 11.0f);
    voicesPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    voicesPill_->onClick = [this]
    {
        juce::PopupMenu menu;
        const int current = proc_.masterSettings().maxVoices;
        menu.addSectionHeader (tx ("voices.global_limit"));
        for (const int n : { 16, 32, 64, 128, 256, 512, 1024 }) menu.addItem (n, juce::String (n), true, n == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (voicesPill_), [this] (int chosen)
        {
            if (chosen <= 0) return;
            auto m = proc_.masterSettings();
            m.maxVoices = chosen;
            proc_.uiEditMaster (m, "Voice limit");
            lastVoiceCount_ = -1;
        });
    };
    navPanel_ = add<nova::Panel> (-1, skin());
    navPanel_->toBack();
}

void NovaSamplerEditor::buildZonesPage()
{
    zonePanel_ = add<nova::Panel> (0, skin());
    zonePanel_->setTitle (tx ("sm.zone").toUpperCase());
    addInfo (0, zonePanel_, "hlp.sm_zone", 1);
    auto pill = [this] (int page, const juce::String& text, std::function<void()> click)
    {
        auto* p = add<nova::Pill> (page, skin(), nova::Pill::Look::Small);
        p->setText (text);
        p->onClick = std::move (click);
        return p;
    };
    loadPill_ = pill (0, tx ("sampler.load"), [this] { chooseSample(); });
    importPill_ = pill (0, tx ("sampler.import"), [this] { startImport(); });
    removePill_ = pill (0, tx ("sampler.remove"), [this] { removeSelectedZones(); });
    listPill_ = pill (0, tx ("map.list"), [this] { listVisible_ = ! listVisible_; layout(); pageChanged(); });
    monitorPill_ = pill (0, tx ("voices.button"), [this]
    {
        showVoices_ = ! showVoices_;
        proc_.setVoiceMonitor (showVoices_);
        pageChanged();
    });
    selectionText_ = add<nova::Text> (0, skin(), Token::TextDim, 11.0f);
    modePill_ = pill (0, {}, [this]
    {
        editZone ([] (Zone& z) { z.mode = z.mode == PlayMode::Loop ? PlayMode::OneShot : PlayMode::Loop; });
    });
    rootNumber_ = add<nova::NumberPill> (0, skin(), tx ("sampler.root").toUpperCase(), 0, 127,
                                         [this] { const auto* z = shownZone(); return z != nullptr ? z->rootNote : 60; },
                                         [this] (int v) { editZone ([v] (Zone& z) { z.rootNote = v; }); },
                                         [this] (int v) { return juce::String (v) + "  " + noteName (v); });
    rootNumber_->gestureBegin = [this] { proc_.uiBeginGesture(); };
    rootNumber_->gestureEnd = [this] { proc_.uiEndGesture(); };
    auto choice = [this] (int page, juce::String label, std::vector<juce::String> items, std::function<int()> get, std::function<void (int)> set)
    {
        auto c = std::make_unique<nova::ChoicePill>();
        c->pill = add<nova::Pill> (page, skin(), nova::Pill::Look::Small);
        c->label = [label] { return label; };
        c->items = std::move (items);
        c->get = std::move (get);
        c->set = std::move (set);
        c->attach (*this);
        auto* raw = c.get();
        choices_.push_back (std::move (c));
        return raw;
    };
    triggerChoice_ = choice (0, tx ("trig.label").toUpperCase(), { tx ("trig.attack"), tx ("trig.release"), tx ("trig.first"), tx ("trig.legato") },
        [this]
        {
            const auto* z = shownZone();
            if (z == nullptr) return 0;
            return z->onRelease ? 1 : z->trigger == TriggerCondition::First ? 2 : z->trigger == TriggerCondition::Legato ? 3 : 0;
        },
        [this] (int i)
        {
            editZone ([i] (Zone& z)
            {
                z.onRelease = i == 1;
                z.trigger = i == 2 ? TriggerCondition::First : i == 3 ? TriggerCondition::Legato : TriggerCondition::Always;
            });
        });
    std::vector<juce::String> chokes { tx ("choke.none") };
    for (int g = 1; g <= maxChokeGroup; ++g) chokes.push_back (juce::String (g));
    chokeChoice_ = choice (0, tx ("choke.label").toUpperCase(), chokes,
        [this] { const auto* z = shownZone(); return z != nullptr && z->chokeCuts == z->chokeCutBy ? z->chokeCuts : 0; },
        [this] (int g) { editZone ([g] (Zone& z) { z.chokeCuts = g; z.chokeCutBy = g; }); });
    chokeMorePill_ = pill (0, "...", [this] { openChokePanel (*chokeMorePill_); });
    levelsPill_ = pill (0, tx ("sm.levels"), [this] { openLevelsPanel (*levelsPill_); });

    tablePanel_ = add<nova::Panel> (0, skin());
    tablePanel_->setTitle (tx ("sm.table").toUpperCase());
    addInfo (0, tablePanel_, "hlp.sm_table", 1);
    matrixPanel_ = add<nova::Panel> (0, skin());
    matrixPanel_->setTitle (tx ("sm.matrix").toUpperCase());
    addInfo (0, matrixPanel_, "hlp.sm_matrix", 1);

    zones_ = add<ZoneTable> (0, skin().palette(), translator());
    zones_->onSelectionChanged = [this] { lastRevision_ = 0xFFFFFFFFu; };
    zones_->onCellEdited = [this] (int row, int column, int value)
    {
        auto rows = zones_->selectedRows();
        if (std::find (rows.begin(), rows.end(), row) == rows.end()) rows = { row };
        auto d = proc_.instrumentDescription();
        for (const int r : rows)
        {
            if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
            auto& z = d.zones[static_cast<std::size_t> (r)];
            if (column == 7)
            {
                z.onRelease = value == 2;
                z.trigger = value == 3 ? TriggerCondition::First : value == 4 ? TriggerCondition::Legato : TriggerCondition::Always;
            }
            else if (column == 8) z.chokeCuts = value;
            else if (column == 9) z.chokeCutBy = value;
        }
        proc_.uiEditInstrument (std::move (d), "Edit zone", "zone");
        lastRevision_ = 0xFFFFFFFFu;
    };
    zones_->onPlay = [this] (int row, bool down, bool stop)
    {
        if (stop) proc_.uiPreviewStop();
        else if (down) proc_.uiPreviewZone (row);
        else proc_.uiPreviewRelease();
    };
    zones_->onContextMenu = [this] (int row) { showSlotAssignMenu (row, {}); };

    matrix_ = add<MappingMatrix> (0, skin().palette(), translator());
    matrix_->onSelect = [this] (const std::vector<int>& z) { setSelection (z); };
    matrix_->onEdit = [this] (InstrumentDescription d, const char* name) { proc_.uiEditInstrument (std::move (d), name, "matrix"); };
    matrix_->onGestureBegin = [this] { proc_.uiBeginGesture(); };
    matrix_->onGestureEnd = [this] { proc_.uiEndGesture(); };
    matrix_->onDeleteRequested = [this] { removeSelectedZones(); };
    matrix_->onNoteOn = [this] (int key, float v) { proc_.uiNoteOn (key, v); };
    matrix_->onNoteOff = [this] (int key) { proc_.uiNoteOff (key); };
    matrix_->onOpenVelocity = [this] (juce::Component&) { setPage (3); };   // la réponse de vélocité a sa propre page
    matrix_->onOpenPolyphony = [this] (juce::Component&) { setPage (3); };
    voicesView_ = add<VoicesView> (0, skin().palette(), translator());
}

void NovaSamplerEditor::buildSamplePage()
{
    wavePanel_ = add<nova::Panel> (1, skin());
    wavePanel_->setTitle (tx ("sm.wave").toUpperCase());
    addInfo (1, wavePanel_, "hlp.sm_wave", 1);
    wave_ = add<nova::WaveView> (1, skin());
    wave_->setAutoLabel (tx ("sm.no_sample"));
    wave_->onGestureBegin = [this] { proc_.uiBeginGesture(); };
    wave_->onGestureEnd = [this]
    {
        proc_.uiEndGesture();
        if (volumeDragged_)   // le marqueur de volume est une propriété du sample : à la fin du glissé, il est écrit dans le fichier
        {
            volumeDragged_ = false;
            if (const auto* zz = shownZone()) saveMarker (zz->samplePath, zz->releaseRefFrame);
        }
    };
    applyMarker_ = [this] (nova::WaveView::Marker m, std::int64_t frame)
    {
        const auto* shown = shownZone();
        if (shown == nullptr) return;
        const auto& info = fileInfo_.info (shown->samplePath);
        const std::int64_t total = info.ok ? info.frames : 0;
        if (snapOn_ && m != nova::WaveView::Marker::Crossfade) frame = snapFrame (shown->samplePath, frame);
        const int idx = editLoop_;
        editZone ([m, frame, total, idx] (Zone& z)
        {
            const std::int64_t end = z.endFrame < 0 ? total : z.endFrame;
            switch (m)
            {
                case nova::WaveView::Marker::Start: z.startFrame = std::clamp<std::int64_t> (frame, 0, std::max<std::int64_t> (0, end - 1)); z.loopStart = std::max (z.loopStart, z.startFrame); break;
                case nova::WaveView::Marker::End: z.endFrame = frame >= total ? -1 : std::max (frame, z.startFrame + 1); break;
                case nova::WaveView::Marker::LoopStart:
                {
                    auto l = loopView (z, idx);
                    const std::int64_t lend = l.end < 0 ? total : l.end;
                    const std::int64_t lo = std::max (idx > 0 ? loopView (z, idx - 1).start : z.startFrame, z.startFrame);
                    l.start = std::clamp<std::int64_t> (frame, lo, std::max (lo, lend - 1));
                    setLoopView (z, idx, l);
                    break;
                }
                case nova::WaveView::Marker::LoopEnd:
                {
                    auto l = loopView (z, idx);
                    std::int64_t lo = l.start + 1;
                    if (idx > 0) { const auto prev = loopView (z, idx - 1); lo = std::max (lo, (prev.end < 0 ? total : prev.end) + 1); }
                    l.end = frame >= total ? -1 : std::max (frame, lo);
                    setLoopView (z, idx, l);
                    break;
                }
                case nova::WaveView::Marker::Crossfade:
                {
                    auto l = loopView (z, idx);
                    const std::int64_t lend = l.end < 0 ? total : l.end;
                    l.crossfade = std::clamp<std::int64_t> (frame, 0, std::min (l.start, (lend - l.start) / 2));
                    setLoopView (z, idx, l);
                    break;
                }
                case nova::WaveView::Marker::Volume: z.releaseRefFrame = std::clamp<std::int64_t> (frame, 0, std::max<std::int64_t> (0, total - 1)); volumeDragged_ = true; break;
            }
        });
    };
    wave_->onMarker = applyMarker_;
    wave_->onCursor = [this] (std::int64_t frame) { cursorFrame_ = frame; };
    wave_->onContext = [this] (std::int64_t frame, juce::Point<int> where) { showWaveMenu (frame, where); };
    infoText_ = add<nova::Text> (1, skin(), Token::TextDim, 11.0f);
    markerText_ = add<nova::Text> (1, skin(), Token::TextDim2, 11.0f, true);

    playPanel_ = add<nova::Panel> (1, skin());
    playPanel_->setTitle (tx ("sm.play").toUpperCase());
    addInfo (1, playPanel_, "hlp.sm_play", 1);
    loopPill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    loopPill_->onClick = [this] { editZone ([] (Zone& z) { z.mode = z.mode == PlayMode::Loop ? PlayMode::OneShot : PlayMode::Loop; }); };
    reversePill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    reversePill_->setText (tx ("sm.reverse"));
    reversePill_->onClick = [this] { const auto* z = shownZone(); const bool now = z != nullptr && ! z->reverse; editZone ([now] (Zone& d) { d.reverse = now; }); };
    addInfo (1, reversePill_, "hlp.sm_reverse", 1);
    // Bouton REC : enregistre l'entrée audio de l'hôte ; à l'arrêt, une zone est créée avec la prise
    recPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    recPill_->setText (tx ("sm.rec"));
    addInfo (-1, recPill_, "hlp.sm_rec", 1);
    recPill_->onClick = [this] { toggleRecording(); };
    // Bouton PLAY / STOP : dans la barre du bas, visible sur toutes les pages (page -1)
    playPill_ = add<nova::Pill> (-1, skin(), nova::Pill::Look::Small);
    playPill_->setText (tx ("sm.play_button"));
    addInfo (-1, playPill_, "hlp.sm_playbtn", 1);
    playPill_->onClick = [this]
    {
        if (previewing_ && previewOwner_ == 1) { stopPreviewNow(); return; }
        const auto* zz = shownZone();
        if (zz == nullptr) return;
        // le sample joué en une fois : de START à END (ou du curseur à END), sans boucle
        const bool fromCursor = cursorFrame_ >= 0 && cursorPath_ == zz->samplePath;
        startPreview (fromCursor ? 3 : 2, fromCursor ? cursorFrame_ : -1, 0, 1);
    };
    showAllPill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    showAllPill_->setText (tx ("sm.show_all"));
    showAllPill_->onClick = [this] { wave_->showAll(); };

    auto slider = [this] (juce::String title, double lo, double hi, double step, juce::String suffix, std::function<double()> get, std::function<void (Zone&, double)> set, double def)
    {
        auto* s = add<nova::ValueSlider> (1, skin(), std::move (title), lo, hi, step, std::move (suffix), std::move (get),
                                          [this, set] (double v) { editZone ([&] (Zone& z) { set (z, v); }); }, def);
        s->gestureBegin = [this] { proc_.uiBeginGesture(); };
        s->gestureEnd = [this] { proc_.uiEndGesture(); };
        return s;
    };
    fadeIn_ = slider (tx ("sm.fade_in").toUpperCase(), 0.0, 500.0, 0.5, " ms", [this] { const auto* z = shownZone(); return z != nullptr ? z->fadeInMs : ZoneDescription::defaultFadeInMs; },
                      [] (Zone& z, double v) { z.fadeInMs = v; }, ZoneDescription::defaultFadeInMs);
    addInfo (1, fadeIn_, "hlp.sm_fadein", 1);
    fadeOut_ = slider (tx ("sm.fade_out").toUpperCase(), 0.0, 500.0, 0.5, " ms", [this] { const auto* z = shownZone(); return z != nullptr ? z->fadeOutMs : ZoneDescription::defaultFadeOutMs; },
                       [] (Zone& z, double v) { z.fadeOutMs = v; }, ZoneDescription::defaultFadeOutMs);
    addInfo (1, fadeOut_, "hlp.sm_fadeout", 1);
    delay_ = slider (tx ("sm.delay").toUpperCase(), 0.0, 2000.0, 1.0, " ms", [this] { const auto* z = shownZone(); return z != nullptr ? z->delayMs : 0.0; },
                     [] (Zone& z, double v) { z.delayMs = v; }, 0.0);
    addInfo (1, delay_, "hlp.sm_delay", 1);
    crossfade_ = slider (tx ("sm.crossfade").toUpperCase(), 0.0, 1000.0, 1.0, " ms",
                         [this] { const auto* z = shownZone(); return z != nullptr ? static_cast<double> (loopView (*z, editLoop_).crossfade) * 1000.0 / sampleRate() : 0.0; },
                         [this] (Zone& z, double v)
                         {
                             const auto& info = fileInfo_.info (z.samplePath);
                             const std::int64_t total = info.ok ? info.frames : 0;
                             auto l = loopView (z, editLoop_);
                             const std::int64_t loopEnd = l.end < 0 ? total : l.end;
                             l.crossfade = std::clamp<std::int64_t> (static_cast<std::int64_t> (std::llround (v * sampleRate() / 1000.0)), 0, std::min (l.start, std::max<std::int64_t> (0, (loopEnd - l.start) / 2)));
                             setLoopView (z, editLoop_, l);
                         }, 0.0);
    addInfo (1, crossfade_, "hlp.sm_crossfade", 1);
    tune_ = slider (tx ("sm.tune").toUpperCase(), -100.0, 100.0, 0.5, " ct", [this] { const auto* z = shownZone(); return z != nullptr ? z->tuneCents : 0.0; },
                    [] (Zone& z, double v) { z.tuneCents = v; }, 0.0);
    gain_ = slider (tx ("sm.gain").toUpperCase(), -24.0, 24.0, 0.1, " dB", [this] { const auto* z = shownZone(); return z != nullptr ? z->gainDb : 0.0; },
                    [] (Zone& z, double v) { z.gainDb = v; }, 0.0);
    pan_ = slider (tx ("sm.pan").toUpperCase(), -100.0, 100.0, 1.0, " %", [this] { const auto* z = shownZone(); return z != nullptr ? z->pan * 100.0 : 0.0; },
                   [] (Zone& z, double v) { z.pan = v / 100.0; }, 0.0);
    addInfo (1, pan_, "hlp.sm_pan", 1);
    // boucles multiples : choix de la boucle éditée, ajout / retrait, répétitions
    loopEditChoice_ = std::make_unique<nova::ChoicePill>();
    loopEditChoice_->pill = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    loopEditChoice_->label = [this] { return tx ("sm.loop_n").toUpperCase(); };
    loopEditChoice_->get = [this] { return editLoop_; };
    loopEditChoice_->set = [this] (int i) { editLoop_ = i; navPoint_ = -1; lastRevision_ = 0xFFFFFFFFu; };
    loopEditChoice_->attach (*this);
    loopAddPill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    loopAddPill_->setText (tx ("sm.loop_add"));
    loopAddPill_->onClick = [this] { showAddLoopMenu(); };
    addInfo (1, loopAddPill_, "hlp.sm_multiloop", 1);
    loopRemovePill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    loopRemovePill_->setText (tx ("sm.loop_remove"));
    loopRemovePill_->onClick = [this] { removeLoop(); };
    loopRepeatsNumber_ = add<nova::NumberPill> (1, skin(), tx ("sm.loop_repeats").toUpperCase(), 0, 99,
                                                [this] { const auto* z = shownZone(); return z != nullptr ? loopView (*z, editLoop_).repeats : 0; },
                                                [this] (int v) { editZone ([this, v] (Zone& z) { auto l = loopView (z, editLoop_); l.repeats = v; setLoopView (z, editLoop_, l); }); },
                                                [] (int v) { return v == 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : juce::String (v); });
    rootNumber2_ = add<nova::NumberPill> (1, skin(), tx ("sampler.root").toUpperCase(), 0, 127,
                                          [this] { const auto* z = shownZone(); return z != nullptr ? z->rootNote : 60; },
                                          [this] (int v) { editZone ([v] (Zone& z) { z.rootNote = v; }); },
                                          [this] (int v) { return juce::String (v) + "  " + noteName (v); });
    rootNumber2_->gestureBegin = [this] { proc_.uiBeginGesture(); };
    rootNumber2_->gestureEnd = [this] { proc_.uiEndGesture(); };

    // ---- outils du sample
    toolsPanel_ = add<nova::Panel> (1, skin());
    toolsPanel_->setTitle (tx ("sm.tools").toUpperCase());
    addInfo (1, toolsPanel_, "hlp.sm_tools", 1);
    auto tool = [this] (juce::String text, const char* help, std::function<void()> click)
    {
        auto* pl = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        pl->setText (text);
        pl->onClick = std::move (click);
        if (help != nullptr) addInfo (1, pl, help, 1);
        return pl;
    };
    auto toolChoice = [this] (juce::String label, std::vector<juce::String> items, int* index)
    {
        auto c = std::make_unique<nova::ChoicePill>();
        c->pill = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
        c->label = [label] { return label; };
        c->items = std::move (items);
        c->get = [index] { return *index; };
        c->set = [index] (int i) { *index = i; };
        c->attach (*this);
        auto* raw = c.get();
        choices_.push_back (std::move (c));
        return raw;
    };
    trimPill_ = tool (tx ("sm.tool_trim"), "hlp.sm_trim", [this] { toolTrim(); });
    normPill_ = tool (tx ("sm.tool_norm"), "hlp.sm_norm", [this] { toolNormalize(); });
    normChoice_ = toolChoice (tx ("sm.norm_target"), { "PEAK 0 dB", "PEAK -1 dB", "PEAK -3 dB", "RMS -18 dB", "RMS -12 dB", "RMS -9 dB" }, &normIndex_);
    dcPill_ = tool (tx ("sm.tool_dc"), "hlp.sm_dc", [this] { toolRemoveDc(); });
    saveAsPill_ = tool (tx ("sm.tool_saveas"), "hlp.sm_saveas", [this] { toolSaveAs(); });
    silencePill_ = tool (tx ("sm.tool_silence"), "hlp.sm_silence", [this] { toolSilence(); });
    silenceChoice_ = toolChoice (tx ("sm.silence_thr"), { "-30 dB", "-40 dB", "-50 dB", "-60 dB", "-72 dB" }, &silenceIndex_);
    cutsPill_ = tool (tx ("sm.tool_cuts"), "hlp.sm_cuts", [this] { toolSplitHits(); });
    cutsChoice_ = toolChoice (tx ("sm.cuts_sens"), { tx ("sm.sens_low"), tx ("sm.sens_mid"), tx ("sm.sens_high") }, &cutsIndex_);
    loopFindPill_ = tool (tx ("sm.tool_loop"), "hlp.sm_loop", [this] { toolAutoLoop(); });
    loopOptionsPill_ = tool ("...", nullptr, [this] { openLoopOptions (*loopOptionsPill_); });
    snapPill_ = tool (tx ("sm.tool_snap"), "hlp.sm_snap", [this] { snapOn_ = ! snapOn_; snapPill_->setOn (snapOn_); });
    toolStatus_ = add<nova::Text> (1, skin(), Token::TextDim, 11.0f);
    durationNumber_ = add<nova::NumberPill> (1, skin(), tx ("sm.tool_duration").toUpperCase(), 25, 400, [this] { return stretchPercent_; }, [this] (int v) { stretchPercent_ = v; },
                                             [] (int v) { return juce::String (v) + " %"; });
    addInfo (1, durationNumber_, "hlp.sm_stretch", 1);
    pitchNumber_ = add<nova::NumberPill> (1, skin(), tx ("sm.tool_pitch").toUpperCase(), -240, 240, [this] { return pitchTenths_; }, [this] (int v) { pitchTenths_ = v; },
                                          [] (int v) { return (v > 0 ? "+" : "") + juce::String (static_cast<double> (v) / 10.0, 1) + " st"; });
    addInfo (1, pitchNumber_, "hlp.sm_stretch", 1);
    stretchPill_ = tool (tx ("sm.tool_stretch"), "hlp.sm_stretch", [this] { toolStretch(); });
    seam_ = add<nova::LoopSeamView> (1, skin());
    toolsPlayPill_ = tool (tx ("sm.tp_play"), "hlp.sm_toolplay", [this]
    {
        if (previewing_ && previewOwner_ == 2) { stopPreviewNow(); return; }
        const auto* zz = shownZone();
        if (zz == nullptr) return;
        const int mode = playSourceIndex_;   // 0 brut, 1 région, 2 depuis le curseur, 3 boucle
        if (mode == 0) startPreview (1, -1, 0, 2);
        else if (mode == 1) startPreview (2, -1, 0, 2);
        else if (mode == 2)
        {
            const bool ok = cursorFrame_ >= 0 && cursorPath_ == zz->samplePath;
            if (! ok) { setToolStatus (tx ("sm.st_loop_nocursor")); return; }
            startPreview (3, cursorFrame_, 0, 2);
        }
        else startPreview (loopPlayHeard_ ? 5 : 4, -1, std::clamp (editLoop_, 0, loopCountOf (*zz) - 1), 2);
    });
    playSourceChoice_ = toolChoice (tx ("sm.tp_source"), { tx ("sm.tp_raw"), tx ("sm.tp_region"), tx ("sm.tp_cursor"), tx ("sm.tp_loop") }, &playSourceIndex_);
    toolLoopChoice_ = std::make_unique<nova::ChoicePill>();
    toolLoopChoice_->pill = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    toolLoopChoice_->label = [this] { return tx ("sm.loop_n").toUpperCase(); };
    toolLoopChoice_->get = [this] { return editLoop_; };
    toolLoopChoice_->set = [this] (int i) { editLoop_ = i; navPoint_ = -1; lastRevision_ = 0xFFFFFFFFu; };
    toolLoopChoice_->attach (*this);
    loopPlayModeChoice_ = toolChoice (tx ("sm.tp_mode"), { tx ("sm.tp_once"), tx ("sm.tp_heard") }, &loopPlayHeard_);

    // ---- empreintes de volume (rangée d'en-tête du panneau de la forme d'onde)
    auto curveToggle = [this, tool] (const char* key, int bit)
    {
        auto* pl = tool (tx (key), nullptr, [this, bit] { curveMask_ ^= (1 << bit); navPoint_ = -1; });
        return pl;
    };
    peakCurvePill_ = curveToggle ("sm.fp_peak", 0);
    rmsCurvePill_ = curveToggle ("sm.fp_rms", 1);
    lufsCurvePill_ = curveToggle ("sm.fp_lufs", 2);
    loudestPill_ = tool (tx ("sm.fp_loudest"), nullptr, [this] { footprintLoudest(); });
    prevPeakPill_ = tool (tx ("sm.fp_prev"), nullptr, [this] { footprintPeak (-1); });
    nextPeakPill_ = tool (tx ("sm.fp_next"), nullptr, [this] { footprintPeak (+1); });
    rangePill_ = tool (tx ("sm.fp_range"), nullptr, [this] { rangeOn_ = ! rangeOn_; });
    rangeLoNumber_ = add<nova::NumberPill> (1, skin(), tx ("sm.fp_lo").toUpperCase(), -60, 0, [this] { return rangeLo_; }, [this] (int v) { rangeLo_ = std::min (v, rangeHi_); }, [] (int v) { return juce::String (v) + " dB"; });
    rangeHiNumber_ = add<nova::NumberPill> (1, skin(), tx ("sm.fp_hi").toUpperCase(), -60, 0, [this] { return rangeHi_; }, [this] (int v) { rangeHi_ = std::max (v, rangeLo_); }, [] (int v) { return juce::String (v) + " dB"; });
}

// ------------------------------------------------------------------------------------------------ outils du sample
namespace
{
namespace se = neptune::core::sampleedit;

std::filesystem::path editedDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return std::filesystem::u8path (base.getChildFile ("Neptune").getChildFile ("Edited").getFullPathName().toStdString());
}

std::filesystem::path utf8Path (const std::string& s) { return std::filesystem::u8path (s); }

juce::String numberText (double v, int decimals = 1) { return juce::String (v, decimals); }
}

int NovaSamplerEditor::shownIndex() const
{
    const auto* z = shownZone();
    return z == nullptr ? -1 : static_cast<int> (z - cached_.zones.data());
}

// Modifie la seule zone affichée (les outils agissent sur la zone qu'on regarde, pas sur toute la sélection).
void NovaSamplerEditor::editShownZone (const std::function<void (Zone&)>& change)
{
    const int i = shownIndex();
    if (i < 0) return;
    auto d = proc_.instrumentDescription();
    if (i >= static_cast<int> (d.zones.size())) return;
    change (d.zones[static_cast<std::size_t> (i)]);
    d.zones[static_cast<std::size_t> (i)] = sanitised (d.zones[static_cast<std::size_t> (i)]);
    proc_.uiEditInstrument (std::move (d), "Edit sample", "sample-edit");
    lastRevision_ = 0xFFFFFFFFu;
    refreshFromInstrument();
}

// Marqueur de volume : celui de la zone s'il y en a un, sinon celui des métadonnées du fichier (lu une fois, gardé en mémoire).
std::int64_t NovaSamplerEditor::fileMarkerFrame (const std::string& path)
{
    if (path.empty()) return -1;
    const auto it = fileMarkers_.find (path);
    if (it != fileMarkers_.end()) return it->second;
    std::int64_t frame = -1;
    SampleMeta meta;
    const auto& info = fileInfo_.info (path);
    if (info.ok && readSampleMeta (utf8Path (path), meta) && meta.hasVolumeMarker)
        frame = std::clamp<std::int64_t> (static_cast<std::int64_t> (std::llround (meta.volumeMarkerMs * info.sampleRate / 1000.0)), 0, std::max<std::int64_t> (0, info.frames - 1));
    fileMarkers_[path] = frame;
    return frame;
}

std::int64_t NovaSamplerEditor::effectiveMarker (const Zone& z) { return z.releaseRefFrame >= 0 ? z.releaseRefFrame : fileMarkerFrame (z.samplePath); }

// Écrit (ou retire, frame < 0) le marqueur de volume dans les métadonnées du fichier.
void NovaSamplerEditor::saveMarker (const std::string& path, std::int64_t frame)
{
    if (path.empty()) return;
    const auto& info = fileInfo_.info (path);
    if (! info.ok || info.sampleRate <= 0.0) return;
    SampleMeta meta;
    if (frame >= 0) { meta.hasVolumeMarker = true; meta.volumeMarkerMs = static_cast<double> (frame) * 1000.0 / info.sampleRate; }
    MetaStore where = MetaStore::None;
    std::string err;
    if (! writeSampleMeta (utf8Path (path), meta, &where, &err)) { setToolStatus (U (translator().format ("sm.st_marker_error", { err }))); return; }
    fileMarkers_[path] = frame;
    setToolStatus (frame < 0 ? tx ("sm.st_marker_removed") : tx (where == MetaStore::Sidecar ? "sm.st_marker_sidecar" : "sm.st_marker_saved"));
}

bool NovaSamplerEditor::writeDerived (const Zone& z, const char* tag, const se::Buffer& b, std::string& newPath)
{
    std::string err;
    const auto path = se::derivedPath (editedDirectory(), z.samplePath, tag, b);
    if (! std::filesystem::exists (path) && ! se::writeWav (path, b, &err))
    {
        setToolStatus (U (translator().format ("sm.st_error", { err })));
        return false;
    }
    newPath = path.u8string();
    return true;
}

void NovaSamplerEditor::setToolStatus (const juce::String& t) { toolStatus_->setText (t); }

// Passage par zéro le plus proche (dans ±2048 échantillons), lu sur le disque autour du point.
std::int64_t NovaSamplerEditor::snapFrame (const std::string& path, std::int64_t frame) const
{
    constexpr std::int64_t kRadius = 2048;
    AudioStreamReader reader;
    if (! reader.open (utf8Path (path))) return frame;
    const std::int64_t from = std::max<std::int64_t> (0, frame - kRadius), to = std::min (reader.totalFrames(), frame + kRadius);
    if (to - from < 2 || ! reader.seek (from)) return frame;
    se::Buffer b;
    b.channels = reader.channels();
    b.data.resize (static_cast<std::size_t> ((to - from) * b.channels));
    const auto got = reader.read (b.data.data(), to - from);
    b.data.resize (static_cast<std::size_t> (got * b.channels));
    return from + se::snapToZero (b, frame - from, kRadius);
}

void NovaSamplerEditor::toolTrim()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    const std::int64_t total = b.frames(), from = zone.startFrame, to = zone.endFrame < 0 ? total : std::min (zone.endFrame, total);
    if (to - from < 1 || (from == 0 && to == total)) { setToolStatus (tx ("sm.st_nothing_trim")); return; }
    const auto t = se::trimmed (b, from, to);
    std::string newPath;
    if (! writeDerived (zone, "trim", t, newPath)) return;
    const std::int64_t n = t.frames();
    const std::int64_t oldMarker = effectiveMarker (zone), newMarker = (oldMarker >= from && oldMarker < to) ? oldMarker - from : -1;
    editShownZone ([&] (Zone& d)
    {
        d.samplePath = newPath;
        d.releaseRefFrame = newMarker;
        d.startFrame = 0;
        d.endFrame = -1;
        d.loopStart = std::max<std::int64_t> (0, d.loopStart - from);
        if (d.loopEnd >= 0) d.loopEnd = std::clamp<std::int64_t> (d.loopEnd - from, 1, n);
    });
    if (newMarker >= 0) saveMarker (newPath, newMarker);
    setToolStatus (U (translator().format ("sm.st_trimmed", { std::to_string (n) })));
}

void NovaSamplerEditor::toolNormalize()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    static constexpr double targets[6] = { 0.0, -1.0, -3.0, -18.0, -12.0, -9.0 };
    const auto kind = normIndex_ < 3 ? se::NormalizeTarget::Peak : se::NormalizeTarget::Rms;
    const double target = targets[std::clamp (normIndex_, 0, 5)];
    const std::int64_t total = b.frames(), from = zone.startFrame, to = zone.endFrame < 0 ? total : std::min (zone.endFrame, total);
    bool limited = false;
    const double g = se::normalizeGainDb (b, from, to, kind, target, &limited);
    const double peakDb = se::toDb (se::peakAmplitude (b, from, to)), rmsDb = se::toDb (se::rmsAmplitude (b, from, to));
    if (std::fabs (g) < 0.05) { setToolStatus (tx ("sm.st_gain_none")); return; }
    se::applyGainDb (b, g);
    std::string newPath;
    if (! writeDerived (zone, "norm", b, newPath)) return;
    const std::int64_t keptMarker = effectiveMarker (zone);
    editShownZone ([&] (Zone& d) { d.samplePath = newPath; });
    if (keptMarker >= 0) saveMarker (newPath, keptMarker);
    setToolStatus (limited ? U (translator().format ("sm.st_gain_limited", { numberText (g).toStdString() }))
                           : U (translator().format ("sm.st_gain", { numberText (g).toStdString(), numberText (peakDb).toStdString(), numberText (rmsDb).toStdString() })));
}

void NovaSamplerEditor::toolRemoveDc()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    const double dc = se::dcOffset (b);
    const auto text = numberText (dc * 100.0, 2).toStdString() + " %";
    if (std::fabs (dc) < 1.0e-4) { setToolStatus (U (translator().format ("sm.st_dc_none", { text }))); return; }
    se::removeDc (b);
    std::string newPath;
    if (! writeDerived (zone, "dc", b, newPath)) return;
    const std::int64_t keptMarker = effectiveMarker (zone);
    editShownZone ([&] (Zone& d) { d.samplePath = newPath; });
    if (keptMarker >= 0) saveMarker (newPath, keptMarker);
    setToolStatus (U (translator().format ("sm.st_dc", { text })));
}

void NovaSamplerEditor::toolSilence()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const std::string path = z->samplePath;
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (path), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    static constexpr double thresholds[5] = { -30.0, -40.0, -50.0, -60.0, -72.0 };
    const auto r = se::detectSilence (b, thresholds[std::clamp (silenceIndex_, 0, 4)]);
    if (! r.found) { setToolStatus (tx ("sm.st_silence_none")); return; }
    std::int64_t start = r.start, end = r.end;
    if (snapOn_) { start = se::snapToZero (b, start, 2048); end = se::snapToZero (b, end, 2048); }
    const std::int64_t total = b.frames();
    editShownZone ([&] (Zone& d)
    {
        d.startFrame = std::clamp<std::int64_t> (start, 0, total - 1);
        d.endFrame = end >= total ? -1 : std::max (end, d.startFrame + 1);
        d.loopStart = std::max (d.loopStart, d.startFrame);
    });
    setToolStatus (tx ("sm.st_silence"));
}

// Un coup = une zone, sur des notes consécutives à partir de la note racine de la zone coupée.
void NovaSamplerEditor::toolSplitHits()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    const int index = shownIndex();
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    static constexpr double sensitivities[3] = { 0.3, 0.6, 0.9 };
    const std::int64_t total = b.frames(), regionStart = zone.startFrame, regionEnd = zone.endFrame < 0 ? total : std::min (zone.endFrame, total);
    std::vector<std::int64_t> hits;
    for (auto h : se::detectTransients (b, sensitivities[std::clamp (cutsIndex_, 0, 2)], 60.0))
    {
        if (snapOn_) h = se::snapToZero (b, h, 512);
        if (h >= regionStart && h < regionEnd) hits.push_back (h);
    }
    if (hits.empty()) { setToolStatus (tx ("sm.st_cuts_none")); return; }
    const int firstNote = std::clamp (zone.rootNote, 0, 127);
    const bool truncated = static_cast<int> (hits.size()) > 128 - firstNote;
    if (truncated) hits.resize (static_cast<std::size_t> (128 - firstNote));
    std::vector<Zone> made;
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        Zone n = zone;
        n.keyLow = n.keyHigh = n.rootNote = firstNote + static_cast<int> (i);
        n.startFrame = hits[i];
        n.endFrame = i + 1 < hits.size() ? hits[i + 1] : (regionEnd >= total ? -1 : regionEnd);
        n.mode = PlayMode::OneShot;
        n.loopStart = n.startFrame;
        n.loopEnd = -1;
        n.loopCrossfade = 0;
        made.push_back (sanitised (n));
    }
    auto d = proc_.instrumentDescription();
    if (index < 0 || index >= static_cast<int> (d.zones.size())) return;
    d.zones.erase (d.zones.begin() + index);
    d.zones.insert (d.zones.begin() + index, made.begin(), made.end());
    proc_.uiEditInstrument (std::move (d), "Split hits");
    lastRevision_ = 0xFFFFFFFFu;
    const auto n = std::to_string (made.size());
    setToolStatus (U (translator().format ("sm.st_cuts", { n })) + (truncated ? " " + U (translator().format ("sm.st_cuts_truncated", { n })) : juce::String()));
}

// Premier clic : cherche les candidats et applique le meilleur ; clics suivants : candidat suivant.
void NovaSamplerEditor::toolAutoLoop()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    auto options = loopOptions_;
    options.cursor = (cursorFrame_ >= 0 && cursorPath_ == zone.samplePath) ? cursorFrame_ : -1;
    if (options.aroundCursor && options.cursor < 0) { setToolStatus (tx ("sm.st_loop_nocursor")); return; }
    const std::string key = zone.samplePath + "|" + std::to_string (zone.startFrame) + "|" + std::to_string (zone.endFrame) + "|" + std::to_string (editLoop_)
                            + "|" + (options.period ? "p" : "-") + (options.stableOnly ? "s" : "-") + (options.spectral ? "f" : "-") + (options.aroundCursor ? "c" : "-")
                            + std::to_string (options.cursor) + "|" + std::to_string (static_cast<int> (options.radiusMs));
    if (key != loopCandidatesKey_ || loopCandidates_.empty())
    {
        se::Buffer b;
        std::string err;
        if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
        const std::int64_t total = b.frames();
        std::int64_t searchFrom = zone.startFrame;
        if (editLoop_ > 0 && editLoop_ < loopCountOf (zone)) { const auto prev = loopView (zone, editLoop_ - 1); searchFrom = std::max (searchFrom, prev.end < 0 ? total : prev.end); }   // les boucles se suivent dans le fichier
        loopCandidates_ = se::findLoopCandidates (b, searchFrom, zone.endFrame < 0 ? total : std::min (zone.endFrame, total), 6, 100.0, options);
        loopCandidatesKey_ = key;
        loopCandidateIndex_ = 0;
        if (loopCandidates_.empty()) { setToolStatus (tx ("sm.st_loop_none")); return; }
    }
    else loopCandidateIndex_ = (loopCandidateIndex_ + 1) % static_cast<int> (loopCandidates_.size());
    const auto c = loopCandidates_[static_cast<std::size_t> (loopCandidateIndex_)];
    editShownZone ([&] (Zone& d)
    {
        d.mode = PlayMode::Loop;
        auto l = loopView (d, editLoop_);
        l.start = c.start;
        l.end = c.end;
        l.crossfade = 0;
        setLoopView (d, editLoop_, l);
    });
    setToolStatus (U (translator().format ("sm.st_loop", { std::to_string (loopCandidateIndex_ + 1), std::to_string (loopCandidates_.size()),
                                                            std::to_string (static_cast<int> (std::lround (c.score * 100.0))) })));
}

// Étirement et décalage de hauteur : calcul en arrière-plan (plusieurs secondes sur un long fichier), puis nouvelle copie de travail.
// Tous les repères de la zone suivent le changement de durée.
void NovaSamplerEditor::toolStretch()
{
    if (stretchBusy_) return;
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const double ratio = static_cast<double> (stretchPercent_) / 100.0, semis = static_cast<double> (pitchTenths_) / 10.0;
    if (std::fabs (ratio - 1.0) < 1.0e-6 && std::fabs (semis) < 1.0e-9) { setToolStatus (tx ("sm.st_stretch_none")); return; }
    const Zone zone = *z;
    const int index = shownIndex();
    const std::int64_t markerOld = effectiveMarker (zone);
    if (stretchThread_.joinable()) stretchThread_.join();
    stretchCancel_ = false;
    stretchBusy_ = true;
    setToolStatus (tx ("sm.st_stretch_busy"));
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    stretchThread_ = std::thread ([this, safe, zone, index, ratio, semis, markerOld]
    {
        se::Buffer in, out;
        std::string err;
        bool ok = se::readFile (utf8Path (zone.samplePath), in, &err);
        if (ok)
        {
            out = se::stretchAndShift (in, ratio, semis, &stretchCancel_);
            ok = out.frames() > 0;
            if (! ok && err.empty()) err = "cancelled";
        }
        std::string newPath;
        if (ok)
        {
            const auto path = se::derivedPath (editedDirectory(), zone.samplePath, "stretch", out);
            if (! std::filesystem::exists (path)) ok = se::writeWav (path, out, &err);
            newPath = path.u8string();
        }
        const std::int64_t oldFrames = in.frames(), newFrames = out.frames();
        stretchBusy_ = false;
        if (stretchCancel_) return;
        juce::MessageManager::callAsync ([safe, ok, err, newPath, oldFrames, newFrames, zone, index, markerOld]
        {
            if (safe == nullptr) return;
            if (! ok) { safe->setToolStatus (U (safe->translator().format ("sm.st_error", { err }))); return; }
            const double f = static_cast<double> (newFrames) / static_cast<double> (std::max<std::int64_t> (1, oldFrames));
            auto scale = [f] (std::int64_t v) { return v < 0 ? v : static_cast<std::int64_t> (std::llround (static_cast<double> (v) * f)); };
            auto d = safe->proc_.instrumentDescription();
            if (index < 0 || index >= static_cast<int> (d.zones.size()) || d.zones[static_cast<std::size_t> (index)].samplePath != zone.samplePath) return;   // la zone a changé entre-temps
            auto& n = d.zones[static_cast<std::size_t> (index)];
            n.samplePath = newPath;
            n.startFrame = scale (n.startFrame);
            n.endFrame = scale (n.endFrame);
            n.loopStart = scale (n.loopStart);
            n.loopEnd = scale (n.loopEnd);
            n.loopCrossfade = scale (n.loopCrossfade);
            const std::int64_t newMarker = markerOld >= 0 ? scale (markerOld) : -1;
            n.releaseRefFrame = newMarker;
            n = sanitised (n);
            safe->proc_.uiEditInstrument (std::move (d), "Stretch sample", "sample-edit");
            safe->lastRevision_ = 0xFFFFFFFFu;
            if (newMarker >= 0) safe->saveMarker (newPath, newMarker);
            safe->setToolStatus (U (safe->translator().format ("sm.st_stretch", { std::to_string (newFrames) })));
        });
    });
}

// Clic droit dans la forme d'onde : poser un marqueur ici, jouer à partir d'ici, chercher une boucle autour d'ici.
void NovaSamplerEditor::showWaveMenu (std::int64_t frame, juce::Point<int> where)
{
    const auto* z = shownZone();
    if (z == nullptr) return;
    cursorFrame_ = frame;
    cursorPath_ = z->samplePath;
    wave_->setCursor (frame, false);
    const bool volumeOk = true;   // le marqueur de volume est une propriété du sample : posable sur toute zone
    juce::PopupMenu menu;
    menu.addItem (1, tx ("sm.menu_start"));
    menu.addItem (2, tx ("sm.menu_end"));
    menu.addItem (3, tx ("sm.menu_loop_start"), z->mode == PlayMode::Loop);
    menu.addItem (4, tx ("sm.menu_loop_end"), z->mode == PlayMode::Loop);
    menu.addSeparator();
    menu.addItem (5, tx ("sm.menu_volume"), volumeOk);
    menu.addItem (6, tx ("sm.menu_volume_clear"), volumeOk && effectiveMarker (*z) >= 0);
    menu.addSeparator();
    menu.addItem (7, tx ("sm.menu_play"));
    menu.addItem (8, tx ("sm.menu_loop_around"), z->mode == PlayMode::Loop);
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (where.x, where.y, 1, 1)), [safe, frame] (int r)
    {
        if (safe == nullptr || r == 0) return;
        using M = nova::WaveView::Marker;
        switch (r)
        {
            case 1: safe->applyMarker_ (M::Start, frame); break;
            case 2: safe->applyMarker_ (M::End, frame); break;
            case 3: safe->applyMarker_ (M::LoopStart, frame); break;
            case 4: safe->applyMarker_ (M::LoopEnd, frame); break;
            case 5:
            {
                safe->applyMarker_ (M::Volume, frame);
                safe->volumeDragged_ = false;
                if (const auto* zz = safe->shownZone()) safe->saveMarker (zz->samplePath, zz->releaseRefFrame);
                break;
            }
            case 6:
            {
                const auto* zz = safe->shownZone();
                const std::string path = zz != nullptr ? zz->samplePath : std::string();
                safe->editZone ([] (Zone& zone) { zone.releaseRefFrame = -1; });
                if (! path.empty()) safe->saveMarker (path, -1);
                break;
            }
            case 7: safe->startPreview (3, frame, 0, 1); break;
            case 8:
            {
                const bool was = safe->loopOptions_.aroundCursor;
                safe->loopOptions_.aroundCursor = true;
                safe->toolAutoLoop();
                safe->loopOptions_.aroundCursor = was;
                break;
            }
            default: break;
        }
    });
}

void NovaSamplerEditor::openLoopOptions (juce::Component& anchor)
{
    auto panel = std::make_unique<LoopOptionsPanel> (skin(), translator(), loopOptions_);
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    panel->onChange = [safe] (const se::LoopSearchOptions& o) { if (safe != nullptr) { safe->loopOptions_ = o; safe->loopCandidates_.clear(); } };
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

// Menu du bouton « + BOUCLE » : une boucle vide, la suivante trouvée automatiquement, ou toutes les boucles automatiques d'un coup.
void NovaSamplerEditor::showAddLoopMenu()
{
    const auto* z = shownZone();
    if (z == nullptr) return;
    const bool room = loopCountOf (*z) <= static_cast<int> (ZoneDescription::maxExtraLoops);
    juce::PopupMenu menu;
    menu.addItem (1, tx ("sm.add_empty"), room);
    menu.addItem (2, tx ("sm.add_next_auto"), room);
    menu.addItem (3, tx ("sm.add_all_auto"), room);
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loopAddPill_), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) safe->addLoop();
        else if (r == 2) { safe->addLoop(); safe->toolAutoLoop(); }   // la nouvelle boucle est sélectionnée : la recherche part de la fin de la précédente
        else safe->addAllAutoLoops();
    });
}

// Toutes les boucles automatiques trouvées deviennent des boucles de la zone (dans l'ordre du fichier, dans la limite des quatre), les nouvelles boucles
// qui en précèdent une autre reçoivent 2 retours par défaut pour que la lecture avance de l'une à l'autre.
void NovaSamplerEditor::addAllAutoLoops()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const Zone zone = *z;
    se::Buffer b;
    std::string err;
    if (! se::readFile (utf8Path (zone.samplePath), b, &err)) { setToolStatus (U (translator().format ("sm.st_error", { err }))); return; }
    const std::int64_t total = b.frames();
    auto options = loopOptions_;
    options.cursor = (cursorFrame_ >= 0 && cursorPath_ == zone.samplePath) ? cursorFrame_ : -1;
    if (options.aroundCursor && options.cursor < 0) options.aroundCursor = false;
    auto found = se::findLoopCandidates (b, zone.startFrame, zone.endFrame < 0 ? total : std::min (zone.endFrame, total), 6, 100.0, options);
    if (found.empty()) { setToolStatus (tx ("sm.st_loop_none")); return; }
    std::sort (found.begin(), found.end(), [] (const se::LoopCandidate& a, const se::LoopCandidate& c) { return a.end < c.end; });
    std::vector<LoopRegion> loops;
    for (int i = 0; i < loopCountOf (zone); ++i) loops.push_back (loopView (zone, i));
    int added = 0;
    for (const auto& c : found)
    {
        if (static_cast<int> (loops.size()) > static_cast<int> (ZoneDescription::maxExtraLoops)) break;
        const auto& last = loops.back();
        const std::int64_t lastEnd = last.end < 0 ? total : last.end;
        const std::int64_t gap = static_cast<std::int64_t> (b.sampleRate * 0.02);
        if (c.end <= lastEnd + gap || c.start < last.start) continue;   // les boucles se suivent dans le fichier
        LoopRegion l;
        l.start = c.start; l.end = c.end;
        loops.push_back (l);
        ++added;
    }
    if (added == 0) { setToolStatus (tx ("sm.st_loops_none_added")); return; }
    editShownZone ([&] (Zone& d)
    {
        d.mode = PlayMode::Loop;
        d.extraLoops.assign (loops.begin() + 1, loops.end());
        d.loopStart = loops.front().start; d.loopEnd = loops.front().end; d.loopCrossfade = loops.front().crossfade; d.loopRepeats = loops.front().repeats;
        for (int i = 0; i + 1 < static_cast<int> (loops.size()); ++i)
        {
            auto l = loopView (d, i);
            if (l.repeats == 0) { l.repeats = 2; setLoopView (d, i, l); }
        }
    });
    setToolStatus (U (translator().format ("sm.st_loops_all_added", { std::to_string (added) })));
}

// Ajoute une boucle (4 au plus) à la suite de la dernière, jusqu'à la fin de la région jouée.
void NovaSamplerEditor::addLoop()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    if (loopCountOf (*z) > static_cast<int> (ZoneDescription::maxExtraLoops)) { setToolStatus (tx ("sm.st_loops_max")); return; }
    const auto& info = fileInfo_.info (z->samplePath);
    const std::int64_t total = info.ok ? info.frames : 0;
    const auto last = loopView (*z, loopCountOf (*z) - 1);
    const std::int64_t lastEnd = last.end < 0 ? total : last.end, regionEnd = z->endFrame < 0 ? total : z->endFrame;
    if (regionEnd - lastEnd < 200) { setToolStatus (tx ("sm.st_loops_room")); return; }
    const std::int64_t start = lastEnd, end = regionEnd >= total ? -1 : regionEnd;
    const int newIndex = loopCountOf (*z);
    editShownZone ([&] (Zone& d)
    {
        d.mode = PlayMode::Loop;
        LoopRegion l;
        l.start = start; l.end = end;
        d.extraLoops.push_back (l);
        // la boucle qui précède doit rendre la main : sans retours finis, elle tournerait sans fin et la nouvelle ne serait jamais jouée
        auto prev = loopView (d, loopCountOf (d) - 2);
        if (prev.repeats == 0) { prev.repeats = 2; setLoopView (d, loopCountOf (d) - 2, prev); }
    });
    editLoop_ = newIndex;
    setToolStatus (tx ("sm.st_loops_added"));
}

void NovaSamplerEditor::removeLoop()
{
    const auto* z = shownZone();
    if (z == nullptr || loopCountOf (*z) < 2) return;
    const int idx = std::clamp (editLoop_, 0, loopCountOf (*z) - 1);
    editShownZone ([idx] (Zone& d)
    {
        if (idx == 0)
        {
            const auto next = d.extraLoops.front();
            d.loopStart = next.start; d.loopEnd = next.end; d.loopCrossfade = next.crossfade; d.loopRepeats = next.repeats;
            d.extraLoops.erase (d.extraLoops.begin());
        }
        else d.extraLoops.erase (d.extraLoops.begin() + (idx - 1));
    });
    editLoop_ = std::max (0, idx - 1);
}

void NovaSamplerEditor::toolSaveAs()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty()) return;
    const std::string source = z->samplePath;
    const auto start = juce::File (juce::String::fromUTF8 (source.c_str())).withFileExtension ("wav");
    saveChooser_ = std::make_unique<juce::FileChooser> (tx ("sm.tool_saveas"), start, "*.wav");
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    saveChooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                               [safe, source] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (safe == nullptr || file == juce::File()) return;
        std::error_code ec;
        const auto target = utf8Path (file.withFileExtension ("wav").getFullPathName().toStdString());
        std::filesystem::copy_file (utf8Path (source), target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) { safe->setToolStatus (U (safe->translator().format ("sm.st_error", { ec.message() }))); return; }
        const std::string path = target.u8string();
        safe->editShownZone ([&] (Zone& d) { d.samplePath = path; });
        safe->setToolStatus (safe->tx ("sm.st_saved"));
    });
}

// ---- empreintes de volume
int NovaSamplerEditor::primaryCurve() const
{
    if ((curveMask_ & 4) != 0 && footprint_ != nullptr && footprint_->hasLufs()) return 2;
    if ((curveMask_ & 2) != 0) return 1;
    if ((curveMask_ & 1) != 0) return 0;
    return (curveMask_ & 4) != 0 ? 1 : -1;
}

// Calcul en arrière-plan, une seule fois par fichier ; recalculé quand la zone affichée change de fichier.
void NovaSamplerEditor::updateFootprint()
{
    const auto* z = shownZone();
    const std::string path = (z != nullptr && curveMask_ != 0) ? z->samplePath : std::string();
    if (footprintJob_.valid() && footprintJob_.wait_for (std::chrono::seconds (0)) == std::future_status::ready)
    {
        auto fp = footprintJob_.get();
        if (path == footprintPath_ && fp != nullptr && fp->ok) footprint_ = std::move (fp);
    }
    if (path != footprintPath_)
    {
        if (footprintJob_.valid()) { footprintCancel_ = true; footprintJob_.wait(); }
        footprintCancel_ = false;
        footprintPath_ = path;
        footprint_.reset();
        navPoint_ = -1;
        if (! path.empty())
            footprintJob_ = std::async (std::launch::async, [this, path]
            {
                return std::shared_ptr<const VolumeFootprint> (std::make_shared<VolumeFootprint> (computeFootprint (std::filesystem::u8path (path), &footprintCancel_)));
            });
    }
    wave_->setFootprint (footprint_, curveMask_);
    const int kind = primaryCurve();
    wave_->setRange (rangeOn_ && kind >= 0, rangeLo_, rangeHi_, kind);
    peakCurvePill_->setOn ((curveMask_ & 1) != 0);
    rmsCurvePill_->setOn ((curveMask_ & 2) != 0);
    lufsCurvePill_->setOn ((curveMask_ & 4) != 0);
    lufsCurvePill_->setAlpha (footprint_ != nullptr && ! footprint_->hasLufs() ? 0.4f : 1.0f);
    rangePill_->setOn (rangeOn_);
    rangeLoNumber_->update(); rangeHiNumber_->update();
    rangeLoNumber_->setActive (rangeOn_); rangeHiNumber_->setActive (rangeOn_);
    for (auto* pl : { loudestPill_, prevPeakPill_, nextPeakPill_ }) pl->setAlpha (footprint_ != nullptr ? 1.0f : 0.4f);
}

void NovaSamplerEditor::footprintLoudest()
{
    const auto* z = shownZone();
    const int kind = primaryCurve();
    if (z == nullptr) return;
    if (kind < 0) { setToolStatus (tx ("sm.st_fp_nocurve")); return; }
    if (footprint_ == nullptr) { setToolStatus (tx ("sm.st_fp_wait")); return; }
    const auto& fp = *footprint_;
    const auto& cv = fp.curve (kind);
    const int from = static_cast<int> (fp.pointOfFrame (z->startFrame)), to = z->endFrame < 0 ? static_cast<int> (cv.size()) : static_cast<int> (fp.pointOfFrame (z->endFrame)) + 1;
    const int i = loudestPoint (cv, from, to);
    if (i < 0) return;
    navPoint_ = i;
    cursorFrame_ = fp.frameOfPoint (static_cast<std::size_t> (i));
    wave_->setCursor (cursorFrame_, true);
    setToolStatus (U (translator().format ("sm.st_fp_loudest", { numberText (cv[static_cast<std::size_t> (i)]).toStdString(), numberText (static_cast<double> (fp.frameOfPoint (static_cast<std::size_t> (i))) / fp.sampleRate, 3).toStdString() })));
}

void NovaSamplerEditor::footprintPeak (int direction)
{
    const auto* z = shownZone();
    const int kind = primaryCurve();
    if (z == nullptr) return;
    if (kind < 0) { setToolStatus (tx ("sm.st_fp_nocurve")); return; }
    if (footprint_ == nullptr) { setToolStatus (tx ("sm.st_fp_wait")); return; }
    const auto& fp = *footprint_;
    const auto& cv = fp.curve (kind);
    const auto peaks = localPeaks (cv, 3.0, static_cast<int> (std::lround (fp.sampleRate / static_cast<double> (fp.hopFrames))));
    const int start = navPoint_ >= 0 ? navPoint_ : static_cast<int> (fp.pointOfFrame (z->startFrame)) - (direction > 0 ? 1 : -1);
    const int i = neighbourPeak (peaks, start, direction);
    if (i < 0) { setToolStatus (tx ("sm.st_fp_nopeak")); return; }
    navPoint_ = i;
    cursorFrame_ = fp.frameOfPoint (static_cast<std::size_t> (i));
    wave_->setCursor (cursorFrame_, true);
    setToolStatus (U (translator().format ("sm.st_fp_peak", { numberText (cv[static_cast<std::size_t> (i)]).toStdString(), numberText (static_cast<double> (fp.frameOfPoint (static_cast<std::size_t> (i))) / fp.sampleRate, 3).toStdString() })));
}

// Raccord de boucle : quelques millisecondes autour des points de boucle, lues sur le disque (seulement quand la boucle change).
void NovaSamplerEditor::updateSeam()
{
    const auto* z = shownZone();
    std::string key;
    neptune::core::AudioFileInfo info;
    if (z != nullptr && ! z->samplePath.empty()) info = fileInfo_.info (z->samplePath);
    const auto el = z != nullptr ? loopView (*z, editLoop_) : LoopRegion {};
    if (z != nullptr && z->mode == PlayMode::Loop && info.ok)
        key = z->samplePath + "|" + std::to_string (std::max (el.start, z->startFrame)) + "|" + std::to_string (el.end < 0 ? info.frames : el.end);
    if (key == seamKey_) return;
    seamKey_ = key;
    if (key.empty()) { seam_->setData ({}, {}, {}); return; }
    const std::int64_t S = std::max (el.start, z->startFrame), E = el.end < 0 ? info.frames : std::min (el.end, info.frames);
    const std::int64_t w = std::min<std::int64_t> ({ std::clamp<std::int64_t> (static_cast<std::int64_t> (info.sampleRate * 0.01), 64, 2048), S, E - S, info.frames - S });
    AudioStreamReader reader;
    if (w < 8 || ! reader.open (utf8Path (z->samplePath))) { seam_->setData ({}, {}, {}); return; }
    auto grab = [&] (std::int64_t from)
    {
        std::vector<float> out;
        std::vector<float> buf (static_cast<std::size_t> (w * reader.channels()));
        if (! reader.seek (from) || reader.read (buf.data(), w) < w) return out;
        for (std::int64_t i = 0; i < w; ++i)
        {
            float m = 0.0f;
            for (int c = 0; c < reader.channels(); ++c) m += buf[static_cast<std::size_t> (i * reader.channels() + c)];
            out.push_back (m / static_cast<float> (reader.channels()));
        }
        return out;
    };
    seam_->setData (grab (E - w), grab (S), grab (S - w));
}

void NovaSamplerEditor::buildSoundPage()
{
    envPanel_ = add<nova::Panel> (2, skin());
    envPanel_->setTitle (tx ("sm.env").toUpperCase());
    addInfo (2, envPanel_, "hlp.sm_env", 1);
    env_ = add<EnvelopeEditor> (2, skin().palette(), translator());
    env_->onGestureBegin = [this] { proc_.uiBeginGesture(); };
    env_->onGestureEnd = [this] { proc_.uiEndGesture(); };
    env_->onChange = [this] (const EnvelopeDescription& e) { editZone ([e] (Zone& z) { z.envelope = e; }); };

    filterPanel_ = add<nova::Panel> (2, skin());
    filterPanel_->setTitle (tx ("sm.filters").toUpperCase());
    addInfo (2, filterPanel_, "hlp.sm_filters", 1);
    const int tr = proc_.selectedTrack();
    const struct { mx::SamplerKind kind; const char* key; } fk[] = { { mx::kFilter1Cutoff, "sm.f1_cutoff" }, { mx::kFilter1Resonance, "sm.f1_res" },
                                                                    { mx::kFilter2Cutoff, "sm.f2_cutoff" }, { mx::kFilter2Resonance, "sm.f2_res" } };
    for (const auto& f : fk)
    {
        auto* k = add<nova::KnobCard> (2, skin(), host(), mx::samplerParam (tr, f.kind), tx (f.key), modalOpener());
        k->setPlain (true);
        filterKnobs_.push_back (k);
    }
    filtersPill_ = add<nova::Pill> (2, skin(), nova::Pill::Look::Small);
    filtersPill_->setText (tx ("sm.filters_open"));
    filtersPill_->onClick = [this] { openFilterEditor(); };

    slotsAreaPanel_ = add<nova::Panel> (2, skin());
    slotsAreaPanel_->setTitle (tx ("sm.slots").toUpperCase());
    addInfo (2, slotsAreaPanel_, "hlp.sm_slots", 1);
    slotsPill_ = add<nova::Pill> (2, skin(), nova::Pill::Look::Small);
    slotsPill_->setText (tx ("slots.button"));
    slotsPill_->onClick = [this] { openSlotsPanel (*slotsPill_); };
    slotsText_ = add<nova::Text> (2, skin(), Token::TextDim, 11.0f);
}

void NovaSamplerEditor::buildInstrumentPage()
{
    velPanel_ = add<nova::Panel> (3, skin());
    velPanel_->setTitle (tx ("sm.velocity").toUpperCase());
    addInfo (3, velPanel_, "hlp.sm_vel", 1);
    {
        const auto d = proc_.instrumentDescription();
        velocity_ = add<VelocityPanel> (3, skin().palette(), translator(), d.velocityFadeWidth, d.velocityFadeLinear, d.velocityCurve);
    }
    velocity_->onGestureBegin = [this] { proc_.uiBeginGesture(); };
    velocity_->onGestureEnd = [this] { proc_.uiEndGesture(); };
    velocity_->onUndo = [this] { proc_.uiUndo(); };
    velocity_->onRedo = [this] { proc_.uiRedo(); };
    velocity_->onChange = [this] (int width, bool linear, const VelocityCurve& curve)
    {
        auto desc = proc_.instrumentDescription();
        desc.velocityFadeWidth = width;
        desc.velocityFadeLinear = linear;
        desc.velocityCurve = curve;
        proc_.uiEditInstrument (std::move (desc), "Velocity response", "velocity");
    };

    polyPanel_ = add<nova::Panel> (3, skin());
    polyPanel_->setTitle (tx ("sm.poly").toUpperCase());
    addInfo (3, polyPanel_, "hlp.sm_poly", 1);
    auto fromDescription = [] (const InstrumentDescription& d)
    {
        PolyphonyPanel::Settings s;
        s.mode = d.polyphony; s.maxVoices = d.maxVoices; s.steal = d.stealMode; s.sameKey = d.sameKey;
        s.cutFadeMs = d.cutFadeMs; s.glideMs = d.glideMs; s.chokeSameZone = d.chokeSameZone;
        return s;
    };
    poly_ = add<PolyphonyPanel> (3, skin().palette(), translator(), fromDescription (proc_.instrumentDescription()));
    poly_->onGestureBegin = [this] { proc_.uiBeginGesture(); };
    poly_->onGestureEnd = [this] { proc_.uiEndGesture(); };
    poly_->onUndo = [this] { proc_.uiUndo(); };
    poly_->onRedo = [this] { proc_.uiRedo(); };
    poly_->onChange = [this] (const PolyphonyPanel::Settings& s)
    {
        auto desc = proc_.instrumentDescription();
        desc.polyphony = s.mode; desc.maxVoices = s.maxVoices; desc.stealMode = s.steal; desc.sameKey = s.sameKey;
        desc.cutFadeMs = s.cutFadeMs; desc.glideMs = s.glideMs; desc.chokeSameZone = s.chokeSameZone;
        proc_.uiEditInstrument (std::move (desc), "Polyphony", "polyphony");
    };

    memPanel_ = add<nova::Panel> (3, skin());
    memPanel_->setTitle (tx ("sm.memory").toUpperCase());
    addInfo (3, memPanel_, "hlp.sm_mem", 1);
    rrToggle_ = add<juce::ToggleButton> (3);
    releaseToggle_ = add<juce::ToggleButton> (3);
    streamToggle_ = add<juce::ToggleButton> (3);
    rrToggle_->onClick = [this]
    {
        auto d = proc_.instrumentDescription();
        d.mainRoundRobinOnly = ! rrToggle_->getToggleState();   // coché = les alternatives sont chargées
        proc_.uiEditInstrument (std::move (d), "Load round-robin alternates");
    };
    releaseToggle_->onClick = [this]
    {
        auto d = proc_.instrumentDescription();
        d.loadReleaseSamples = releaseToggle_->getToggleState();
        proc_.uiEditInstrument (std::move (d), "Load release samples");
    };
    streamToggle_->onClick = [this]
    {
        auto d = proc_.instrumentDescription();
        d.streamFromDisk = streamToggle_->getToggleState();
        proc_.uiEditInstrument (std::move (d), "Stream from disk");
    };
    preloadLabel_ = add<juce::Label> (3);
    preloadLabel_->setText (tx ("load.preload"), juce::dontSendNotification);
    preloadBox_ = add<juce::ComboBox> (3);
    for (const int kb : { 32, 64, 128, 256, 512, 1024, 2048, 4096 }) preloadBox_->addItem (juce::String (kb) + " KB", kb);
    preloadBox_->onChange = [this]
    {
        if (updating_) return;
        auto d = proc_.instrumentDescription();
        d.preloadKB = preloadBox_->getSelectedId();
        proc_.uiEditInstrument (std::move (d), "Preload size");
    };
    groupsViewport_ = add<juce::Viewport> (3);
    groupsViewport_->setViewedComponent (&groupsHolder_, false);
}

// ------------------------------------------------------------------------------------------------ zones et édition
std::vector<int> NovaSamplerEditor::selection() const { return zones_->selectedRows(); }

void NovaSamplerEditor::setSelection (const std::vector<int>& z)
{
    zones_->setSelectedRows (z, false);
    matrix_->setSelection (zones_->selectedRows());
}

const ZoneDescription* NovaSamplerEditor::shownZone() const
{
    if (cached_.zones.empty()) return nullptr;
    const auto sel = zones_->selectedRows();
    const auto i = (! sel.empty() && sel.front() >= 0 && sel.front() < static_cast<int> (cached_.zones.size())) ? static_cast<std::size_t> (sel.front()) : 0u;
    return &cached_.zones[i];
}

// Applique un changement aux zones sélectionnées (toutes si rien n'est sélectionné) et republie l'instrument.
void NovaSamplerEditor::editZone (const std::function<void (Zone&)>& change)
{
    auto d = proc_.instrumentDescription();
    if (d.zones.empty()) return;
    const auto sel = zones_->selectedRows();
    if (sel.empty())
        for (auto& z : d.zones) change (z);
    else
        for (const int row : sel)
            if (row >= 0 && row < static_cast<int> (d.zones.size())) change (d.zones[static_cast<std::size_t> (row)]);
    for (auto& z : d.zones) z = sanitised (z);
    proc_.uiEditInstrument (std::move (d), "Edit zone", "zone");
    lastRevision_ = 0xFFFFFFFFu;
    refreshFromInstrument();
}

double NovaSamplerEditor::sampleRate() const
{
    const auto* z = shownZone();
    if (z == nullptr) return 48000.0;
    const auto& info = const_cast<neptune::core::memory::FileInfoCache&> (fileInfo_).info (z->samplePath);
    return info.ok && info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
}

juce::String NovaSamplerEditor::noteName (int note) const { return U (import::midiToNoteName (note, import::NoteConvention {})); }

void NovaSamplerEditor::removeSelectedZones()
{
    auto sel = selection();
    if (sel.empty()) return;
    auto d = proc_.instrumentDescription();
    d.removeZones (sel);   // les emplacements qui visaient ces zones sont libérés, les autres suivent le décalage
    proc_.uiEditInstrument (std::move (d), "Remove zones");
    lastRevision_ = 0xFFFFFFFFu;
    refreshFromInstrument();
}

void NovaSamplerEditor::chooseSample()
{
    chooser_ = std::make_unique<juce::FileChooser> (tx ("sampler.choose"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<NovaSamplerEditor> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return;
                               // Un seul sample : on garde les réglages de la première zone et on remplace le reste.
                               auto d = safe->proc_.instrumentDescription();
                               ZoneDescription z = d.zones.empty() ? ZoneDescription {} : d.zones.front();
                               z.samplePath = file.getFullPathName().toStdString();
                               z.keyLow = 0; z.keyHigh = 127; z.velLow = 1; z.velHigh = 127; z.roundRobin = 0; z.group.clear(); z.onRelease = false;
                               // compensation : le niveau du nouveau fichier est comparé à celui de l'ancien sur les mêmes 100 premières ms de la région jouée
                               double oldDb = 0.0, newDb = 0.0, delta = 0.0;
                               const bool compensate = d.compensateOnReplace && ! d.zones.empty() && ! d.zones.front().samplePath.empty()
                                                       && windowLevelDb (d.zones.front().samplePath, d.zones.front().startFrame, 100.0, oldDb)
                                                       && windowLevelDb (z.samplePath, 0, 100.0, newDb);
                               if (compensate) delta = std::clamp (oldDb - newDb, -24.0, 24.0);
                               z.startFrame = 0; z.endFrame = -1; z.loopStart = 0; z.loopEnd = -1; z.loopCrossfade = 0;   // les repères de l'ancien fichier n'ont plus de sens
                               z.extraLoops.clear(); z.loopRepeats = 0;
                               if (compensate) z.gainDb = std::clamp (z.gainDb + delta, -60.0, 60.0);
                               d.zones.assign (1, z);
                               safe->proc_.uiEditInstrument (std::move (d), "Replace sample");
                               safe->lastRevision_ = 0xFFFFFFFFu;
                               if (compensate) safe->setToolStatus (U (safe->translator().format ("sm.st_compensated", { numberText (delta).toStdString() })));
                           });
}

void NovaSamplerEditor::startImport()
{
    juce::PopupMenu menu;
    menu.addItem (1, tx ("sampler.import_files"));
    menu.addItem (2, tx ("sampler.import_folder"));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (importPill_), [safe = juce::Component::SafePointer<NovaSamplerEditor> (this)] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        auto* self = safe.getComponent();
        const bool folder = choice == 2;
        // Choix d'un dossier : AUCUN filtre (la boîte système l'appliquerait aux dossiers eux-mêmes) ; les fichiers audio sont filtrés ensuite.
        self->importChooser_ = std::make_unique<juce::FileChooser> (self->tx ("sampler.choose"), juce::File(), folder ? juce::String ("*") : juce::String ("*.wav;*.aif;*.aiff;*.flac"));
        const int flags = folder ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                 : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems;
        self->importChooser_->launchAsync (flags, [safe, folder] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            std::vector<std::string> paths;
            if (folder)
            {
                const auto dir = fc.getResult();
                if (! dir.isDirectory()) return;
                for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac")) paths.push_back (f.getFullPathName().toStdString());
            }
            else
                for (const auto& f : fc.getResults()) paths.push_back (f.getFullPathName().toStdString());
            if (! paths.empty()) safe->openWizard (std::move (paths));
        });
    });
}

void NovaSamplerEditor::openWizard (std::vector<std::string> paths)
{
    if (wizard_) return;
    const auto saved = proc_.lastImportTemplate();
    auto templ = saved.empty() ? import::Template {} : import::Template::fromJson (saved);
    wizard_ = std::make_unique<ImportWizard> (std::move (paths), skin().palette(), translator(), std::move (templ), proc_.instrumentDescription(), fileInfo_,
        memory::availablePhysicalBytes(),
        [safe = juce::Component::SafePointer<NovaSamplerEditor> (this)] (const ImportWizard::Result& r)
        {
            if (safe == nullptr) return;
            safe->applyImport (r);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeWizard(); });
        },
        [safe = juce::Component::SafePointer<NovaSamplerEditor> (this)]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeWizard(); });
        });
    addAndMakeVisible (*wizard_);
    wizard_->setBounds (getLocalBounds());
    wizard_->toFront (true);
}

void NovaSamplerEditor::closeWizard()
{
    wizard_.reset();
    grabKeyboardFocus();
}

void NovaSamplerEditor::applyImport (const ImportWizard::Result& r)
{
    proc_.setLastImportTemplate (r.templ.toJson());
    auto d = proc_.instrumentDescription();
    if (r.replace)
    {
        std::vector<int> all (d.zones.size());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int> (i);
        d.removeZones (std::move (all));
    }
    for (const auto& z : r.zones) d.zones.push_back (z);
    d.mainRoundRobinOnly = r.replace ? r.mainRoundRobinOnly : (d.mainRoundRobinOnly || r.mainRoundRobinOnly);
    d.loadReleaseSamples = r.replace ? r.loadReleaseSamples : (d.loadReleaseSamples && r.loadReleaseSamples);
    d.streamFromDisk = r.replace ? r.streamFromDisk : (d.streamFromDisk || r.streamFromDisk);
    d.roundRobinMode = r.roundRobinMode;
    proc_.uiEditInstrument (std::move (d), "Import samples");
    lastRevision_ = 0xFFFFFFFFu;
}

// Choke avancé (coupe / est coupée par) des zones sélectionnées.
void NovaSamplerEditor::openChokePanel (juce::Component& anchor)
{
    const auto* z = shownZone();
    if (z == nullptr) return;
    auto panel = std::make_unique<ChokePanel> (skin().palette(), translator(), z->chokeCuts, z->chokeCutBy);
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    panel->onUndo = [safe] { if (safe != nullptr) safe->proc_.uiUndo(); };
    panel->onRedo = [safe] { if (safe != nullptr) safe->proc_.uiRedo(); };
    panel->onChange = [safe] (int cuts, int cutBy) { if (safe != nullptr) safe->editZone ([cuts, cutBy] (Zone& zone) { zone.chokeCuts = cuts; zone.chokeCutBy = cutBy; }); };
    chokePanel_ = panel.get();
    juce::Component::SafePointer<ChokePanel> focus (panel.get());
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
    juce::MessageManager::callAsync ([focus] { if (focus != nullptr) focus->grabKeyboardFocus(); });
}

void NovaSamplerEditor::openLevelsPanel (juce::Component& anchor)
{
    const auto* z = shownZone();
    auto panel = std::make_unique<LevelsPanel> (skin(), translator(), cached_.compensateOnReplace, z);
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    panel->onCompensate = [safe] (bool on)
    {
        if (safe == nullptr) return;
        auto d = safe->proc_.instrumentDescription();
        d.compensateOnReplace = on;
        safe->proc_.uiEditInstrument (std::move (d), "Compensate on replace");
        safe->lastRevision_ = 0xFFFFFFFFu;
    };
    panel->onRelease = [safe] (bool follow, int meter, double floorDb, double curve, double offsetDb)
    {
        if (safe == nullptr) return;
        safe->editZone ([=] (Zone& zone) { zone.releaseFollowsLevel = follow; zone.releaseMeter = meter; zone.releaseFloorDb = floorDb; zone.releaseCurve = curve; zone.releaseOffsetDb = offsetDb; });
    };
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

// Emplacements assignables : fenêtre flottante (les 16 lignes) ou menu contextuel d'une zone / d'un Groupe.
void NovaSamplerEditor::openSlotsPanel (juce::Component& anchor)
{
    auto panel = std::make_unique<SlotsPanel> (proc_, skin().palette(), translator(), proc_.selectedTrack());
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    panel->getDescription = [safe] { return safe != nullptr ? safe->proc_.instrumentDescription() : InstrumentDescription {}; };
    panel->getSelectedZones = [safe] { return safe != nullptr ? safe->zones_->selectedRows() : std::vector<int> {}; };
    panel->onEdit = [safe] (InstrumentDescription d) { if (safe != nullptr) safe->proc_.uiEditInstrument (std::move (d), "Slots", "slots"); };
    slotsPanel_ = panel.get();
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

void NovaSamplerEditor::showSlotAssignMenu (int zone, const std::string& group)
{
    const auto d = proc_.instrumentDescription();
    if (group.empty() && (zone < 0 || zone >= static_cast<int> (d.zones.size()))) return;
    static constexpr int kOpenWindow = 100000;
    juce::PopupMenu menu;
    menu.addSectionHeader (group.empty() ? tx ("slots.zone") + " " + juce::String (zone + 1) : tx ("slots.group") + " " + U (group));
    for (int s = 0; s < kZoneSlots; ++s)
    {
        juce::PopupMenu sub;
        const auto& cur = d.slots[static_cast<std::size_t> (s)];
        const bool same = cur.assigned() && (group.empty() ? (! cur.isGroup && cur.zone == zone) : (cur.isGroup && cur.group == group));
        for (int p = 1; p < kSlotParams; ++p)
        {
            static const char* const keys[kSlotParams] = { "", "slots.param_attack", "slots.param_decay", "slots.param_sustain", "slots.param_release", "slots.param_level", "slots.param_tune" };
            sub.addItem (1 + s * 16 + p, tx (keys[p]), true, same && static_cast<int> (cur.param) == p);
        }
        if (same) { sub.addSeparator(); sub.addItem (1 + s * 16, tx ("slots.free_slot")); }
        juce::String title = tx ("slots.slot") + " " + juce::String (s + 1);
        if (cur.assigned() && ! same) title += "  (" + tx ("slots.taken") + ")";
        menu.addSubMenu (title, sub, true, nullptr, same);
    }
    menu.addSeparator();
    menu.addItem (kOpenWindow, tx ("slots.open_window"));
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safe, zone, group] (int choice)
    {
        if (safe == nullptr || choice == 0) return;
        if (choice == kOpenWindow) { safe->openSlotsPanel (*safe->slotsPill_); return; }
        const int slot = (choice - 1) / 16, param = (choice - 1) % 16;
        auto desc = safe->proc_.instrumentDescription();
        if (slot < 0 || slot >= kZoneSlots) return;
        auto& a = desc.slots[static_cast<std::size_t> (slot)];
        if (param == 0) a = SlotAssignment {};
        else
        {
            a = SlotAssignment {};
            a.param = static_cast<SlotParam> (param);
            if (group.empty()) a.zone = zone; else { a.isGroup = true; a.group = group; }
        }
        safe->proc_.uiEditInstrument (std::move (desc), "Slots", "slots");
    });
}

// ------------------------------------------------------------------------------------------------ filtres (calque)
void NovaSamplerEditor::openFilterEditor()
{
    if (filterEditor_ || wizard_) return;
    filterEditor_ = std::make_unique<FilterEditor> (skin().palette(), translator(), skin().knobStyle());
    juce::Component::SafePointer<NovaSamplerEditor> safe (this);
    filterEditor_->onGestureBegin = [safe] { if (safe != nullptr) safe->proc_.uiBeginGesture(); };
    filterEditor_->onGestureEnd = [safe] { if (safe != nullptr) safe->proc_.uiEndGesture(); };
    filterEditor_->onUndo = [safe] { if (safe != nullptr) safe->proc_.uiUndo(); };
    filterEditor_->onRedo = [safe] { if (safe != nullptr) safe->proc_.uiRedo(); };
    filterEditor_->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeFilterEditor(); }); };
    filterEditor_->onDefaultChanged = [safe] (const FilterBlock& b)
    {
        if (safe == nullptr) return;
        auto d = safe->proc_.instrumentDescription();
        d.defaultFilters = b;
        safe->proc_.uiEditInstrument (std::move (d), "Filters", "filter");
    };
    filterEditor_->onZonesChanged = [safe] (bool overrideOn, const FilterBlock& b)
    {
        if (safe == nullptr) return;
        auto d = safe->proc_.instrumentDescription();
        for (const int r : safe->zones_->selectedRows())
        {
            if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
            auto& z = d.zones[static_cast<std::size_t> (r)];
            z.filterOverride = overrideOn;
            if (overrideOn) z.filters = b;
        }
        safe->proc_.uiEditInstrument (std::move (d), "Filters", "filter");
    };
    filterEditor_->onParam = [safe] (int index, double plain, int phase)
    {
        if (safe == nullptr) return;
        const int tr = safe->proc_.selectedTrack();
        const ParamId ids[4] = { mx::samplerParam (tr, mx::kFilter1Cutoff), mx::samplerParam (tr, mx::kFilter1Resonance), mx::samplerParam (tr, mx::kFilter2Cutoff), mx::samplerParam (tr, mx::kFilter2Resonance) };
        const ParamId id = ids[juce::jlimit (0, 3, index)];
        if (phase == 0) safe->proc_.uiBeginEdit (id);
        else if (phase == 2) safe->proc_.uiEndEdit (id);
        else if (const auto* spec = safe->proc_.parameters().find (id)) safe->proc_.uiSetNormalised (id, spec->toNormalized (plain));
    };
    addAndMakeVisible (*filterEditor_);
    filterEditor_->setBounds (getLocalBounds());
    filterEditor_->toFront (true);
    updateFilterEditor();
    filterEditor_->grabKeyboardFocus();
}

void NovaSamplerEditor::closeFilterEditor()
{
    filterEditor_.reset();
    grabKeyboardFocus();
}

void NovaSamplerEditor::updateFilterEditor()
{
    if (! filterEditor_) return;
    const auto& ps = proc_.parameters();
    FilterEditor::DefaultParams p;
    const int tr = proc_.selectedTrack();
    p.cutoffHz[0] = ps.get (mx::samplerParam (tr, mx::kFilter1Cutoff));
    p.cutoffHz[1] = ps.get (mx::samplerParam (tr, mx::kFilter2Cutoff));
    p.resonance[0] = ps.get (mx::samplerParam (tr, mx::kFilter1Resonance)) / 100.0;
    p.resonance[1] = ps.get (mx::samplerParam (tr, mx::kFilter2Resonance)) / 100.0;
    const auto& d = cached_;
    int count = 0;
    bool overrideOn = false;
    FilterBlock zoneBlock = d.defaultFilters;
    bool first = true;
    for (const int r : zones_->selectedRows())
    {
        if (r < 0 || r >= static_cast<int> (d.zones.size())) continue;
        const auto& z = d.zones[static_cast<std::size_t> (r)];
        ++count;
        if (first) { overrideOn = z.filterOverride; if (z.filterOverride) zoneBlock = z.filters; first = false; }
    }
    filterEditor_->setState (d.defaultFilters, p, count, overrideOn, zoneBlock);
}

// ------------------------------------------------------------------------------------------------ moniteur des voix
void NovaSamplerEditor::updateVoices()
{
    if (! showVoices_)
    {
        if (! seenVoices_.empty()) { seenVoices_.clear(); zones_->setPlayingRows ({}); matrix_->setSoundingZones ({}); }
        return;
    }
    Engine::VoiceInfo info[Engine::kVoiceSnapshot];
    const int n = proc_.readVoices (info, Engine::kVoiceSnapshot);
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& [order, s] : seenVoices_) s.present = false;
    std::vector<int> sounding;
    for (int i = 0; i < n; ++i)
    {
        auto& s = seenVoices_[info[i].order];
        s.info = info[i];
        s.lastSeen = now;
        s.present = true;
        if (info[i].sourceIndex >= 0 && info[i].track == proc_.selectedTrack()) sounding.push_back (info[i].sourceIndex);
    }
    constexpr juce::uint32 kKeepMs = 700;
    for (auto it = seenVoices_.begin(); it != seenVoices_.end();)
        it = (! it->second.present && now - it->second.lastSeen > kKeepMs) ? seenVoices_.erase (it) : std::next (it);
    while (seenVoices_.size() > 160) seenVoices_.erase (seenVoices_.begin());
    std::sort (sounding.begin(), sounding.end());
    sounding.erase (std::unique (sounding.begin(), sounding.end()), sounding.end());
    zones_->setPlayingRows (sounding);
    matrix_->setSoundingZones (sounding);

    std::array<InstrumentDescription, mx::numTracks> descs;
    std::array<bool, mx::numTracks> haveDesc {};
    int held = 0, releasing = 0, fading = 0;
    std::vector<VoicesView::Row> rows;
    for (auto it = seenVoices_.rbegin(); it != seenVoices_.rend(); ++it)
    {
        const auto& s = it->second;
        const auto& v = s.info;
        VoicesView::Row r;
        const auto tr = static_cast<std::size_t> (juce::jlimit (0, mx::numTracks - 1, v.track));
        if (! haveDesc[tr]) { descs[tr] = proc_.instrumentDescription (static_cast<int> (tr)); haveDesc[tr] = true; }
        const auto& d = descs[tr];
        const bool known = v.sourceIndex >= 0 && v.sourceIndex < static_cast<int> (d.zones.size());
        const ZoneDescription* z = known ? &d.zones[static_cast<std::size_t> (v.sourceIndex)] : nullptr;
        r.sample = z != nullptr ? juce::File (U (z->samplePath)).getFileName() : juce::String ("?");
        r.key = juce::String (v.key) + "  " + noteName (v.key);
        r.velocity = juce::String (v.velocity);
        r.zone = juce::String (v.track + 1) + ":" + (known ? juce::String (v.sourceIndex) : juce::String ("-"));
        if (z != nullptr)
        {
            r.trigger = tx (z->onRelease ? "trig.release" : z->trigger == TriggerCondition::First ? "trig.first" : z->trigger == TriggerCondition::Legato ? "trig.legato" : "trig.attack");
            r.group = U (z->group);
            r.choke = z->chokeCuts == 0 && z->chokeCutBy == 0 ? juce::String() : z->chokeCuts == z->chokeCutBy ? juce::String (z->chokeCuts) : juce::String (z->chokeCuts) + ">" + juce::String (z->chokeCutBy);
        }
        r.finished = ! s.present;
        r.fading = v.fading;
        r.releasing = v.releasing;
        r.preview = v.preview;
        r.state = tx (r.finished ? "voices.state_finished" : v.preview ? "voices.state_preview" : v.fading ? "voices.state_fading" : v.releasing ? "voices.state_release" : "voices.state_held");
        r.layerGain = juce::String (v.layerGain, 2);
        r.level = juce::String (v.level, 2);
        r.progress = v.progress;
        r.source = tx (v.streaming ? "voices.source_disk" : "voices.source_ram");
        if (s.present) { if (v.fading) ++fading; else if (v.releasing) ++releasing; else ++held; }
        rows.push_back (std::move (r));
    }
    const int total = held + releasing + fading;
    voicesView_->setRows (std::move (rows), U (translator().format ("voices.summary", { std::to_string (total), std::to_string (held), std::to_string (releasing), std::to_string (fading),
                                                                                         std::to_string (proc_.streamUnderruns()) })));
}

// ------------------------------------------------------------------------------------------------ état
juce::String NovaSamplerEditor::statusText() const
{
    auto s = baseStatusText();
    if (cached_.streamFromDisk) s += U (translator().format ("sampler.stream_status", { std::to_string (proc_.streamUnderruns()) }));
    return s;
}

juce::String NovaSamplerEditor::baseStatusText() const
{
    using Kind = NeptuneProcessor::InstrumentStatus::Kind;
    const auto st = proc_.instrumentStatus();
    switch (st.kind)
    {
        case Kind::None: return tx ("sampler.none");
        case Kind::Loading:
            if (st.total > 0) return U (translator().format ("sampler.loading_progress", { std::to_string (st.done), std::to_string (st.total) }));
            return tx ("sampler.loading");
        case Kind::Error: return U (translator().format ("sampler.error", { st.error }));
        case Kind::Loaded:
        {
            if (st.zones > 1)
                return U (translator().format ("sampler.loaded_many", { std::to_string (st.zones), std::to_string (st.files), juce::String (st.megabytes, 1).toStdString() }));
            const std::string ch (translator().tr (st.channels == 1 ? "sampler.mono" : "sampler.stereo"));
            return U (translator().format ("sampler.loaded", { st.name, ch, juce::String (st.sampleRate, 0).toStdString(), juce::String (st.seconds, 2).toStdString(),
                                                                std::to_string (st.bitDepth), juce::String (st.megabytes, 1).toStdString() }));
        }
    }
    return {};
}

// Mémoire exacte lue dans l'en-tête des fichiers, selon ce que le mode (tout RAM / streaming) garde en mémoire ; cases par Groupe.
void NovaSamplerEditor::rebuildLoadPanel()
{
    const auto& d = cached_;
    auto sizeOf = [this, &d] (const ZoneDescription& z) -> std::uint64_t
    {
        const auto& info = fileInfo_.info (z.samplePath);
        if (! info.ok) return 0;
        const auto n = residentFramesNeeded (d, sanitised (z), info.channels, info.keptAsInt16, info.frames);
        return static_cast<std::uint64_t> (n) * static_cast<std::uint64_t> (info.channels) * (info.keptAsInt16 ? 2u : 4u);
    };
    std::uint64_t alternates = 0, release = 0;
    std::map<std::string, std::uint64_t> groups;
    {
        auto all = d;
        all.mainRoundRobinOnly = true; all.loadReleaseSamples = true; all.unloadedGroups.clear();
        const auto mask = loadedZones (all);
        for (std::size_t i = 0; i < d.zones.size(); ++i)
        {
            const auto& z = d.zones[i];
            const auto bytes = sizeOf (z);
            if (z.onRelease) release += bytes;
            else if (! z.group.empty()) groups[z.group] += bytes;
            if (! mask[i] && ! z.onRelease && z.group.empty()) alternates += bytes;
        }
    }
    auto mb = [] (std::uint64_t b) { return juce::String (static_cast<double> (b) / (1024.0 * 1024.0), 0).toStdString(); };
    std::string signature = std::to_string (alternates) + "/" + std::to_string (release);
    for (const auto& [g, b] : groups) signature += "|" + g + ":" + std::to_string (b);
    for (const auto& u : d.unloadedGroups) signature += "!" + u;
    const bool has = ! d.zones.empty();
    rrToggle_->setButtonText (U (translator().format ("load.round_robin", { mb (alternates) })));
    releaseToggle_->setButtonText (U (translator().format ("load.release", { mb (release) })));
    rrToggle_->setToggleState (! d.mainRoundRobinOnly, juce::dontSendNotification);
    releaseToggle_->setToggleState (d.loadReleaseSamples, juce::dontSendNotification);
    rrToggle_->setEnabled (has);
    releaseToggle_->setEnabled (has);
    streamToggle_->setButtonText (tx ("load.stream"));
    updating_ = true;
    streamToggle_->setToggleState (d.streamFromDisk, juce::dontSendNotification);
    preloadBox_->setSelectedId (d.preloadKB, juce::dontSendNotification);
    if (preloadBox_->getSelectedId() != d.preloadKB) preloadBox_->setText (juce::String (d.preloadKB) + " KB", juce::dontSendNotification);
    updating_ = false;
    streamToggle_->setEnabled (has);
    preloadBox_->setEnabled (has && d.streamFromDisk);
    preloadLabel_->setEnabled (has && d.streamFromDisk);

    if (signature != groupsSignature_)
    {
        groupsSignature_ = signature;
        groupToggles_.clear();
        groupNames_.clear();
        for (const auto& [name, bytes] : groups)
        {
            groupNames_.push_back (name);
            auto* t = groupToggles_.add (new juce::ToggleButton());
            t->setButtonText (U (translator().format ("load.group", { name, mb (bytes) })));
            t->setToggleState (std::find (d.unloadedGroups.begin(), d.unloadedGroups.end(), name) == d.unloadedGroups.end(), juce::dontSendNotification);
            t->onClick = [this, name, t]
            {
                auto desc = proc_.instrumentDescription();
                auto& u = desc.unloadedGroups;
                u.erase (std::remove (u.begin(), u.end(), name), u.end());
                if (! t->getToggleState()) u.push_back (name);   // décoché = laissé sur le disque
                proc_.uiEditInstrument (std::move (desc), "Load group");
            };
            t->addMouseListener (groupMenuListener_.get(), false);
            groupsHolder_.addAndMakeVisible (t);
        }
        const int w = juce::jmax (200, groupsViewport_->getWidth() - 20);
        for (int i = 0; i < groupToggles_.size(); ++i) groupToggles_[i]->setBounds (0, i * 28, w, 28);
        groupsHolder_.setSize (w, juce::jmax (1, groupToggles_.size()) * 28);
    }
}

void NovaSamplerEditor::refreshFromInstrument()
{
    const auto rev = proc_.instrumentRevision();
    if (rev != lastRevision_)
    {
        lastRevision_ = rev;
        cached_ = proc_.instrumentDescription();
        zones_->setZones (cached_.zones);
        matrix_->setDescription (cached_);
        rebuildLoadPanel();
    }
    matrix_->setSelection (zones_->selectedRows());
}

void NovaSamplerEditor::updateSampleInfo()
{
    const auto* z = shownZone();
    if (z == nullptr || z->samplePath.empty())
    {
        wave_->setSample ({}, 0, 48000.0);
        infoText_->setText ({});
        markerText_->setText ({});
        shownSamplePath_.clear();
        return;
    }
    const auto& info = fileInfo_.info (z->samplePath);
    if (! info.ok)
    {
        wave_->setSample ({}, 0, 48000.0);
        infoText_->setText (U (info.error));
        return;
    }
    wave_->setSample (z->samplePath, info.frames, info.sampleRate);
    nova::WaveView::Markers m;
    m.start = z->startFrame; m.end = z->endFrame;
    const int idx = std::clamp (editLoop_, 0, loopCountOf (*z) - 1);
    const auto edited = loopView (*z, idx);
    m.loopStart = edited.start; m.loopEnd = edited.end;
    m.crossfade = edited.crossfade;
    m.loopStart = std::max (edited.start, z->startFrame);   // la boucle ne revient jamais avant START (même règle que le moteur)
    m.loopNumber = idx + 1;
    if (const auto marker = effectiveMarker (*z); marker >= 0)   // propriété du sample : visible sur toute zone (utilisée seulement par les zones de relâchement)
    {
        m.volumeRef = marker;
        const std::string key = z->samplePath + "|" + std::to_string (marker) + "|" + std::to_string (z->releaseMeter);
        if (key != refLevelKey_)
        {
            refLevelKey_ = key;
            refLevelKnown_ = measureLevelAt (std::filesystem::u8path (z->samplePath), marker, z->releaseMeter, refLevelDb_);
        }
        m.volumeRefKnown = refLevelKnown_;
        m.volumeRefDb = refLevelDb_;
    }
    if (cursorPath_ != z->samplePath) { cursorPath_ = z->samplePath; cursorFrame_ = -1; }
    wave_->setCursor (cursorFrame_, false);
    for (int i = 0; i < loopCountOf (*z); ++i)
        if (i != idx) { const auto l = loopView (*z, i); m.otherLoops.push_back ({ std::max (l.start, z->startFrame), l.end < 0 ? info.frames : l.end, i + 1 }); }
    const double rate = info.sampleRate;
    m.fadeInMs = z->fadeInMs; m.fadeOutMs = z->fadeOutMs; m.delayMs = z->delayMs;
    m.fadeInFrames = static_cast<std::int64_t> (std::llround (z->fadeInMs * rate / 1000.0));
    m.fadeOutFrames = static_cast<std::int64_t> (std::llround (z->fadeOutMs * rate / 1000.0));
    m.delayFrames = static_cast<std::int64_t> (std::llround (z->delayMs * rate / 1000.0));
    m.gainDb = z->gainDb;
    m.tuneCents = z->tuneCents;
    m.loop = z->mode == PlayMode::Loop;
    m.reverse = z->reverse;
    wave_->setMarkers (m);
    if (shownSamplePath_ != z->samplePath)
    {
        shownSamplePath_ = z->samplePath;
        infoText_->setText (juce::File (U (z->samplePath)).getFileName() + "   -   " + juce::String (info.channels) + " ch   " + juce::String (info.sampleRate, 0) + " Hz   "
                            + juce::String (info.bitDepth) + " bit   " + juce::String (static_cast<double> (info.frames) / info.sampleRate, 3) + " s   ("
                            + juce::String (static_cast<long long> (info.frames)) + " " + tx ("sm.frames") + ")");
    }
    auto t = [&] (std::int64_t f) { return juce::String (static_cast<long long> (f)) + " (" + juce::String (static_cast<double> (f) * 1000.0 / info.sampleRate, 1) + " ms)"; };
    const std::int64_t end = z->endFrame < 0 ? info.frames : z->endFrame, loopEnd = edited.end < 0 ? info.frames : edited.end;
    auto text = juce::String ("START ") + t (z->startFrame) + "    END " + t (end);
    if (z->mode == PlayMode::Loop) text += "    LOOP" + (loopCountOf (*z) > 1 ? " " + juce::String (idx + 1) : juce::String()) + " " + t (std::max (edited.start, z->startFrame)) + " > " + t (loopEnd);
    markerText_->setText (text);
}

// ------------------------------------------------------------------------------------------------ clavier, mise en page, rafraîchissement
bool NovaSamplerEditor::keyPressed (const juce::KeyPress& k) { return EditorBase::keyPressed (k); }

void NovaSamplerEditor::pageChanged()
{
    if (zones_ == nullptr) return;
    const bool zonesPage = currentPage() == 0;
    // moniteur de voix : remplace la liste et la matrice
    zones_->setVisible (zonesPage && listVisible_ && ! showVoices_);
    tablePanel_->setVisible (zonesPage && listVisible_ && ! showVoices_);
    matrix_->setVisible (zonesPage && ! showVoices_);
    matrixPanel_->setVisible (zonesPage && ! showVoices_);
    voicesView_->setVisible (zonesPage && showVoices_);
    setInfoVisible (tablePanel_, zonesPage && listVisible_ && ! showVoices_);
    setInfoVisible (matrixPanel_, zonesPage && ! showVoices_);
    resized();
}

void NovaSamplerEditor::layout()
{
    constexpr float X0 = 204.0f;
    // ---- colonne de gauche
    outPanel_->setBounds (R (14, 72, 176, 296));
    for (std::size_t i = 0; i < outKnobs_.size(); ++i)
        outKnobs_[i]->setBounds (R (22.0f + static_cast<float> (i % 2) * 84.0f, 106.0f + static_cast<float> (i / 2) * 92.0f, 80, 88));
    statePanel_->setBounds (R (14, 380, 176, 150));
    statusText_->setBounds (R (24, 412, 156, 76));
    voicesPill_->setBounds (R (24, 492, 156, 30));
    navPanel_->setBounds (R (14, 542, 176, 168));
    layoutNav (542, 168);

    // ---- page ZONES
    zonePanel_->setBounds (R (X0, 72, 942, 114));
    loadPill_->setBounds (R (X0 + 12, 106, 120, 30));
    importPill_->setBounds (R (X0 + 138, 106, 120, 30));
    removePill_->setBounds (R (X0 + 264, 106, 120, 30));
    listPill_->setBounds (R (X0 + 390, 106, 100, 30));
    monitorPill_->setBounds (R (X0 + 496, 106, 150, 30));
    selectionText_->setBounds (R (X0 + 660, 112, 270, 18));
    modePill_->setBounds (R (X0 + 12, 144, 160, 30));
    rootNumber_->setBounds (R (X0 + 178, 144, 160, 30));
    triggerChoice_->pill->setBounds (R (X0 + 344, 144, 250, 30));
    chokeChoice_->pill->setBounds (R (X0 + 600, 144, 170, 30));
    chokeMorePill_->setBounds (R (X0 + 776, 144, 44, 30));
    levelsPill_->setBounds (R (X0 + 826, 144, 106, 30));
    if (listVisible_ && ! showVoices_)
    {
        tablePanel_->setBounds (R (X0, 196, 330, 514));
        zones_->setBounds (R (X0 + 10, 230, 310, 470));
        matrixPanel_->setBounds (R (X0 + 342, 196, 600, 514));
        matrix_->setBounds (R (X0 + 350, 228, 584, 474));
    }
    else
    {
        matrixPanel_->setBounds (R (X0, 196, 942, 514));
        matrix_->setBounds (R (X0 + 8, 228, 926, 474));
    }
    voicesView_->setBounds (R (X0, 196, 942, 514));

    // ---- page SAMPLE
    wavePanel_->setBounds (R (X0, 72, 942, 204));
    wave_->setBounds (R (X0 + 10, 104, 922, 162));
    peakCurvePill_->setBounds (R (X0 + 150, 76, 54, 22));
    rmsCurvePill_->setBounds (R (X0 + 208, 76, 54, 22));
    lufsCurvePill_->setBounds (R (X0 + 266, 76, 58, 22));
    loudestPill_->setBounds (R (X0 + 340, 76, 104, 22));
    prevPeakPill_->setBounds (R (X0 + 448, 76, 80, 22));
    nextPeakPill_->setBounds (R (X0 + 532, 76, 80, 22));
    rangePill_->setBounds (R (X0 + 630, 76, 72, 22));
    rangeLoNumber_->setBounds (R (X0 + 706, 74, 96, 26));
    rangeHiNumber_->setBounds (R (X0 + 806, 74, 96, 26));
    infoText_->setBounds (R (X0 + 4, 278, 934, 16));
    markerText_->setBounds (R (X0 + 4, 294, 934, 16));
    toolsPanel_->setBounds (R (X0, 314, 942, 202));
    trimPill_->setBounds (R (X0 + 12, 346, 110, 28));
    normPill_->setBounds (R (X0 + 128, 346, 116, 28));
    normChoice_->pill->setBounds (R (X0 + 250, 346, 170, 28));
    dcPill_->setBounds (R (X0 + 426, 346, 124, 28));
    saveAsPill_->setBounds (R (X0 + 556, 346, 150, 28));
    silencePill_->setBounds (R (X0 + 12, 380, 170, 28));
    silenceChoice_->pill->setBounds (R (X0 + 188, 380, 160, 28));
    cutsPill_->setBounds (R (X0 + 354, 380, 160, 28));
    cutsChoice_->pill->setBounds (R (X0 + 520, 380, 186, 28));
    loopFindPill_->setBounds (R (X0 + 12, 414, 150, 28));
    loopOptionsPill_->setBounds (R (X0 + 168, 414, 36, 28));
    snapPill_->setBounds (R (X0 + 210, 414, 150, 28));
    toolStatus_->setBounds (R (X0 + 368, 414, 338, 28));
    durationNumber_->setBounds (R (X0 + 12, 448, 200, 28));
    pitchNumber_->setBounds (R (X0 + 218, 448, 200, 28));
    stretchPill_->setBounds (R (X0 + 424, 448, 140, 28));
    toolsPlayPill_->setBounds (R (X0 + 12, 482, 110, 28));
    playSourceChoice_->pill->setBounds (R (X0 + 128, 482, 250, 28));
    toolLoopChoice_->pill->setBounds (R (X0 + 384, 482, 120, 28));
    loopPlayModeChoice_->pill->setBounds (R (X0 + 510, 482, 196, 28));
    seam_->setBounds (R (X0 + 718, 346, 216, 130));
    playPanel_->setBounds (R (X0, 524, 942, 186));
    loopPill_->setBounds (R (X0 + 12, 558, 170, 28));
    reversePill_->setBounds (R (X0 + 188, 558, 150, 28));
    showAllPill_->setBounds (R (X0 + 344, 558, 130, 28));
    playPill_->setBounds (R (1006, 736, 140, 32));
    recPill_->setBounds (R (846, 736, 140, 32));
    rootNumber2_->setBounds (R (X0 + 480, 558, 154, 28));
    loopEditChoice_->pill->setBounds (R (X0 + 640, 558, 108, 28));
    loopAddPill_->setBounds (R (X0 + 754, 558, 120, 28));
    loopRemovePill_->setBounds (R (X0 + 880, 558, 52, 28));
    loopRepeatsNumber_->setBounds (R (X0 + 696, 654, 150, 32));
    pan_->setBounds (R (X0 + 468, 650, 214, 46));
    fadeIn_->setBounds (R (X0 + 12, 596, 214, 46));
    fadeOut_->setBounds (R (X0 + 240, 596, 214, 46));
    delay_->setBounds (R (X0 + 468, 596, 214, 46));
    crossfade_->setBounds (R (X0 + 696, 596, 230, 46));
    tune_->setBounds (R (X0 + 12, 650, 214, 46));
    gain_->setBounds (R (X0 + 240, 650, 214, 46));

    // ---- page SON
    envPanel_->setBounds (R (X0, 72, 942, 340));
    env_->setBounds (R (X0 + 10, 104, 922, 298));
    filterPanel_->setBounds (R (X0, 424, 560, 286));
    for (std::size_t i = 0; i < filterKnobs_.size(); ++i) filterKnobs_[i]->setBounds (R (X0 + 12.0f + static_cast<float> (i) * 134.0f, 462, 126, 120));
    filtersPill_->setBounds (R (X0 + 12, 596, 280, 30));
    slotsAreaPanel_->setBounds (R (X0 + 572, 424, 370, 286));
    slotsPill_->setBounds (R (X0 + 584, 462, 346, 30));
    slotsText_->setBounds (R (X0 + 584, 504, 346, 60));

    // ---- page INSTRUMENT
    velPanel_->setBounds (R (X0, 72, 442, 638));
    velocity_->setBounds (R (X0 + 10, 104, 422, 596));
    polyPanel_->setBounds (R (X0 + 454, 72, 488, 400));
    poly_->setBounds (R (X0 + 464, 104, 468, 360));
    memPanel_->setBounds (R (X0 + 454, 484, 488, 226));
    rrToggle_->setBounds (R (X0 + 466, 516, 464, 26));
    releaseToggle_->setBounds (R (X0 + 466, 544, 464, 26));
    streamToggle_->setBounds (R (X0 + 466, 572, 300, 26));
    preloadLabel_->setBounds (R (X0 + 770, 572, 70, 26));
    preloadBox_->setBounds (R (X0 + 842, 572, 90, 26));
    groupsViewport_->setBounds (R (X0 + 466, 606, 464, 96));

    abPill()->setBounds (R (14, 724 + 12, 40, 32));
    presetPill()->setBounds (R (440, 11, 280, 36));
    if (wizard_) wizard_->setBounds (getLocalBounds());
    if (filterEditor_) filterEditor_->setBounds (getLocalBounds());
}

// Écoute : une seule à la fois (le moteur remplace la précédente). `owner` : 1 = bouton du bas, 2 = bouton des outils du sample.
void NovaSamplerEditor::startPreview (int source, std::int64_t frame, int loopIndex, int owner)
{
    if (cached_.zones.empty()) return;
    const auto sel = zones_->selectedRows();
    const int row = (! sel.empty() && sel.front() >= 0 && sel.front() < static_cast<int> (cached_.zones.size())) ? sel.front() : 0;
    proc_.setVoiceMonitor (true);   // l'état du bouton (voix qui sonne encore ou non) se lit dans le moniteur de voix
    proc_.uiPreviewZone (row, frame, source, loopIndex);
    previewing_ = true;
    previewOwner_ = owner;
    previewStartMs_ = juce::Time::getMillisecondCounter();
}

void NovaSamplerEditor::stopPreviewNow()
{
    proc_.uiPreviewStop();
    previewing_ = false;
    previewOwner_ = 0;
}

// Un one-shot qui finit seul remet le bouton à « play » : on regarde si une voix d'écoute de la piste sonne encore. Le moniteur de voix n'est alimenté que sur demande :
// il reste allumé tant qu'une écoute est en cours (sans lui, une boucle aurait l'air finie au bout d'un instant et le bouton ne saurait plus l'arrêter).
void NovaSamplerEditor::updatePlayButton()
{
    if (previewing_ && juce::Time::getMillisecondCounter() - previewStartMs_ > 400)
    {
        Engine::VoiceInfo info[Engine::kVoiceSnapshot];
        const int n = proc_.readVoices (info, Engine::kVoiceSnapshot);
        bool any = false;
        for (int i = 0; i < n; ++i) any = any || (info[i].preview && info[i].track == proc_.selectedTrack());
        if (! any) { previewing_ = false; previewOwner_ = 0; }
    }
    proc_.setVoiceMonitor (showVoices_ || previewing_);
    const bool bottom = previewing_ && previewOwner_ == 1, tools = previewing_ && previewOwner_ == 2;
    playPill_->setText (tx (bottom ? "sm.stop_button" : "sm.play_button"));
    playPill_->setOn (bottom);
    playPill_->setAlpha (cached_.zones.empty() && ! previewing_ ? 0.4f : 1.0f);
    toolsPlayPill_->setText (tx (tools ? "sm.stop_button" : "sm.tp_play"));
    toolsPlayPill_->setOn (tools);
}

void NovaSamplerEditor::toggleRecording()
{
    if (proc_.isRecording())
    {
        const auto frames = proc_.uiStopRecording();
        const auto path = recordingPath_;
        recordingPath_.clear();
        finishRecording (path, frames);
        return;
    }
    std::string path, err;
    if (! proc_.uiStartRecording (path, &err)) { recPill_->setText (tx ("sm.rec_error")); return; }
    recordingPath_ = path;
    recordStartMs_ = juce::Time::getMillisecondCounter();
}

// La prise devient une zone sur la prochaine note libre ; les silences de début et de fin sont écartés (marqueurs, rien n'est coupé).
void NovaSamplerEditor::finishRecording (const std::string& path, std::int64_t frames)
{
    if (path.empty()) return;
    if (frames <= 0) { std::error_code ec; std::filesystem::remove (utf8Path (path), ec); return; }
    Zone zone;
    zone.samplePath = path;
    auto d = proc_.instrumentDescription();
    int note = 60;
    if (! d.zones.empty())
    {
        note = 0;
        for (const auto& z : d.zones) note = std::max (note, z.keyHigh + 1);
        note = std::min (note, 127);
    }
    zone.keyLow = zone.keyHigh = zone.rootNote = note;
    se::Buffer b;
    if (se::readFile (utf8Path (path), b))
    {
        const auto r = se::detectSilence (b, -60.0);
        if (r.found)
        {
            const std::int64_t pad = static_cast<std::int64_t> (b.sampleRate * 0.005);
            const std::int64_t start = std::max<std::int64_t> (0, r.start - pad), end = std::min (b.frames(), r.end + pad);
            zone.startFrame = start;
            zone.endFrame = end >= b.frames() ? -1 : end;
        }
    }
    d.zones.push_back (sanitised (zone));
    const int index = static_cast<int> (d.zones.size()) - 1;
    proc_.uiEditInstrument (std::move (d), "Record sample");
    lastRevision_ = 0xFFFFFFFFu;
    refreshFromInstrument();
    setSelection ({ index });
    setPage (1);
}

void NovaSamplerEditor::updateRecordButton()
{
    if (! proc_.isRecording())
    {
        recPill_->setText (tx ("sm.rec"));
        recPill_->setOn (false);
        return;
    }
    const auto frames = proc_.recordedFrames();
    const double seconds = static_cast<double> (frames) / std::max (1.0, proc_.recordingSampleRate());
    const bool noInput = juce::Time::getMillisecondCounter() - recordStartMs_ > 1500 && frames == 0;
    const int whole = static_cast<int> (seconds);
    recPill_->setText (noInput ? tx ("sm.rec_noinput") : tx ("sm.rec_stop") + "  " + juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2));
    recPill_->setOn (true);
}

void NovaSamplerEditor::tick()
{
    for (int t = 0, k = 0, v = 0; proc_.popNoteTrace (t, k, v);)
        if (t == proc_.selectedTrack()) matrix_->notePlayed (k, v);   // curseur de la matrice (piste sélectionnée)
    refreshFromInstrument();
    const auto* z = shownZone();
    const auto sel = selection();
    const bool has = z != nullptr;
    statusText_->setText (statusText());
    {
        const int voices = proc_.totalActiveVoices();
        if (voices != lastVoiceCount_)
        {
            lastVoiceCount_ = voices;
            voicesPill_->setText (tx ("analog.voices") + " " + juce::String (voices) + " / " + juce::String (proc_.masterSettings().maxVoices));
        }
    }
    updateVoices();
    updatePlayButton();
    updateRecordButton();
    updateFilterEditor();
    if (chokePanel_ != nullptr && has) chokePanel_->setValues (z->chokeCuts, z->chokeCutBy);

    const int page = currentPage();
    if (page == 0)
    {
        listPill_->setOn (listVisible_);
        monitorPill_->setOn (showVoices_);
        removePill_->setAlpha (sel.empty() ? 0.4f : 1.0f);
        selectionText_->setText (sel.empty() ? tx ("sampler.selection_all") : U (translator().format ("sampler.selection_n", { std::to_string (sel.size()) })));
        const bool loop = has && z->mode == PlayMode::Loop;
        modePill_->setText (U (translator().format ("sampler.mode", { std::string (translator().tr (loop ? "sampler.mode_loop" : "sampler.mode_one_shot")) })));
        modePill_->setOn (loop);
        rootNumber_->update();
        triggerChoice_->refresh (has);
        chokeChoice_->refresh (has);
        chokeMorePill_->setOn (has && z->chokeCuts != z->chokeCutBy);
    }
    if (page == 1)
    {
        updateSampleInfo();
        updateSeam();
        updateFootprint();
        for (auto* c : { normChoice_, silenceChoice_, cutsChoice_, playSourceChoice_ }) c->refresh (has);
        {
            const int n = z != nullptr ? loopCountOf (*z) : 1;
            if (static_cast<int> (toolLoopChoice_->items.size()) != n) toolLoopChoice_->items = loopEditChoice_->items;
            const bool loopSource = playSourceIndex_ == 3 && has && z->mode == PlayMode::Loop;
            toolLoopChoice_->refresh (loopSource);
            loopPlayModeChoice_->refresh (loopSource);
        }
        for (auto* pl : { trimPill_, normPill_, dcPill_, saveAsPill_, silencePill_, cutsPill_, loopFindPill_ }) pl->setAlpha (has ? 1.0f : 0.4f);
        {
            // boucles multiples : la liste du menu suit le nombre de boucles ; la dernière tourne sans fin (pas de répétitions)
            const int n = z != nullptr ? loopCountOf (*z) : 1;
            if (z != nullptr) editLoop_ = std::clamp (editLoop_, 0, n - 1);
            if (static_cast<int> (loopEditChoice_->items.size()) != n)
            {
                loopEditChoice_->items.clear();
                for (int i = 0; i < n; ++i) loopEditChoice_->items.push_back (juce::String (i + 1) + (n > 1 ? " / " + juce::String (n) : juce::String()));
            }
            loopEditChoice_->refresh (has);
            loopRepeatsNumber_->update();
            loopRepeatsNumber_->setActive (has && n > 1 && editLoop_ < n - 1);
            loopAddPill_->setAlpha (has && n <= static_cast<int> (ZoneDescription::maxExtraLoops) ? 1.0f : 0.4f);
            loopRemovePill_->setAlpha (has && n > 1 ? 1.0f : 0.4f);
        }
        durationNumber_->update(); pitchNumber_->update();
        durationNumber_->setActive (has); pitchNumber_->setActive (has);
        stretchPill_->setAlpha (has && ! stretchBusy_ ? 1.0f : 0.4f);
        if (has && z != nullptr)
        {
            auto options = loopOptions_;
            options.cursor = (cursorFrame_ >= 0 && cursorPath_ == z->samplePath) ? cursorFrame_ : -1;
            const std::string key = z->samplePath + "|" + std::to_string (z->startFrame) + "|" + std::to_string (z->endFrame) + "|" + std::to_string (editLoop_)
                                    + "|" + (options.period ? "p" : "-") + (options.stableOnly ? "s" : "-") + (options.spectral ? "f" : "-") + (options.aroundCursor ? "c" : "-")
                                    + std::to_string (options.cursor) + "|" + std::to_string (static_cast<int> (options.radiusMs));
            loopFindPill_->setText (tx ("sm.tool_loop") + (key == loopCandidatesKey_ && ! loopCandidates_.empty()
                                                          ? "  " + juce::String (loopCandidateIndex_ + 1) + "/" + juce::String (static_cast<int> (loopCandidates_.size())) : juce::String()));
        }
        const bool loop = has && z->mode == PlayMode::Loop;
        loopPill_->setText (U (translator().format ("sampler.mode", { std::string (translator().tr (loop ? "sampler.mode_loop" : "sampler.mode_one_shot")) })));
        loopPill_->setOn (loop);
        reversePill_->setOn (has && z->reverse);
        for (auto* s : { fadeIn_, fadeOut_, delay_, tune_, gain_, pan_ }) { s->update(); s->setActive (has); }
        crossfade_->update();
        crossfade_->setActive (loop && has && ! z->reverse);
        fadeOut_->setActive (has && ! loop);
        rootNumber2_->update();
    }
    if (page == 2)
    {
        env_->setEnabled (has);
        if (has) env_->setEnvelope (z->envelope);
        int assigned = 0;
        for (const auto& s : cached_.slots) if (s.assigned()) ++assigned;
        slotsText_->setText (U (translator().format ("sm.slots_status", { std::to_string (assigned) })));
    }
    if (page == 3)
    {
        velocity_->setValues (cached_.velocityFadeWidth, cached_.velocityFadeLinear, cached_.velocityCurve);
        PolyphonyPanel::Settings ps;
        ps.mode = cached_.polyphony; ps.maxVoices = cached_.maxVoices; ps.steal = cached_.stealMode; ps.sameKey = cached_.sameKey;
        ps.cutFadeMs = cached_.cutFadeMs; ps.glideMs = cached_.glideMs; ps.chokeSameZone = cached_.chokeSameZone;
        poly_->setValues (ps);
    }
}

void NovaSamplerEditor::paintBackground (juce::Graphics&) {}
