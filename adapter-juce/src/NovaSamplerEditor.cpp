#include "NovaSamplerEditor.h"

#include <algorithm>
#include <cmath>

#include "ChokePanel.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/SamplerParams.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mx = neptune::core::mixer;

namespace
{
juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

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

// Bouton « maintenu » : appuyé = la note démarre, relâché = Note Off (une boucle se relâche, un one-shot va au bout).
struct NovaSamplerEditor::HoldListener : public juce::MouseListener
{
    std::function<void()> down, up;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu() && down) down(); }
    void mouseUp (const juce::MouseEvent&) override { if (up) up(); }
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
    wave_->onGestureEnd = [this] { proc_.uiEndGesture(); };
    wave_->onMarker = [this] (nova::WaveView::Marker m, std::int64_t frame)
    {
        const auto* shown = shownZone();
        if (shown == nullptr) return;
        const auto& info = fileInfo_.info (shown->samplePath);
        const std::int64_t total = info.ok ? info.frames : 0;
        editZone ([m, frame, total] (Zone& z)
        {
            const std::int64_t end = z.endFrame < 0 ? total : z.endFrame;
            const std::int64_t loopEnd = z.loopEnd < 0 ? total : z.loopEnd;
            switch (m)
            {
                case nova::WaveView::Marker::Start: z.startFrame = std::clamp<std::int64_t> (frame, 0, std::max<std::int64_t> (0, end - 1)); z.loopStart = std::max (z.loopStart, z.startFrame); break;
                case nova::WaveView::Marker::End: z.endFrame = frame >= total ? -1 : std::max (frame, z.startFrame + 1); break;
                case nova::WaveView::Marker::LoopStart: z.loopStart = std::clamp<std::int64_t> (frame, z.startFrame, std::max<std::int64_t> (z.startFrame, loopEnd - 1)); break;
                case nova::WaveView::Marker::LoopEnd: z.loopEnd = frame >= total ? -1 : std::max (frame, z.loopStart + 1); break;
                case nova::WaveView::Marker::Crossfade: z.loopCrossfade = std::clamp<std::int64_t> (frame, 0, std::min (z.loopStart, (loopEnd - z.loopStart) / 2)); break;
            }
        });
    };
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
    playPill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    playPill_->setText (tx ("sm.play_button"));
    addInfo (1, playPill_, "hlp.sm_playbtn", 1);
    playListener_ = std::make_unique<HoldListener>();
    playListener_->down = [this]
    {
        if (cached_.zones.empty()) return;
        const auto sel = zones_->selectedRows();
        const int row = (! sel.empty() && sel.front() >= 0 && sel.front() < static_cast<int> (cached_.zones.size())) ? sel.front() : 0;
        proc_.uiPreviewZone (row);
    };
    playListener_->up = [this] { proc_.uiPreviewRelease(); };
    playPill_->addMouseListener (playListener_.get(), false);
    stopPill_ = add<nova::Pill> (1, skin(), nova::Pill::Look::Small);
    stopPill_->setText (tx ("sm.stop_button"));
    stopPill_->onClick = [this] { proc_.uiPreviewStop(); };
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
                         [this] { const auto* z = shownZone(); return z != nullptr ? static_cast<double> (z->loopCrossfade) * 1000.0 / sampleRate() : 0.0; },
                         [this] (Zone& z, double v)
                         {
                             const auto& info = fileInfo_.info (z.samplePath);
                             const std::int64_t total = info.ok ? info.frames : 0;
                             const std::int64_t loopEnd = z.loopEnd < 0 ? total : z.loopEnd;
                             z.loopCrossfade = std::clamp<std::int64_t> (static_cast<std::int64_t> (std::llround (v * sampleRate() / 1000.0)), 0, std::min (z.loopStart, std::max<std::int64_t> (0, (loopEnd - z.loopStart) / 2)));
                         }, 0.0);
    addInfo (1, crossfade_, "hlp.sm_crossfade", 1);
    tune_ = slider (tx ("sm.tune").toUpperCase(), -100.0, 100.0, 0.5, " ct", [this] { const auto* z = shownZone(); return z != nullptr ? z->tuneCents : 0.0; },
                    [] (Zone& z, double v) { z.tuneCents = v; }, 0.0);
    gain_ = slider (tx ("sm.gain").toUpperCase(), -24.0, 24.0, 0.1, " dB", [this] { const auto* z = shownZone(); return z != nullptr ? z->gainDb : 0.0; },
                    [] (Zone& z, double v) { z.gainDb = v; }, 0.0);
    rootNumber2_ = add<nova::NumberPill> (1, skin(), tx ("sampler.root").toUpperCase(), 0, 127,
                                          [this] { const auto* z = shownZone(); return z != nullptr ? z->rootNote : 60; },
                                          [this] (int v) { editZone ([v] (Zone& z) { z.rootNote = v; }); },
                                          [this] (int v) { return juce::String (v) + "  " + noteName (v); });
    rootNumber2_->gestureBegin = [this] { proc_.uiBeginGesture(); };
    rootNumber2_->gestureEnd = [this] { proc_.uiEndGesture(); };
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
                               z.startFrame = 0; z.endFrame = -1; z.loopStart = 0; z.loopEnd = -1; z.loopCrossfade = 0;   // les repères de l'ancien fichier n'ont plus de sens
                               d.zones.assign (1, z);
                               safe->proc_.uiEditInstrument (std::move (d), "Replace sample");
                               safe->lastRevision_ = 0xFFFFFFFFu;
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
    m.loopStart = z->loopStart; m.loopEnd = z->loopEnd;
    m.crossfade = z->loopCrossfade;
    m.loopStart = std::max (z->loopStart, z->startFrame);   // la boucle ne revient jamais avant START (même règle que le moteur)
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
    const std::int64_t end = z->endFrame < 0 ? info.frames : z->endFrame, loopEnd = z->loopEnd < 0 ? info.frames : z->loopEnd;
    auto text = juce::String ("START ") + t (z->startFrame) + "    END " + t (end);
    if (z->mode == PlayMode::Loop) text += "    LOOP " + t (std::max (z->loopStart, z->startFrame)) + " > " + t (loopEnd);
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
    wavePanel_->setBounds (R (X0, 72, 942, 350));
    wave_->setBounds (R (X0 + 10, 104, 922, 306));
    infoText_->setBounds (R (X0 + 4, 428, 934, 16));
    markerText_->setBounds (R (X0 + 4, 446, 934, 16));
    playPanel_->setBounds (R (X0, 468, 942, 242));
    loopPill_->setBounds (R (X0 + 12, 504, 170, 30));
    reversePill_->setBounds (R (X0 + 188, 504, 150, 30));
    showAllPill_->setBounds (R (X0 + 344, 504, 130, 30));
    playPill_->setBounds (R (X0 + 640, 504, 140, 30));
    stopPill_->setBounds (R (X0 + 786, 504, 100, 30));
    rootNumber2_->setBounds (R (X0 + 480, 504, 154, 30));
    fadeIn_->setBounds (R (X0 + 12, 552, 214, 46));
    fadeOut_->setBounds (R (X0 + 240, 552, 214, 46));
    delay_->setBounds (R (X0 + 468, 552, 214, 46));
    crossfade_->setBounds (R (X0 + 696, 552, 230, 46));
    tune_->setBounds (R (X0 + 12, 612, 214, 46));
    gain_->setBounds (R (X0 + 240, 612, 214, 46));

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
        const bool loop = has && z->mode == PlayMode::Loop;
        loopPill_->setText (U (translator().format ("sampler.mode", { std::string (translator().tr (loop ? "sampler.mode_loop" : "sampler.mode_one_shot")) })));
        loopPill_->setOn (loop);
        reversePill_->setOn (has && z->reverse);
        for (auto* s : { fadeIn_, fadeOut_, delay_, tune_, gain_ }) { s->update(); s->setActive (has); }
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
