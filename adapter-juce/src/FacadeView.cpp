#include "FacadeView.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "VelocityPanel.h"

using neptune::core::theme::Token;
namespace fc = neptune::core::facade;

namespace
{
juce::Colour toColour (const neptune::core::theme::Palette& p, Token t)
{
    const auto& c = p.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

constexpr float curveTitleHeight = 24.0f;   // hauteur (dans le canevas) de la légende d'un widget Courbe

int snapped (int v) { return (v + 2) / 4 * 4; } // aimantation sur une grille de 4

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

bool needsParam (fc::WidgetType t) { return t == fc::WidgetType::Knob || t == fc::WidgetType::Button || t == fc::WidgetType::Menu; }

// Valeurs proposées par un menu : un paramètre à choix ou à pas (rendu vide pour les autres).
int menuCount (const neptune::core::ParamSpec& s)
{
    if (s.kind == neptune::core::Kind::Choice) return static_cast<int> (s.choices.size());
    if (s.kind == neptune::core::Kind::Stepped) return std::clamp (static_cast<int> (s.max - s.min) + 1, 0, 256);
    return 0;
}
} // namespace

// ============================== Toile : dessin des widgets et gestes ==============================

class FacadeView::Canvas : public juce::Component
{
public:
    explicit Canvas (FacadeView& owner) : owner_ (owner) { setWantsKeyboardFocus (true); }

    bool dragging() const noexcept { return mode_ != Mode::None; }

    void resized() override { syncChildren(); }

