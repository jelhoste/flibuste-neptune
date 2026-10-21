#pragma once
// Briques partagées des éditeurs NOVA de synthétiseurs (AnalogSynth, FM6Op) : curseur lié à une valeur de la description du patch (non automatisable),
// pastille de valeur entière glissable, pastille à menu, grille de tuiles de presets (page MATRICE).
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "NovaKit.h"
#include "PresetHost.h"
#include "neptune/core/Preset.h"
#include "neptune/core/PresetLibrary.h"

namespace nova
{
// Curseur horizontal sur une valeur lue / écrite par fonctions (description du patch). `set` est appelé à chaque changement ; `gestureBegin/End`
// entourent le glissé (une seule entrée d'annulation).
class ValueSlider : public juce::Component
{
public:
    ValueSlider (const Skin& s, juce::String title, double lo, double hi, double step, juce::String suffix, std::function<double()> get, std::function<void (double)> set,
                 double defaultValue)
        : skin_ (s), title_ (std::move (title)), lo_ (lo), hi_ (hi), step_ (step), suffix_ (std::move (suffix)), get_ (std::move (get)), set_ (std::move (set)), default_ (defaultValue) {}

    std::function<void()> gestureBegin, gestureEnd;
    std::function<juce::String (double)> format;   // texte de la valeur (sinon nombre et suffixe)

    void update()
    {
        if (dragging_) return;
        const double v = get_();
        if (v == shown_) return;
        shown_ = v;
        repaint();
    }
    void setActive (bool a) { if (a != active_) { active_ = a; setAlpha (a ? 1.0f : 0.4f); setInterceptsMouseClicks (a, a); } }
    void paint (juce::Graphics& g) override
    {
        using neptune::core::theme::Token;
        const auto b = getLocalBounds().toFloat();
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (11.0f, true));
        g.drawText (title_, b.withHeight (skin_.px (15.0f)), juce::Justification::centredLeft, true);
        const auto t = track();
        g.setColour (skin_.colour (Token::Grid));
        g.fillRoundedRectangle (t, t.getHeight() / 2);
        const float x = t.getX() + static_cast<float> (std::clamp ((shown_ - lo_) / (hi_ - lo_), 0.0, 1.0)) * t.getWidth();
        g.setColour (skin_.colour (Token::Accent));
        g.fillRoundedRectangle (t.withRight (std::max (t.getX() + 1.0f, x)), t.getHeight() / 2);
        const float hd = skin_.px (12.0f);
        g.setColour (skin_.colour (Token::Hring));
        g.fillEllipse (x - hd / 2 - 2.0f, t.getCentreY() - hd / 2 - 2.0f, hd + 4.0f, hd + 4.0f);
        g.setColour (skin_.colour (Token::Handle));
        g.fillEllipse (x - hd / 2, t.getCentreY() - hd / 2, hd, hd);
        g.setColour (skin_.colour (Token::TextDim));
        g.setFont (skin_.font (12.0f));
        const int decimals = step_ >= 1.0 ? 0 : step_ >= 0.1 ? 1 : 2;
        g.drawText (format ? format (shown_) : juce::String (shown_, decimals) + suffix_, b.withTop (t.getBottom() + skin_.px (3.0f)).withHeight (skin_.px (16.0f)),
                    juce::Justification::centredLeft, true);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        dragging_ = true;
        if (gestureBegin) gestureBegin();
        applyFromX (e.position.x);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging_) applyFromX (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging_) return;
        dragging_ = false;
        if (gestureEnd) gestureEnd();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (gestureBegin) gestureBegin();
        shown_ = default_;
        set_ (default_);
        if (gestureEnd) gestureEnd();
        repaint();
    }

private:
    juce::Rectangle<float> track() const
    {
        const auto b = getLocalBounds().toFloat();
        return { b.getX() + skin_.px (6.0f), b.getY() + skin_.px (22.0f), b.getWidth() - skin_.px (12.0f), skin_.px (6.0f) };
    }
    void applyFromX (float x)
    {
        const auto t = track();
        double v = lo_ + std::clamp (static_cast<double> ((x - t.getX()) / t.getWidth()), 0.0, 1.0) * (hi_ - lo_);
        if (step_ > 0.0) v = lo_ + std::round ((v - lo_) / step_) * step_;
        shown_ = std::clamp (v, lo_, hi_);
        set_ (shown_);
        repaint();
    }
    const Skin& skin_;
    juce::String title_;
    double lo_, hi_, step_;
    juce::String suffix_;
    std::function<double()> get_;
    std::function<void (double)> set_;
    double default_;
    double shown_ = -1.0e30;
    bool dragging_ = false, active_ = true;
};