    // Courbes de vélocité : un panneau réel par widget « Courbe » (mode Jeu, piste Sampler), posé sur la toile.
    void syncChildren()
    {
        auto& o = owner_;
        const bool sampler = o.track_ >= 0 && o.processor_.trackType (o.track_) == neptune::core::TrackType::Sampler;
        std::set<int> wanted;
        if (sampler && ! o.editMode_)
            for (const auto& w : o.facade_.widgets)
                if (w.type == fc::WidgetType::Curve) wanted.insert (w.id);
        for (auto it = panels_.begin(); it != panels_.end();)
            it = wanted.count (it->first) != 0 ? std::next (it) : panels_.erase (it);

        const auto rev = o.processor_.instrumentRevision();
        const bool refreshValues = rev != panelRevision_;
        panelRevision_ = rev;
        for (const int id : wanted)
        {
            const auto* w = o.facade_.find (id);
            if (w == nullptr) continue;
            auto& panel = panels_[id];
            if (panel == nullptr)
            {
                const auto d = o.processor_.instrumentDescription();
                panel = std::make_unique<VelocityPanel> (o.palette_, o.tr_, d.velocityFadeWidth, d.velocityFadeLinear, d.velocityCurve);
                auto* proc = &o.processor_;
                panel->setFadeControlsVisible (false);   // la Façade n'expose que la courbe de transfert ; le fondu reste dans l'éditeur Sampler
                panel->onGestureBegin = [proc] { proc->uiBeginGesture(); };
                panel->onGestureEnd = [proc] { proc->uiEndGesture(); };
                panel->onUndo = [proc] { proc->uiUndo(); };
                panel->onRedo = [proc] { proc->uiRedo(); };
                panel->onChange = [proc] (int width, bool linear, const neptune::core::VelocityCurve& curve)
                {
                    auto desc = proc->instrumentDescription();
                    desc.velocityFadeWidth = width;
                    desc.velocityFadeLinear = linear;
                    desc.velocityCurve = curve;
                    proc->uiEditInstrument (std::move (desc), "Velocity response", "velocity");
                };
                addAndMakeVisible (*panel);
            }
            else if (refreshValues)
            {
                const auto d = o.processor_.instrumentDescription();
                panel->setValues (d.velocityFadeWidth, d.velocityFadeLinear, d.velocityCurve);
            }
            const auto area = canvasArea();
            const float s = scale();
            const float title = w->text.empty() ? 0.0f : curveTitleHeight;   // la légende de la courbe est au-dessus du panneau
            panel->setBounds (juce::Rectangle<float> (area.getX() + w->x * s, area.getY() + (w->y + title) * s, w->w * s, (w->h - title) * s).toNearestInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto& o = owner_;
        const auto area = canvasArea();
        g.setColour (o.colour (Token::PanelBg));
        g.fillRect (area);

        loadImageIfNeeded();
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (area.toNearestInt());
            if (image_.isValid())
            {
                const float iw = static_cast<float> (image_.getWidth()), ih = static_cast<float> (image_.getHeight());
                auto dest = area;
                if (o.facade_.fit == fc::ImageFit::Fit || o.facade_.fit == fc::ImageFit::Fill)
                {
                    const float k = o.facade_.fit == fc::ImageFit::Fit ? std::min (area.getWidth() / iw, area.getHeight() / ih)
                                                                       : std::max (area.getWidth() / iw, area.getHeight() / ih);
                    dest = juce::Rectangle<float> (iw * k, ih * k).withCentre (area.getCentre());
                }
                g.drawImage (image_, dest, juce::RectanglePlacement::stretchToFit);
            }

            g.addTransform (juce::AffineTransform::scale (scale()).translated (area.getX(), area.getY()));
            for (const auto& w : o.facade_.widgets) paintWidget (g, w);
            if (o.editMode_)
            {
                for (const auto& w : o.facade_.widgets) paintEditOverlay (g, w);
                if (mode_ == Mode::Band)
                {
                    g.setColour (o.colour (Token::Accent).withAlpha (0.18f));
                    g.fillRect (band_);
                    g.setColour (o.colour (Token::Accent));
                    g.drawRect (band_, 1.0f);
                }
            }
        }
        g.setColour (o.colour (Token::Border));
        g.drawRect (area, 1.0f);

        if (o.facade_.widgets.empty())
        {
            g.setColour (o.colour (Token::TextDim));
            g.setFont (juce::FontOptions (16.0f));
            g.drawFittedText (o.text ("facade.empty"), area.toNearestInt().reduced (40), juce::Justification::centred, 4);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& o = owner_;
        const auto p = toCanvas (e.position);
        if (o.editMode_) { editDown (p, e.mods); return; }

        const auto* w = widgetAt (p);
        if (w == nullptr) return;
        if (w->type == fc::WidgetType::Action) { if (! e.mods.isPopupMenu()) runAction (*w); return; }
        if (w->param == 0 || o.processor_.parameters().indexOf (w->param) == neptune::core::ParameterSet::npos) return;
        if (e.mods.isPopupMenu())
        {
            if (w->type == fc::WidgetType::Knob) o.openModal_ (w->param, neptune::core::uistate::Terminal::Pointer);
            return;
        }
        playId_ = w->param;
        if (w->type == fc::WidgetType::Button)
        {
            toggle (w->param);
            playId_ = 0;
        }
        else if (w->type == fc::WidgetType::Menu)
        {
            showValueMenu (*w);
            playId_ = 0;
        }
        else if (w->type == fc::WidgetType::Knob)
        {
            o.processor_.uiBeginEdit (w->param);
            mode_ = Mode::Turn;
            startNorm_ = o.processor_.parameters().getNormalized (w->param);
            startY_ = e.position.y;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& o = owner_;
        if (mode_ == Mode::Turn)
        {
            const double range = e.mods.isShiftDown() ? 1000.0 : 200.0;
            o.processor_.uiSetNormalised (playId_, juce::jlimit (0.0, 1.0, startNorm_ + (startY_ - e.position.y) / range));
            repaint();
            return;
        }
        const auto p = toCanvas (e.position);
        if (mode_ == Mode::Band)
        {
            band_ = juce::Rectangle<float> (dragStart_, p);
            o.selection_ = bandBase_;
            for (const auto& w : before_.widgets)
                if (band_.intersects (juce::Rectangle<float> (static_cast<float> (w.x), static_cast<float> (w.y), static_cast<float> (w.w), static_cast<float> (w.h))))
                    o.selection_.insert (w.id);
            o.updateButtons();
            repaint();
            return;
        }
        if (mode_ != Mode::Move && mode_ != Mode::Resize) return;
        const int dx = juce::roundToInt (p.x - dragStart_.x), dy = juce::roundToInt (p.y - dragStart_.y);
        if (mode_ == Mode::Move)
        {
            // le groupe se déplace d'un bloc et reste dans le canevas
            int minX = fc::canvasWidth, minY = fc::canvasHeight, maxR = 0, maxB = 0;
            for (const auto& w : before_.widgets)
                if (o.selection_.count (w.id) != 0) { minX = std::min (minX, w.x); minY = std::min (minY, w.y); maxR = std::max (maxR, w.x + w.w); maxB = std::max (maxB, w.y + w.h); }
            const int mx = std::clamp (snapped (dx), -minX, fc::canvasWidth - maxR);
            const int my = std::clamp (snapped (dy), -minY, fc::canvasHeight - maxB);
            for (auto& w : o.facade_.widgets)
                if (const auto* b = before_.find (w.id); b != nullptr && o.selection_.count (w.id) != 0) { w.x = b->x + mx; w.y = b->y + my; }
        }
        else if (auto* w = o.facade_.find (o.single()))
        {
            w->w = std::clamp (snapped (startRect_.getWidth() + dx), fc::minWidgetSize, fc::canvasWidth - w->x);
            w->h = std::clamp (snapped (startRect_.getHeight() + dy), fc::minWidgetSize, fc::canvasHeight - w->y);
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        auto& o = owner_;
        const auto mode = mode_;
        mode_ = Mode::None;
        if (mode == Mode::Turn)
        {
            o.processor_.uiEndEdit (playId_);
            playId_ = 0;
        }
        else if (mode == Mode::Band)
            repaint();
        else if (mode == Mode::Move || mode == Mode::Resize)
        {
            if (o.facade_ != before_) o.commit (o.facade_, mode == Mode::Move ? "Facade: move widgets" : "Facade: resize widget");
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto& o = owner_;
        const auto* w = widgetAt (toCanvas (e.position));
        if (w == nullptr) return;
        if (o.editMode_)
        {
            if (w->type == fc::WidgetType::Action) o.openPicker (true);
            else if (needsParam (w->type)) o.openPicker();
            return;
        }
        if (w->type == fc::WidgetType::Knob && w->param != 0) o.processor_.uiResetToDefault (w->param);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        auto& o = owner_;
        if (o.editMode_) return;
        const auto* w = widgetAt (toCanvas (e.position));
        if (w == nullptr || w->type != fc::WidgetType::Knob || w->param == 0) return;
        auto& ps = o.processor_.parameters();
        if (ps.indexOf (w->param) == neptune::core::ParameterSet::npos) return;
        o.processor_.uiBeginEdit (w->param);
        o.processor_.uiSetNormalised (w->param, juce::jlimit (0.0, 1.0, ps.getNormalized (w->param) + wheel.deltaY * 0.1));
        o.processor_.uiEndEdit (w->param);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto& o = owner_;
        if (! o.editMode_) return false;
        if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'A' || k.getKeyCode() == 'a'))
        {
            for (const auto& w : o.facade_.widgets) o.selection_.insert (w.id);
            o.updateButtons();
            repaint();
            return true;
        }
        if (o.selection_.empty()) return false;
        const int step = k.getModifiers().isShiftDown() ? 10 : 1;
        int dx = 0, dy = 0;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { o.deleteSelected(); return true; }
        if (k.getKeyCode() == juce::KeyPress::leftKey) dx = -step;
        else if (k.getKeyCode() == juce::KeyPress::rightKey) dx = step;
        else if (k.getKeyCode() == juce::KeyPress::upKey) dy = -step;
        else if (k.getKeyCode() == juce::KeyPress::downKey) dy = step;
        else return false;
        auto f = o.facade_;
        int minX = fc::canvasWidth, minY = fc::canvasHeight, maxR = 0, maxB = 0;
        for (const auto& w : f.widgets)
            if (o.selection_.count (w.id) != 0) { minX = std::min (minX, w.x); minY = std::min (minY, w.y); maxR = std::max (maxR, w.x + w.w); maxB = std::max (maxB, w.y + w.h); }
        dx = std::clamp (dx, -minX, fc::canvasWidth - maxR);
        dy = std::clamp (dy, -minY, fc::canvasHeight - maxB);
        for (auto& w : f.widgets)
            if (o.selection_.count (w.id) != 0) { w.x += dx; w.y += dy; }
        o.commit (std::move (f), "Facade: move widgets");
        return true;
    }

private:
    enum class Mode { None, Move, Resize, Turn, Band };

    float scale() const
    {
        return std::min (static_cast<float> (getWidth()) / fc::canvasWidth, static_cast<float> (getHeight()) / fc::canvasHeight);
    }
    juce::Rectangle<float> canvasArea() const
    {
        const float s = scale();
        return juce::Rectangle<float> (fc::canvasWidth * s, fc::canvasHeight * s).withCentre (getLocalBounds().toFloat().getCentre());
    }
    juce::Point<float> toCanvas (juce::Point<float> p) const
    {
        const auto a = canvasArea();
        const float s = scale();
        return { (p.x - a.getX()) / s, (p.y - a.getY()) / s };
    }

    const fc::Widget* widgetAt (juce::Point<float> p) const
    {
        const auto& ws = owner_.facade_.widgets;
        for (auto it = ws.rbegin(); it != ws.rend(); ++it)
            if (juce::Rectangle<float> (static_cast<float> (it->x), static_cast<float> (it->y), static_cast<float> (it->w), static_cast<float> (it->h)).contains (p))
                return &*it;
        return nullptr;
    }

    void editDown (juce::Point<float> p, const juce::ModifierKeys& mods)
    {
        auto& o = owner_;
        before_ = o.facade_;
        dragStart_ = p;
        const bool extend = mods.isShiftDown() || mods.isCommandDown();
        if (const auto* sel = o.facade_.find (o.single()))
        {
            const juce::Rectangle<float> handle (static_cast<float> (sel->x + sel->w) - 14.0f, static_cast<float> (sel->y + sel->h) - 14.0f, 14.0f, 14.0f);
            if (handle.contains (p))
            {
                mode_ = Mode::Resize;
                startRect_ = { sel->x, sel->y, sel->w, sel->h };
                return;
            }
        }
        const auto* w = widgetAt (p);
        if (w != nullptr)
        {
            if (extend)
            {
                if (o.selection_.count (w->id) != 0) o.selection_.erase (w->id); else o.selection_.insert (w->id);
            }
            else if (o.selection_.count (w->id) == 0)
                o.selection_ = { w->id };
            if (o.selection_.count (w->id) != 0) mode_ = Mode::Move;
        }
        else
        {
            if (! extend) o.selection_.clear();
            bandBase_ = o.selection_;
            band_ = {};
            mode_ = Mode::Band;
        }
        o.updateButtons();
        repaint();
    }

    void toggle (neptune::core::ParamId id)
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        const auto* spec = ps.find (id);
        if (spec == nullptr) return;
        const double plain = ps.get (id);
        double next;
        if ((spec->kind == neptune::core::Kind::Choice || spec->kind == neptune::core::Kind::Stepped) && spec->max - spec->min > 1.0)
            next = plain + 1.0 > spec->max ? spec->min : plain + 1.0;   // plusieurs positions : on passe à la suivante
        else
            next = ps.getNormalized (id) >= 0.5 ? spec->min : spec->max;
        o.processor_.uiBeginEdit (id);
        o.processor_.uiSetNormalised (id, spec->toNormalized (next));
        o.processor_.uiEndEdit (id);
    }

    // Menu déroulant : toutes les valeurs du paramètre.
    void showValueMenu (const fc::Widget& w)
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        const auto* spec = ps.find (w.param);
        if (spec == nullptr) return;
        const int count = menuCount (*spec);
        if (count <= 0) return;
        const int current = static_cast<int> (std::lround (ps.get (w.param) - spec->min));
        juce::PopupMenu menu;
        for (int i = 0; i < count; ++i)
            menu.addItem (i + 1, utf8 (ps.toText (*spec, spec->min + i)), true, i == current);
        menu.setLookAndFeel (&getLookAndFeel());
        const auto id = w.param;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (widgetScreenRect (w))),
                            [safe = juce::Component::SafePointer<Canvas> (this), id] (int choice)
                            {
                                if (safe == nullptr || choice <= 0) return;
                                auto& proc = safe->owner_.processor_;
                                const auto* sp = proc.parameters().find (id);
                                if (sp == nullptr) return;
                                proc.uiBeginEdit (id);
                                proc.uiSetNormalised (id, sp->toNormalized (sp->min + (choice - 1)));
                                proc.uiEndEdit (id);
                            });
    }

    juce::Rectangle<int> widgetScreenRect (const fc::Widget& w) const
    {
        const auto a = canvasArea();
        const float s = scale();
        return juce::Rectangle<float> (a.getX() + w.x * s, a.getY() + w.y * s, w.w * s, w.h * s).toNearestInt();
    }

    // Bouton d'action : toutes les valeurs de l'instantané d'un coup (un seul geste, donc une seule étape d'annulation).
    void runAction (const fc::Widget& w)
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        if (w.snapshot.empty()) return;
        o.processor_.uiBeginEdit (w.snapshot.front().first);
        for (const auto& [id, value] : w.snapshot)
            if (const auto* sp = ps.find (id)) o.processor_.uiSetNormalised (id, sp->toNormalized (sp->clamp (value)));
        o.processor_.uiEndEdit (w.snapshot.front().first);
    }

    bool actionIsLit (const fc::Widget& w) const
    {
        auto& ps = owner_.processor_.parameters();
        if (w.snapshot.empty()) return false;
        for (const auto& [id, value] : w.snapshot)
        {
            const auto* sp = ps.find (id);
            if (sp == nullptr || std::abs (ps.get (id) - sp->clamp (value)) > 1.0e-6) return false;
        }
        return true;
    }

    void loadImageIfNeeded()
    {
        const auto& path = owner_.facade_.image;
        if (path == imagePath_) return;
        imagePath_ = path;
        image_ = {};
        if (path.empty()) return;
        const juce::File file (utf8 (path));
        if (file.existsAsFile()) image_ = juce::ImageFileFormat::loadFrom (file);
    }

    void paintWidget (juce::Graphics& g, const fc::Widget& w) const
    {
        auto& o = owner_;
        auto& ps = o.processor_.parameters();
        const auto* spec = w.param != 0 ? ps.find (w.param) : nullptr;
        const bool linked = spec != nullptr;
        const juce::Rectangle<float> b (static_cast<float> (w.x), static_cast<float> (w.y), static_cast<float> (w.w), static_cast<float> (w.h));
        const juce::String caption = w.text.empty() ? (linked ? utf8 (spec->name) : juce::String()) : utf8 (w.text);
        const float n = linked ? static_cast<float> (ps.getNormalized (w.param)) : 0.0f;

        switch (w.type)
        {
            case fc::WidgetType::Knob:
            {
                auto r = b;
                auto valueRow = r.removeFromBottom (15.0f);
                auto captionRow = r.removeFromBottom (17.0f);
                paintNeptuneKnob (g, r, n, o.palette_, o.knobStyle_);
                g.setColour (o.colour (Token::Text));
                g.setFont (juce::FontOptions (14.0f));
                g.drawFittedText (caption, captionRow.toNearestInt(), juce::Justification::centred, 1);
                g.setColour (o.colour (Token::Accent));
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText (linked ? utf8 (ps.toText (w.param)) : (o.editMode_ ? o.text ("facade.unlinked") : juce::String()), valueRow.toNearestInt(), juce::Justification::centred, 1);
                break;
            }
            case fc::WidgetType::Button:
            case fc::WidgetType::Action:
            {
                const bool isAction = w.type == fc::WidgetType::Action;
                const bool on = isAction ? actionIsLit (w) : linked && n >= 0.5f;
                const bool empty = isAction ? w.snapshot.empty() : ! linked;
                const auto r = b.reduced (1.0f);
                g.setColour (on ? o.colour (Token::Accent) : o.colour (Token::Pb));
                g.fillRoundedRectangle (r, 8.0f);
                g.setColour (o.colour (Token::Border));
                g.drawRoundedRectangle (r, 8.0f, 1.5f);
                g.setColour (on ? o.colour (Token::BgApp) : o.colour (Token::Text));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 24.0f, b.getHeight() * 0.42f), juce::Font::bold));
                g.drawFittedText (caption.isEmpty() && empty && o.editMode_ ? o.text ("facade.unlinked") : caption, r.toNearestInt().reduced (6, 2), juce::Justification::centred, 2);
                break;
            }
            case fc::WidgetType::Menu:
            {
                auto r = b.reduced (1.0f);
                auto captionRow = r.removeFromTop (std::min (18.0f, r.getHeight() * 0.4f));
                g.setColour (o.colour (Token::TextDim));
                g.setFont (juce::FontOptions (13.0f));
                g.drawFittedText (caption, captionRow.toNearestInt(), juce::Justification::centredLeft, 1);
                g.setColour (o.colour (Token::Pb));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (o.colour (Token::Border));
                g.drawRoundedRectangle (r, 6.0f, 1.5f);
                g.setColour (o.colour (Token::Text));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 22.0f, r.getHeight() * 0.5f)));
                g.drawFittedText (linked ? utf8 (ps.toText (w.param)) : (o.editMode_ ? o.text ("facade.unlinked") : juce::String()),
                                  r.reduced (8.0f, 2.0f).withTrimmedRight (18.0f).toNearestInt(), juce::Justification::centredLeft, 1);
                juce::Path arrow;
                const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
                arrow.addTriangle (cx - 5.0f, cy - 3.0f, cx + 5.0f, cy - 3.0f, cx, cy + 4.0f);
                g.setColour (o.colour (Token::Accent));
                g.fillPath (arrow);
                break;
            }
            case fc::WidgetType::Curve:
            {
                const bool sampler = o.track_ >= 0 && o.processor_.trackType (o.track_) == neptune::core::TrackType::Sampler;
                auto body = b;
                if (! w.text.empty())
                {
                    g.setColour (o.colour (Token::Text));
                    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
                    g.drawFittedText (utf8 (w.text), body.removeFromTop (curveTitleHeight).toNearestInt(), juce::Justification::centredLeft, 1);
                }
                if (sampler && ! o.editMode_) break;   // le panneau réel est posé par-dessus
                g.setColour (o.colour (Token::BgApp));
                g.fillRoundedRectangle (body.reduced (1.0f), 8.0f);
                g.setColour (o.colour (Token::Border));
                g.drawRoundedRectangle (body.reduced (1.0f), 8.0f, 1.5f);
                g.setColour (o.colour (Token::TextDim));
                g.setFont (juce::FontOptions (16.0f));
                g.drawFittedText (o.text (sampler ? "facade.curve_title" : "facade.curve_unavailable"), body.toNearestInt().reduced (12), juce::Justification::centred, 3);
                break;
            }
            case fc::WidgetType::Label:
                g.setColour (o.colour (Token::Text));
                g.setFont (juce::FontOptions (juce::jlimit (11.0f, 48.0f, b.getHeight() * 0.62f)));
                g.drawFittedText (utf8 (w.text), b.toNearestInt(), juce::Justification::centredLeft, 1);
                break;
        }
    }

    void paintEditOverlay (juce::Graphics& g, const fc::Widget& w) const
    {
        auto& o = owner_;
        const bool sel = o.selection_.count (w.id) != 0;
        const juce::Rectangle<float> b (static_cast<float> (w.x), static_cast<float> (w.y), static_cast<float> (w.w), static_cast<float> (w.h));
        g.setColour (sel ? o.colour (Token::Accent) : o.colour (Token::TextDim2).withAlpha (0.8f));
        g.drawRect (b, sel ? 2.0f : 1.0f);
        if (sel && o.selection_.size() == 1) g.fillRect (juce::Rectangle<float> (b.getRight() - 12.0f, b.getBottom() - 12.0f, 12.0f, 12.0f));
    }

    FacadeView& owner_;
    Mode mode_ = Mode::None;
    fc::Facade before_;
    juce::Point<float> dragStart_;
    juce::Rectangle<int> startRect_;
    juce::Rectangle<float> band_;
    std::set<int> bandBase_;
    neptune::core::ParamId playId_ = 0;
    double startNorm_ = 0.0;
    float startY_ = 0.0f;
    std::string imagePath_;
    juce::Image image_;
    std::map<int, std::unique_ptr<VelocityPanel>> panels_;
    std::uint32_t panelRevision_ = 0xFFFFFFFFu;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Canvas)
};