// Pastille de valeur entière : glisser vers le haut / le bas, ou molette (Maj : plus lent).
class NumberPill : public juce::Component
{
public:
    NumberPill (const Skin& s, juce::String label, int lo, int hi, std::function<int()> get, std::function<void (int)> set, std::function<juce::String (int)> text = {})
        : skin_ (s), label_ (std::move (label)), lo_ (lo), hi_ (hi), get_ (std::move (get)), set_ (std::move (set)), text_ (std::move (text)) {}

    std::function<void()> gestureBegin, gestureEnd;
    void update()
    {
        if (dragging_) return;
        const int v = get_();
        if (v == shown_) return;
        shown_ = v;
        repaint();
    }
    void setActive (bool a) { if (a != active_) { active_ = a; setAlpha (a ? 1.0f : 0.4f); setInterceptsMouseClicks (a, a); } }
    void paint (juce::Graphics& g) override
    {
        using neptune::core::theme::Token;
        const auto b = getLocalBounds().toFloat().reduced (1.0f);
        skin_.fill (g, Token::StageBg, b, skin_.px (8.0f));
        g.setColour (skin_.colour (Token::Grid));
        g.drawRoundedRectangle (b, skin_.px (8.0f), skin_.px (1.0f));
        g.setColour (skin_.colour (Token::TextDim2));
        g.setFont (skin_.font (10.0f, true));
        g.drawText (label_, b.reduced (skin_.px (8.0f), 0.0f), juce::Justification::centredLeft, true);
        g.setColour (skin_.colour (Token::Text));
        g.setFont (skin_.font (13.0f, true));
        g.drawText (text_ ? text_ (shown_) : juce::String (shown_), b.reduced (skin_.px (8.0f), 0.0f), juce::Justification::centredRight, true);
    }
    void mouseDown (const juce::MouseEvent& e) override { dragging_ = ! e.mods.isPopupMenu(); start_ = shown_; if (dragging_ && gestureBegin) gestureBegin(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging_) return;
        const int steps = static_cast<int> (std::lround (-static_cast<float> (e.getDistanceFromDragStartY()) / (skin_.px (8.0f) * (e.mods.isShiftDown() ? 4.0f : 1.0f))));
        const int v = std::clamp (start_ + steps, lo_, hi_);
        if (v != shown_) { shown_ = v; set_ (v); repaint(); }
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging_ && gestureEnd) gestureEnd(); dragging_ = false; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (gestureBegin) gestureBegin();
        const int v = std::clamp (get_() + (w.deltaY > 0 ? 1 : -1), lo_, hi_);
        shown_ = v;
        set_ (v);
        if (gestureEnd) gestureEnd();
        repaint();
    }

private:
    const Skin& skin_;
    juce::String label_;
    int lo_, hi_, shown_ = -1000, start_ = 0;
    std::function<int()> get_;
    std::function<void (int)> set_;
    std::function<juce::String (int)> text_;
    bool dragging_ = false, active_ = true;
};

// Pastille à menu : « libellé  choix courant » ; un clic ouvre la liste.
struct ChoicePill
{
    Pill* pill = nullptr;
    std::function<juce::String()> label;
    std::vector<juce::String> items;
    std::function<int()> get;
    std::function<void (int)> set;
    juce::String shown;
    void refresh (bool enabled = true)
    {
        const int i = std::clamp (get(), 0, std::max (0, static_cast<int> (items.size()) - 1));
        const auto l = label();
        const auto t = (l.isEmpty() ? juce::String() : l + "  ") + (items.empty() ? juce::String() : items[static_cast<std::size_t> (i)]);
        if (t != shown) { shown = t; pill->setText (t); }
        pill->setAlpha (enabled ? 1.0f : 0.4f);
        pill->setInterceptsMouseClicks (enabled, enabled);
    }
    void attach (juce::Component& owner)
    {
        pill->onClick = [this, safe = juce::Component::SafePointer<juce::Component> (&owner)]
        {
            juce::PopupMenu menu;
            for (std::size_t i = 0; i < items.size(); ++i) menu.addItem (static_cast<int> (i) + 1, items[i], true, static_cast<int> (i) == get());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pill), [this, safe] (int r) { if (safe != nullptr && r > 0) set (r - 1); });
        };
    }
};

// Grille de tuiles de presets d'une banque (page MATRICE, pour le concert) : un clic charge le preset.
class PresetTiles : public juce::Component
{
public:
    PresetTiles (const Skin& s, PresetHost& host) : skin_ (s), host_ (host) {}

    int cols = 4, rows = 3, bank = 0, field = 0;   // field : 0 nom du preset, 1 morceau, 2 atelier, 3 élève, 4 événement
    std::function<void()> onLoaded;