// ============================== Choix du ou des paramètres ==============================

// Un paramètre (liaison d'un widget) ou plusieurs (instantané d'un bouton d'action : coche par clic).
class FacadeView::Picker : public juce::Component, private juce::ListBoxModel
{
public:
    Picker (FacadeView& owner, int track, bool capture, std::set<neptune::core::ParamId> initial,
            std::function<void (neptune::core::ParamId)> onChoose, std::function<void (const std::set<neptune::core::ParamId>&)> onCapture)
        : owner_ (owner), capture_ (capture), chosen_ (std::move (initial)), onChoose_ (std::move (onChoose)), onCapture_ (std::move (onCapture))
    {
        auto& ps = owner.processor_.parameters();
        for (std::size_t i = 0; i < ps.size(); ++i)
        {
            const auto& s = ps.spec (i);
            if (! s.hidden) entries_.push_back ({ s.id, utf8 (s.name) });
        }
        title_.setText (owner.text (capture ? "facade.capture_title" : "facade.picker_title"), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        hint_.setText (owner.text (capture ? "facade.capture_hint" : "facade.picker_hint"), juce::dontSendNotification);
        hint_.setFont (juce::FontOptions (13.0f));
        hint_.setColour (juce::Label::textColourId, owner.colour (Token::TextDim));
        addAndMakeVisible (title_);
        addAndMakeVisible (hint_);
        filter_.setText ("Track " + juce::String (track + 1) + " ", false);
        filter_.setFont (juce::FontOptions (18.0f));
        filter_.onTextChange = [this] { refilter(); };
        filter_.onReturnKey = [this] { if (capture_) confirm(); else choose (list_.getSelectedRow()); };
        filter_.onEscapeKey = [this] { close(); };
        addAndMakeVisible (filter_);
        list_.setModel (this);
        list_.setRowHeight (26);
        list_.setColour (juce::ListBox::backgroundColourId, owner.colour (Token::BgApp));
        addAndMakeVisible (list_);
        cancel_.setButtonText (owner.text ("facade.picker_cancel"));
        cancel_.onClick = [this] { close(); };
        addAndMakeVisible (cancel_);
        if (capture)
        {
            ok_.setButtonText (owner.text ("facade.capture_ok"));
            ok_.onClick = [this] { confirm(); };
            addAndMakeVisible (ok_);
        }
        else
        {
            unlink_.setButtonText (owner.text ("facade.picker_unlink"));
            unlink_.onClick = [this] { if (onChoose_) onChoose_ (0); close(); };
            addAndMakeVisible (unlink_);
        }
        refilter();
        setWantsKeyboardFocus (true);
    }

    void visibilityChanged() override { if (isVisible()) filter_.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (owner_.colour (Token::PanelBg));
        g.fillRoundedRectangle (panel().toFloat(), 10.0f);
        g.setColour (owner_.colour (Token::Border));
        g.drawRoundedRectangle (panel().toFloat(), 10.0f, 1.5f);
    }

    void resized() override
    {
        auto r = panel().reduced (16);
        title_.setBounds (r.removeFromTop (30));
        hint_.setBounds (r.removeFromTop (36));
        filter_.setBounds (r.removeFromTop (32));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (32);
        r.removeFromBottom (8);
        list_.setBounds (r);
        cancel_.setBounds (bottom.removeFromRight (120));
        bottom.removeFromRight (8);
        (capture_ ? ok_ : unlink_).setBounds (bottom.removeFromRight (140));
    }

private:
    struct Entry { neptune::core::ParamId id; juce::String name; };

    juce::Rectangle<int> panel() const { return juce::Rectangle<int> (560, juce::jmin (620, getHeight() - 20)).withCentre (getLocalBounds().getCentre()); }

    void refilter()
    {
        shown_.clear();
        const auto words = juce::StringArray::fromTokens (filter_.getText().trim(), " ", "");
        for (std::size_t i = 0; i < entries_.size(); ++i)
        {
            bool ok = true;
            for (const auto& w : words)
                if (w.isNotEmpty() && ! entries_[i].name.containsIgnoreCase (w)) { ok = false; break; }
            if (ok) shown_.push_back (i);
        }
        list_.updateContent();
        list_.selectRow (shown_.empty() ? -1 : 0);
        list_.repaint();
    }

    void choose (int row)
    {
        if (row < 0 || row >= static_cast<int> (shown_.size())) return;
        const auto id = entries_[shown_[static_cast<std::size_t> (row)]].id;
        if (onChoose_) onChoose_ (id);
        close();
    }

    void confirm()
    {
        if (onCapture_) onCapture_ (chosen_);
        close();
    }

    void close()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FacadeView> (&owner_)]
        {
            if (safe != nullptr) { safe->picker_.reset(); if (safe->canvas_ != nullptr) safe->canvas_->grabKeyboardFocus(); }
        });
    }

    int getNumRows() override { return static_cast<int> (shown_.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= static_cast<int> (shown_.size())) return;
        const auto& e = entries_[shown_[static_cast<std::size_t> (row)]];
        if (selected) { g.setColour (owner_.colour (Token::Accent).withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
        int x = 8;
        if (capture_)
        {
            const juce::Rectangle<float> box (8.0f, h * 0.5f - 7.0f, 14.0f, 14.0f);
            g.setColour (owner_.colour (Token::Border));
            g.drawRect (box, 1.5f);
            if (chosen_.count (e.id) != 0) { g.setColour (owner_.colour (Token::Accent)); g.fillRect (box.reduced (3.0f)); }
            x = 32;
        }
        g.setColour (owner_.colour (Token::Text));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (e.name, x, 0, w - x - 8, h, juce::Justification::centredLeft);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (! capture_ || row < 0 || row >= static_cast<int> (shown_.size())) return;
        const auto id = entries_[shown_[static_cast<std::size_t> (row)]].id;
        if (chosen_.count (id) != 0) chosen_.erase (id); else chosen_.insert (id);
        list_.repaint();
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (! capture_) choose (row); }

    FacadeView& owner_;
    bool capture_;
    std::set<neptune::core::ParamId> chosen_;
    std::function<void (neptune::core::ParamId)> onChoose_;
    std::function<void (const std::set<neptune::core::ParamId>&)> onCapture_;
    std::vector<Entry> entries_;
    std::vector<std::size_t> shown_;
    juce::Label title_, hint_;
    juce::TextEditor filter_;
    juce::ListBox list_;
    juce::TextButton cancel_, unlink_, ok_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Picker)
};

// ============================== Saisie de texte et aide (calques de la vue) ==============================

// Saisie d'un texte dans un calque de la vue (pas de fenêtre système : fiable dans un hôte), avec OK et Annuler bien visibles.
class FacadeView::TextPrompt : public juce::Component
{
public:
    TextPrompt (FacadeView& owner, const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> onDone)
        : owner_ (owner), onDone_ (std::move (onDone))
    {
        title_.setText (title, juce::dontSendNotification);
        title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        addAndMakeVisible (title_);
        editor_.setText (initial, false);
        editor_.setFont (juce::FontOptions (20.0f));
        editor_.setSelectAllWhenFocused (true);
        editor_.setInputRestrictions (static_cast<int> (fc::maxTextLength));
        editor_.onReturnKey = [this] { accept(); };
        editor_.onEscapeKey = [this] { close(); };
        addAndMakeVisible (editor_);
        ok_.setButtonText (owner.text ("facade.ok"));
        ok_.onClick = [this] { accept(); };
        cancel_.setButtonText (owner.text ("facade.picker_cancel"));
        cancel_.onClick = [this] { close(); };
        addAndMakeVisible (ok_);
        addAndMakeVisible (cancel_);
    }

    void visibilityChanged() override { if (isVisible()) editor_.grabKeyboardFocus(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (owner_.colour (Token::PanelBg));
        g.fillRoundedRectangle (panel().toFloat(), 10.0f);
        g.setColour (owner_.colour (Token::Border));
        g.drawRoundedRectangle (panel().toFloat(), 10.0f, 1.5f);
    }

    void resized() override
    {
        auto r = panel().reduced (18);
        title_.setBounds (r.removeFromTop (32));
        r.removeFromTop (8);
        editor_.setBounds (r.removeFromTop (44));
        r.removeFromTop (16);
        auto row = r.removeFromTop (44);
        cancel_.setBounds (row.removeFromRight (150));
        row.removeFromRight (10);
        ok_.setBounds (row.removeFromRight (150));
    }

private:
    juce::Rectangle<int> panel() const { return juce::Rectangle<int> (480, 190).withCentre (getLocalBounds().getCentre()); }

    void accept()
    {
        const auto text = editor_.getText();
        auto done = onDone_;
        close();
        if (done) done (text);
    }

    void close()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FacadeView> (&owner_)]
        {
            if (safe != nullptr) { safe->prompt_.reset(); if (safe->canvas_ != nullptr) safe->canvas_->grabKeyboardFocus(); }
        });
    }

    FacadeView& owner_;
    std::function<void (const juce::String&)> onDone_;
    juce::Label title_;
    juce::TextEditor editor_;
    juce::TextButton ok_, cancel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextPrompt)
};