    void update()
    {
        const auto rev = host_.presetRevision();
        const auto id = host_.currentPresetId();
        const auto key = static_cast<std::uint32_t> (cols * 1000 + rows * 100 + bank * 7 + field);
        if (rev == rev_ && id == id_ && key == key_) return;
        rev_ = rev;
        id_ = id;
        key_ = key;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        using neptune::core::theme::Token;
        namespace preset = neptune::core::preset;
        const auto& lib = host_.library();
        const auto current = host_.currentPresetId();
        for (int i = 0; i < cols * rows; ++i)
        {
            const auto r = tile (i).reduced (skin_.px (4.0f));
            const auto presetId = preset::Navigator::presetAt (lib.banks(), { bank, i });
            const auto* p = presetId.empty() ? nullptr : lib.find (presetId);
            if (p != nullptr && ! host_.acceptsPreset (*p)) p = nullptr;
            const bool active = p != nullptr && presetId == current;
            skin_.fill (g, active ? Token::Accent : Token::StageBg, r, skin_.px (8.0f));
            g.setColour (skin_.colour (active ? Token::Accent : Token::Grid, active ? 1.0f : 0.9f));
            g.drawRoundedRectangle (r, skin_.px (8.0f), skin_.px (active ? 2.0f : 1.0f));
            g.setColour (skin_.colour (active ? Token::BgApp : Token::TextDim2));
            g.setFont (skin_.font (11.0f, true));
            g.drawText (juce::String (i + 1), r.withTrimmedLeft (skin_.px (8.0f)).withHeight (skin_.px (18.0f)), juce::Justification::centredLeft);
            if (p == nullptr)
            {
                g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94"), r, juce::Justification::centred);
                continue;
            }
            const auto main = textOf (*p, field);
            g.setColour (skin_.colour (active ? Token::BgApp : Token::Text));
            g.setFont (skin_.font (cols <= 4 ? 17.0f : 13.0f, true));
            g.drawText (main.isEmpty() ? juce::String::fromUTF8 (p->name.c_str()) : main, r.reduced (skin_.px (10.0f), skin_.px (18.0f)).withHeight (r.getHeight() * 0.5f),
                        juce::Justification::centredLeft, true);
            g.setFont (skin_.font (cols <= 4 ? 12.0f : 10.0f));
            g.setColour (skin_.colour (active ? Token::BgApp : Token::TextDim));
            float y = r.getCentreY();
            for (int f = 0; f < 5; ++f)
            {
                if (f == field) continue;
                const auto t = textOf (*p, f);
                if (t.isEmpty() || y + skin_.px (14.0f) > r.getBottom()) continue;
                g.drawText (t, juce::Rectangle<float> (r.getX() + skin_.px (10.0f), y, r.getWidth() - skin_.px (20.0f), skin_.px (14.0f)), juce::Justification::centredLeft, true);
                y += skin_.px (14.0f);
            }
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        namespace preset = neptune::core::preset;
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() > 6) return;
        for (int i = 0; i < cols * rows; ++i)
            if (tile (i).contains (e.position))
            {
                const auto id = preset::Navigator::presetAt (host_.library().banks(), { bank, i });
                const auto* p = id.empty() ? nullptr : host_.library().find (id);
                if (p != nullptr && host_.acceptsPreset (*p) && host_.loadPreset (id) && onLoaded) onLoaded();
                repaint();
                return;
            }
    }

private:
    juce::Rectangle<float> tile (int i) const
    {
        const float w = static_cast<float> (getWidth()) / static_cast<float> (cols), h = static_cast<float> (getHeight()) / static_cast<float> (rows);
        return { static_cast<float> (i % cols) * w, static_cast<float> (i / cols) * h, w, h };
    }
    static juce::String textOf (const neptune::core::preset::Preset& p, int f)
    {
        if (f == 0) return juce::String::fromUTF8 (p.name.c_str());
        static const std::vector<std::string> keys[5] = { {}, { "song", "morceau", "piece", "pi\xc3\xa8" "ce" }, { "workshop", "atelier" }, { "student", "\xc3\xa9" "l\xc3\xa8" "ve", "eleve" },
                                                          { "event", "\xc3\xa9v\xc3\xa9nement", "evenement", "concert" } };
        for (const auto& cf : p.customFields)
        {
            std::string k = cf.key;
            std::transform (k.begin(), k.end(), k.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
            for (const auto& wanted : keys[f]) if (k == wanted) return juce::String::fromUTF8 (cf.value.c_str());
        }
        return {};
    }

    const Skin& skin_;
    PresetHost& host_;
    std::uint32_t rev_ = 0xFFFFFFFFu, key_ = 0xFFFFFFFFu;
    std::string id_;
};
} // namespace nova