// Aide : ce que fait chaque élément de la Façade.
class FacadeView::HelpPanel : public juce::Component
{
public:
    explicit HelpPanel (FacadeView& owner) : owner_ (owner)
    {
        title_.setText (owner.text ("facade.help_title"), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, owner.colour (Token::Text));
        addAndMakeVisible (title_);
        body_.setMultiLine (true, true);
        body_.setReadOnly (true);
        body_.setCaretVisible (false);
        body_.setFont (juce::FontOptions (16.0f));
        body_.setColour (juce::TextEditor::backgroundColourId, owner.colour (Token::BgApp));
        body_.setColour (juce::TextEditor::textColourId, owner.colour (Token::Text));
        body_.setColour (juce::TextEditor::outlineColourId, owner.colour (Token::Border));
        body_.setText (owner.text ("facade.help_body"), false);
        addAndMakeVisible (body_);
        close_.setButtonText (owner.text ("modal.close"));
        close_.onClick = [this] { close(); };
        addAndMakeVisible (close_);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (owner_.colour (Token::PanelBg));
        g.fillRoundedRectangle (panel().toFloat(), 10.0f);
        g.setColour (owner_.colour (Token::Border));
        g.drawRoundedRectangle (panel().toFloat(), 10.0f, 1.5f);
    }

    void resized() override
    {
        auto r = panel().reduced (18);
        title_.setBounds (r.removeFromTop (32));
        r.removeFromTop (6);
        auto bottom = r.removeFromBottom (44);
        r.removeFromBottom (10);
        body_.setBounds (r);
        close_.setBounds (bottom.removeFromRight (150));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

private:
    juce::Rectangle<int> panel() const { return juce::Rectangle<int> (720, juce::jmin (640, getHeight() - 20)).withCentre (getLocalBounds().getCentre()); }

    void close()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FacadeView> (&owner_)]
        {
            if (safe != nullptr) { safe->help_.reset(); if (safe->canvas_ != nullptr) safe->canvas_->grabKeyboardFocus(); }
        });
    }

    FacadeView& owner_;
    juce::Label title_;
    juce::TextEditor body_;
    juce::TextButton close_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
};

// ============================== Vue ==============================

FacadeView::FacadeView (NeptuneProcessor& p, const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr,
                        neptune::core::theme::KnobStyle style, NeptuneKnob::OpenModal openModal)
    : processor_ (p), palette_ (palette), tr_ (tr), knobStyle_ (style), openModal_ (std::move (openModal))
{
    canvas_ = std::make_unique<Canvas> (*this);
    addAndMakeVisible (*canvas_);

    editButton_.setButtonText (text ("facade.edit_layout"));
    editButton_.setClickingTogglesState (true);
    editButton_.onClick = [this]
    {
        editMode_ = editButton_.getToggleState();
        if (! editMode_) selection_.clear();
        updateButtons();
        resized();
        canvas_->syncChildren();
        canvas_->repaint();
    };
    auto setupAdd = [this] (juce::TextButton& b, const char* key, fc::WidgetType type)
    {
        b.setButtonText (text (key));
        b.onClick = [this, type] { addWidget (type); };
    };
    setupAdd (knobButton_, "facade.add_knob", fc::WidgetType::Knob);
    setupAdd (buttonButton_, "facade.add_button", fc::WidgetType::Button);
    setupAdd (menuButton_, "facade.add_menu", fc::WidgetType::Menu);
    setupAdd (actionButton_, "facade.add_action", fc::WidgetType::Action);
    setupAdd (curveButton_, "facade.add_curve", fc::WidgetType::Curve);
    setupAdd (labelButton_, "facade.add_label", fc::WidgetType::Label);
    imageButton_.setButtonText (text ("facade.image"));
    imageButton_.onClick = [this] { chooseImage(); };
    fitButton_.setButtonText (text ("facade.fit_fit"));
    fitButton_.onClick = [this] { showFitMenu(); };
    clearImageButton_.setButtonText (text ("facade.image_clear"));
    clearImageButton_.onClick = [this]
    {
        auto f = facade_;
        f.image.clear();
        commit (std::move (f), "Facade: background");
    };
    linkButton_.setButtonText (text ("facade.link"));
    linkButton_.onClick = [this] { openPicker(); };
    textButton_.setButtonText (text ("facade.text"));
    textButton_.onClick = [this] { editText(); };
    captureButton_.setButtonText (text ("facade.capture"));
    captureButton_.onClick = [this] { openPicker (true); };
    duplicateButton_.setButtonText (text ("facade.duplicate"));
    duplicateButton_.onClick = [this] { duplicateSelected(); };
    deleteButton_.setButtonText (text ("facade.delete"));
    deleteButton_.onClick = [this] { deleteSelected(); };
    alignButton_.setButtonText (text ("facade.align"));
    alignButton_.onClick = [this] { showAlignMenu(); };
    helpButton_.setButtonText ("?");
    helpButton_.setTooltip (text ("facade.help_title"));
    helpButton_.onClick = [this] { openHelp(); };
    editButton_.setTooltip (text ("facade.tip_edit"));
    knobButton_.setTooltip (text ("facade.tip_knob"));
    buttonButton_.setTooltip (text ("facade.tip_button"));
    menuButton_.setTooltip (text ("facade.tip_menu"));
    actionButton_.setTooltip (text ("facade.tip_action"));
    curveButton_.setTooltip (text ("facade.tip_curve"));
    labelButton_.setTooltip (text ("facade.tip_label"));
    captureButton_.setTooltip (text ("facade.tip_capture"));
    alignButton_.setTooltip (text ("facade.tip_align"));

    for (auto* b : { &editButton_, &knobButton_, &buttonButton_, &menuButton_, &actionButton_, &curveButton_, &labelButton_, &imageButton_, &fitButton_, &clearImageButton_,
                     &linkButton_, &textButton_, &captureButton_, &duplicateButton_, &deleteButton_, &alignButton_, &helpButton_ })
        addAndMakeVisible (*b);
    refresh();
    updateButtons();
    startTimerHz (15);
}

FacadeView::~FacadeView() { stopTimer(); }

juce::Colour FacadeView::colour (Token t) const { return toColour (palette_, t); }

void FacadeView::pruneSelection()
{
    for (auto it = selection_.begin(); it != selection_.end();)
        it = facade_.find (*it) != nullptr ? std::next (it) : selection_.erase (it);
}

void FacadeView::updateButtons()
{
    pruneSelection();
    const auto* one = facade_.find (single());
    for (auto* b : { &knobButton_, &buttonButton_, &menuButton_, &actionButton_, &curveButton_, &labelButton_, &imageButton_, &fitButton_, &clearImageButton_ }) b->setVisible (editMode_);
    const bool any = editMode_ && ! selection_.empty();
    textButton_.setVisible (editMode_ && one != nullptr);
    linkButton_.setVisible (editMode_ && one != nullptr && needsParam (one->type));
    captureButton_.setVisible (editMode_ && one != nullptr && one->type == fc::WidgetType::Action);
    duplicateButton_.setVisible (any);
    deleteButton_.setVisible (any);
    alignButton_.setVisible (editMode_ && selection_.size() >= 2);
    clearImageButton_.setEnabled (! facade_.image.empty());
    fitButton_.setButtonText (text (facade_.fit == fc::ImageFit::Fit ? "facade.fit_fit" : facade_.fit == fc::ImageFit::Stretch ? "facade.fit_stretch" : "facade.fit_fill"));
    editButton_.setToggleState (editMode_, juce::dontSendNotification);
}

void FacadeView::resized()
{
    static constexpr int rowH = 28, gap = 6, pad = 24;
    auto fit = [this] (const juce::TextButton& b)
    {
        return juce::roundToInt (std::ceil (getLookAndFeel().getTextButtonFont (const_cast<juce::TextButton&> (b), rowH).getStringWidthFloat (b.getButtonText()))) + pad;
    };
    auto area = getLocalBounds();
    auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<juce::TextButton*> buttons)
    {
        for (auto* b : buttons)
        {
            const int w = fit (*b);
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    };
    {
        auto top = area.removeFromTop (rowH);
        helpButton_.setBounds (top.removeFromRight (rowH + 8));   // « ? » à droite de la première rangée
        layoutRow (top, { &editButton_, &imageButton_, &fitButton_, &clearImageButton_ });
    }
    area.removeFromTop (gap);
    if (editMode_)
    {
        layoutRow (area.removeFromTop (rowH), { &knobButton_, &buttonButton_, &menuButton_, &actionButton_, &curveButton_, &labelButton_ });
        area.removeFromTop (gap);
        layoutRow (area.removeFromTop (rowH), { &linkButton_, &textButton_, &captureButton_, &duplicateButton_, &deleteButton_, &alignButton_ });
        area.removeFromTop (gap);
    }
    canvas_->setBounds (area);
    if (picker_ != nullptr) picker_->setBounds (getLocalBounds());
    if (prompt_ != nullptr) prompt_->setBounds (getLocalBounds());
    if (help_ != nullptr) help_->setBounds (getLocalBounds());
}

void FacadeView::paint (juce::Graphics&) {}

void FacadeView::timerCallback()
{
    if (! isShowing()) return;
    refresh();
    canvas_->syncChildren();
    canvas_->repaint();   // les valeurs peuvent changer (automation, autre vue)
}

void FacadeView::refresh()
{
    const int t = processor_.selectedTrack();
    const auto rev = processor_.instrumentRevision();
    if (t == track_ && rev == revision_) return;
    if (canvas_->dragging()) return;
    if (t != track_) selection_.clear();
    track_ = t;
    revision_ = rev;
    facade_ = processor_.trackSettings (t).facade;
    updateButtons();
    resized();
}

void FacadeView::commit (fc::Facade f, const char* undoName)
{
    if (track_ < 0) return;
    f = fc::sanitised (std::move (f));
    facade_ = f;
    auto s = processor_.trackSettings (track_);
    if (s.facade != f)
    {
        s.facade = f;
        processor_.uiEditTrack (track_, std::move (s), undoName);
        revision_ = processor_.instrumentRevision();
    }
    updateButtons();
    canvas_->syncChildren();
    canvas_->repaint();
}

void FacadeView::addWidget (fc::WidgetType type)
{
    auto f = facade_;
    if (static_cast<int> (f.widgets.size()) >= fc::maxWidgets) return;
    // posé près du centre, décalé à chaque ajout pour ne pas empiler exactement
    const int n = static_cast<int> (f.widgets.size()) % 8;
    auto w = fc::defaultWidget (type, f.freeId(), fc::canvasWidth / 2 - 40 + 24 * n, fc::canvasHeight / 2 - 40 + 24 * n);
    if (type == fc::WidgetType::Label) w.text = "Label";
    selection_ = { w.id };
    f.widgets.push_back (std::move (w));
    commit (std::move (f), "Facade: add widget");
}

void FacadeView::addLinked (fc::WidgetType type, neptune::core::ParamId param)
{
    refresh();
    if (track_ < 0) return;
    auto f = facade_;
    if (static_cast<int> (f.widgets.size()) >= fc::maxWidgets) return;
    const int n = static_cast<int> (f.widgets.size()) % 8;
    auto w = fc::defaultWidget (type, f.freeId(), 60 + 24 * n, 60 + 24 * n);
    w.param = param;
    f.widgets.push_back (std::move (w));
    commit (std::move (f), "Facade: add widget");
}

void FacadeView::duplicateSelected()
{
    auto f = facade_;
    std::set<int> copies;
    const auto original = f.widgets;
    for (const auto& src : original)
    {
        if (selection_.count (src.id) == 0 || static_cast<int> (f.widgets.size()) >= fc::maxWidgets) continue;
        auto copy = src;
        copy.id = f.freeId();
        copy.x += 24;
        copy.y += 24;
        copies.insert (copy.id);
        f.widgets.push_back (std::move (copy));
    }
    if (copies.empty()) return;
    selection_ = copies;
    commit (std::move (f), "Facade: duplicate widgets");
}

void FacadeView::deleteSelected()
{
    auto f = facade_;
    const auto before = f.widgets.size();
    f.widgets.erase (std::remove_if (f.widgets.begin(), f.widgets.end(), [this] (const fc::Widget& w) { return selection_.count (w.id) != 0; }), f.widgets.end());
    if (f.widgets.size() == before) return;
    selection_.clear();
    commit (std::move (f), "Facade: delete widgets");
}

void FacadeView::showAlignMenu()
{
    juce::PopupMenu grid;
    for (int c = 2; c <= 8; ++c) grid.addItem (100 + c, juce::String (c));
    juce::PopupMenu menu;
    menu.addItem (1, text ("facade.align_left"));
    menu.addItem (2, text ("facade.align_right"));
    menu.addItem (3, text ("facade.align_top"));
    menu.addItem (4, text ("facade.align_bottom"));
    menu.addItem (5, text ("facade.align_centre_h"));
    menu.addItem (6, text ("facade.align_centre_v"));
    menu.addSeparator();
    menu.addItem (7, text ("facade.distribute_h"), selection_.size() >= 3);
    menu.addItem (8, text ("facade.distribute_v"), selection_.size() >= 3);
    menu.addSubMenu (text ("facade.grid"), grid);
    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&alignButton_), [safe = juce::Component::SafePointer<FacadeView> (this)] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        auto f = safe->facade_;
        const auto& ids = safe->selection_;
        if (choice <= 6) fc::alignWidgets (f, ids, static_cast<fc::Align> (choice - 1));
        else if (choice == 7 || choice == 8) fc::distributeWidgets (f, ids, choice == 7);
        else fc::arrangeGrid (f, ids, choice - 100);
        safe->commit (std::move (f), "Facade: arrange widgets");
    });
}

void FacadeView::chooseImage()
{
    chooser_ = std::make_unique<juce::FileChooser> (text ("facade.choose_image"), juce::File::getSpecialLocation (juce::File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<FacadeView> (this)] (const juce::FileChooser& c)
                           {
                               if (safe == nullptr) return;
                               const auto file = c.getResult();
                               if (! file.existsAsFile()) return;
                               auto f = safe->facade_;
                               f.image = file.getFullPathName().toStdString();
                               safe->commit (std::move (f), "Facade: background");
                           });
}

void FacadeView::showFitMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, text ("facade.fit_fit"), true, facade_.fit == fc::ImageFit::Fit);
    menu.addItem (2, text ("facade.fit_stretch"), true, facade_.fit == fc::ImageFit::Stretch);
    menu.addItem (3, text ("facade.fit_fill"), true, facade_.fit == fc::ImageFit::Fill);
    menu.setLookAndFeel (&getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fitButton_), [safe = juce::Component::SafePointer<FacadeView> (this)] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        auto f = safe->facade_;
        f.fit = static_cast<fc::ImageFit> (choice - 1);
        safe->commit (std::move (f), "Facade: background fit");
    });
}

void FacadeView::openPicker (bool capture)
{
    const auto* current = facade_.find (single());
    if (picker_ != nullptr || current == nullptr) return;
    const int id = current->id;
    std::set<neptune::core::ParamId> initial;
    if (capture)
        for (const auto& e : current->snapshot) initial.insert (e.first);
    picker_ = std::make_unique<Picker> (*this, track_, capture, std::move (initial),
        [safe = juce::Component::SafePointer<FacadeView> (this), id] (neptune::core::ParamId param)
        {
            if (safe == nullptr) return;
            auto f = safe->facade_;
            if (auto* w = f.find (id)) { w->param = param; safe->commit (std::move (f), "Facade: link parameter"); }
        },
        [safe = juce::Component::SafePointer<FacadeView> (this), id] (const std::set<neptune::core::ParamId>& ids)
        {
            if (safe == nullptr) return;
            auto f = safe->facade_;
            if (auto* w = f.find (id))
            {
                w->snapshot.clear();
                for (const auto pid : ids)
                    if (safe->processor_.parameters().find (pid) != nullptr) w->snapshot.emplace_back (pid, safe->processor_.parameters().get (pid));
                safe->commit (std::move (f), "Facade: capture values");
            }
        });
    addAndMakeVisible (*picker_);
    picker_->setBounds (getLocalBounds());
    picker_->toFront (true);
}

void FacadeView::editText()
{
    const auto* current = facade_.find (single());
    if (current == nullptr || prompt_ != nullptr) return;
    const int id = current->id;
    prompt_ = std::make_unique<TextPrompt> (*this, text ("facade.text_title"), utf8 (current->text),
        [safe = juce::Component::SafePointer<FacadeView> (this), id] (const juce::String& value)
        {
            if (safe == nullptr) return;
            auto f = safe->facade_;
            if (auto* widget = f.find (id)) { widget->text = value.toStdString(); safe->commit (std::move (f), "Facade: widget text"); }
        });
    addAndMakeVisible (*prompt_);
    prompt_->setBounds (getLocalBounds());
    prompt_->toFront (true);
}

void FacadeView::openHelp()
{
    if (help_ != nullptr) return;
    help_ = std::make_unique<HelpPanel> (*this);
    addAndMakeVisible (*help_);
    help_->setBounds (getLocalBounds());
    help_->toFront (true);
    help_->grabKeyboardFocus();
}
