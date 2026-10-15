#include "OrganView.h"
#include "neptune/core/Mixer.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace
{
constexpr int kDrawbarW = 42, kDrawbarH = 190;
const char* const kFootage[9] = { "16'", "5 1/3'", "8'", "4'", "2 2/3'", "2'", "1 3/5'", "1 1/3'", "1'" };
// couleurs des tirettes : brun, brun, blanc, blanc, noir, blanc, noir, noir, blanc
const int kDrawbarKind[9] = { 0, 0, 1, 1, 2, 1, 2, 2, 1 };
} // namespace

// Tirette : le haut est « rentrée » (0), le bas « sortie » (8), comme sur la console.
class OrganView::Drawbar : public juce::Component
{
public:
    Drawbar (ParamHost& host, int kind, int offset, const juce::String& footage, const theme::Palette& pal)
        : host_ (host), kind_ (kind), offset_ (offset), footage_ (footage), palette_ (pal) {}

    void setTrack (int track) { id_ = mixer::organParam (track, offset_); repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
        const juce::Colour bodyColour = kind_ == 0 ? juce::Colour (0xff6b3f1d) : kind_ == 1 ? juce::Colour (0xfff2eadb) : juce::Colour (0xff1c1a18);
        g.setColour (c (Token::TextDim));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (footage_, b.removeFromTop (16.0f).toNearestInt(), juce::Justification::centred);
        auto valueArea = b.removeFromBottom (18.0f);
        auto slot = b.reduced (b.getWidth() * 0.3f, 2.0f);
        g.setColour (c (Token::Grid));
        g.fillRoundedRectangle (slot, 3.0f);
        const int v = value();
        const float y = slot.getY() + slot.getHeight() * (static_cast<float> (v) / 8.0f);
        g.setColour (bodyColour);
        g.fillRoundedRectangle (b.getX() + 3.0f, juce::jmax (slot.getY(), y - 3.0f), b.getWidth() - 6.0f,
                                slot.getBottom() - juce::jmax (slot.getY(), y - 3.0f), 3.0f);
        g.setColour (c (Token::Accent));
        g.fillRoundedRectangle (b.getX() + 1.0f, y - 6.0f, b.getWidth() - 2.0f, 12.0f, 3.0f);
        g.setColour (c (Token::Text));
        g.drawText (juce::String (v), valueArea.toNearestInt(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override { host_.uiBeginEdit (id_); set (e); }
    void mouseDrag (const juce::MouseEvent& e) override { set (e); }
    void mouseUp (const juce::MouseEvent&) override { host_.uiEndEdit (id_); }
    void mouseDoubleClick (const juce::MouseEvent&) override { host_.uiResetToDefault (id_); repaint(); }

    int value() const { return juce::jlimit (0, 8, static_cast<int> (std::lround (host_.parameters().get (id_)))); }

private:
    void set (const juce::MouseEvent& e)
    {
        const auto b = getLocalBounds().toFloat();
        const float top = b.getY() + 18.0f, h = b.getHeight() - 18.0f - 20.0f;
        const float p = juce::jlimit (0.0f, 1.0f, (e.position.y - top) / juce::jmax (1.0f, h));
        host_.uiSetNormalised (id_, static_cast<double> (std::lround (p * 8.0f)) / 8.0);
        repaint();
    }

    ParamHost& host_;
    ParamId id_ = 0;
    int kind_, offset_;
    juce::String footage_;
    const theme::Palette& palette_;
};

class OrganView::Switch : public juce::Component
{
public:
    Switch (ParamHost& host, int offset, const juce::String& label, const theme::Palette& pal)
        : host_ (host), offset_ (offset), label_ (label), palette_ (pal) {}

    void setTrack (int track) { id_ = mixer::organParam (track, offset_); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto c = [this] (Token t) { const auto& x = palette_.color (t); return juce::Colour (x.r, x.g, x.b, static_cast<float> (x.a)); };
        const bool on = host_.parameters().get (id_) >= 0.5;
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (on ? c (Token::Accent) : c (Token::Grid));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (c (Token::CtlBd));
        g.drawRoundedRectangle (b, 5.0f, 1.0f);
        g.setColour (on ? c (Token::BgApp) : c (Token::Text));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label_, getLocalBounds(), juce::Justification::centred, true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        const double next = host_.parameters().get (id_) >= 0.5 ? 0.0 : 1.0;
        host_.uiBeginEdit (id_);
        host_.uiSetNormalised (id_, next);
        host_.uiEndEdit (id_);
        repaint();
    }

private:
    ParamHost& host_;
    ParamId id_ = 0;
    int offset_;
    juce::String label_;
    const theme::Palette& palette_;
};

struct OrganView::ComboLink
{
    juce::ComboBox box;
    int offset = 0;
    ParamId id = 0;
};

juce::Colour OrganView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

OrganView::OrganView (ParamHost& host, const theme::Palette& palette, const i18n::Translator& tr, theme::KnobStyle knobStyle,
                      KnobIds extraKnobs, OpenModal openModal)
    : host_ (host), palette_ (palette), tr_ (tr), knobStyle_ (knobStyle), extraIds_ (std::move (extraKnobs)), openModal_ (std::move (openModal))
{
    namespace mx = neptune::core::mixer;
    for (int i = 0; i < 9; ++i) addAndMakeVisible (drawbars_.add (new Drawbar (host_, kDrawbarKind[i], mx::kOrgUpper0 + i, kFootage[i], palette_)));
    for (int i = 0; i < 9; ++i) addAndMakeVisible (drawbars_.add (new Drawbar (host_, kDrawbarKind[i], mx::kOrgLower0 + i, kFootage[i], palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (host_, 0, mx::kOrgPedal16, "16'", palette_)));
    addAndMakeVisible (drawbars_.add (new Drawbar (host_, 1, mx::kOrgPedal8, "8'", palette_)));

    auto sw = [this] (int kind, const char* key)
    {
        addAndMakeVisible (switches_.add (new Switch (host_, kind, text (key), palette_)));
    };
    sw (mx::kOrgPercOn, "organ.perc_on");
    sw (mx::kOrgPercSoft, "organ.perc_soft");
    sw (mx::kOrgPercFast, "organ.perc_fast");
    sw (mx::kOrgPercThird, "organ.perc_third");
    sw (mx::kOrgVibUpper, "organ.vib_upper");
    sw (mx::kOrgVibLower, "organ.vib_lower");
    sw (mx::kOrgDriveOn, "organ.drive");

    for (const int kind : { static_cast<int> (mx::kOrgVibMode), static_cast<int> (mx::kOrgLeslieSpeed) })
    {
        auto* link = combos_.add (new ComboLink);
        link->offset = kind;
        link->id = mx::organParam (0, kind);
        const auto index = host_.parameters().indexOf (link->id);
        if (index != ParameterSet::npos)
        {
            int itemId = 1;
            for (const auto& choice : host_.parameters().spec (index).choices)
                link->box.addItem (juce::String::fromUTF8 (choice.c_str()), itemId++);
        }
        link->box.onChange = [this, link]
        {
            if (updating_) return;
            const int count = link->box.getNumItems();
            if (count < 2) return;
            host_.uiBeginEdit (link->id);
            host_.uiSetNormalised (link->id, static_cast<double> (link->box.getSelectedId() - 1) / static_cast<double> (count - 1));
            host_.uiEndEdit (link->id);
        };
        addAndMakeVisible (link->box);
    }

    // potards : Leslie dry/wet, overdrive (3), réverb ; puis ceux de l'instrument
    for (const int kind : { static_cast<int> (mx::kOrgLeslieMix), static_cast<int> (mx::kOrgDriveInput), static_cast<int> (mx::kOrgDriveCharacter),
                            static_cast<int> (mx::kOrgDriveOutput), static_cast<int> (mx::kOrgReverb) })
        addAndMakeVisible (knobs_.add (new NeptuneKnob (host_, mx::organParam (0, kind), palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { if (openModal_) openModal_ (pid, t); })));
    for (const auto& f : extraIds_)
        addAndMakeVisible (knobs_.add (new NeptuneKnob (host_, f (0), palette_, knobStyle_,
                                                        [this] (ParamId pid, uistate::Terminal t) { if (openModal_) openModal_ (pid, t); })));

    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].addItem (text ("organ.off"), 1);
        for (int c = 1; c <= 16; ++c) channelBox_[m].addItem (juce::String (c), c + 1);
        for (int t = -24; t <= 24; ++t) transposeBox_[m].addItem ((t > 0 ? "+" : "") + juce::String (t), t + 25);
        channelBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = description_;
            d.channel[static_cast<std::size_t> (m)] = channelBox_[m].getSelectedId() - 1;
            description_ = d;
            if (onChange) onChange (d);
        };
        transposeBox_[m].onChange = [this, m]
        {
            if (updating_) return;
            auto d = description_;
            d.transpose[static_cast<std::size_t> (m)] = transposeBox_[m].getSelectedId() - 25;
            description_ = d;
            if (onChange) onChange (d);
        };
        addAndMakeVisible (channelBox_[m]);
        addAndMakeVisible (transposeBox_[m]);
    }
    for (auto* s : { &splitLower_, &splitUpper_ })
    {
        s->setRange (0.0, 127.0, 1.0);
        s->setSliderStyle (juce::Slider::LinearBar);
        addAndMakeVisible (*s);
    }
    splitLower_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = description_;
        d.splitLower = static_cast<int> (splitLower_.getValue());
        description_ = d;
        if (onChange) onChange (d);
    };
    splitUpper_.onValueChange = [this]
    {
        if (updating_) return;
        auto d = description_;
        d.splitUpper = static_cast<int> (splitUpper_.getValue());
        description_ = d;
        if (onChange) onChange (d);
    };

    bindTrack();
    setDescription (description_);
}

OrganView::~OrganView() = default;

void OrganView::bindTrack()
{
    for (auto* d : drawbars_) d->setTrack (track_);
    for (auto* s : switches_) s->setTrack (track_);
    for (auto* c : combos_) c->id = mixer::organParam (track_, c->offset);
    const int own = 5; // les potards propres de l'orgue
    static const int kinds[own] = { mixer::kOrgLeslieMix, mixer::kOrgDriveInput, mixer::kOrgDriveCharacter, mixer::kOrgDriveOutput, mixer::kOrgReverb };
    for (int i = 0; i < own && i < knobs_.size(); ++i) knobs_[i]->setParamId (mixer::organParam (track_, kinds[i]));
    for (std::size_t i = 0; i < extraIds_.size() && own + static_cast<int> (i) < knobs_.size(); ++i)
        knobs_[own + static_cast<int> (i)]->setParamId (extraIds_[i] (track_));
    refreshFromParameters();
    repaint();
}

void OrganView::setTrack (int track)
{
    if (track == track_) return;
    track_ = track;
    bindTrack();
}

void OrganView::setDescription (const organ::OrganDescription& d)
{
    description_ = d;
    updating_ = true;
    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].setSelectedId (d.channel[static_cast<std::size_t> (m)] + 1, juce::dontSendNotification);
        transposeBox_[m].setSelectedId (d.transpose[static_cast<std::size_t> (m)] + 25, juce::dontSendNotification);
    }
    splitLower_.setValue (d.splitLower, juce::dontSendNotification);
    splitUpper_.setValue (d.splitUpper, juce::dontSendNotification);
    updating_ = false;
}

void OrganView::refreshFromParameters()
{
    updating_ = true;
    for (auto* link : combos_)
    {
        const int v = static_cast<int> (std::lround (host_.parameters().get (link->id))) + 1;
        if (link->box.getSelectedId() != v) link->box.setSelectedId (v, juce::dontSendNotification);
    }
    updating_ = false;
    for (auto* d : drawbars_) d->repaint();
    for (auto* s : switches_) s->repaint();
    repaint();
}

void OrganView::placeScaled (juce::Rectangle<int> area)
{
    lastArea_ = area;
    const float s = juce::jmin (1.0f, static_cast<float> (area.getWidth()) / designWidth, static_cast<float> (area.getHeight()) / designHeight);
    const float scale = juce::jmax (0.4f, s);
    setTransform (juce::AffineTransform::scale (scale));
    // la transformation de JUCE agit autour de l'origine du PARENT : les limites sont exprimées en repère du parent / échelle
    const auto x = juce::roundToInt (static_cast<float> (area.getX()) / scale);
    const auto y = juce::roundToInt (static_cast<float> (area.getY()) / scale);
    const auto w = juce::roundToInt (static_cast<float> (area.getWidth()) / scale);
    const auto h = juce::roundToInt (static_cast<float> (area.getHeight()) / scale);
    if (getWidth() == w && getHeight() == h) resized();
    setBounds (x, y, w, h);
}

void OrganView::resized()
{
    sections_.clear();
    auto r = getLocalBounds().reduced (4, 2);

    // tirettes
    auto row = r.removeFromTop (kDrawbarH + 34);
    int x = row.getX();
    auto place = [&] (int firstIndex, int count, const juce::String& title)
    {
        const int width = count * (kDrawbarW + 4) + 12;
        sections_.push_back ({ title, { x, row.getY(), width, row.getHeight() } });
        for (int i = 0; i < count; ++i)
            drawbars_[firstIndex + i]->setBounds (x + 6 + i * (kDrawbarW + 4), row.getY() + 26, kDrawbarW, kDrawbarH);
        x += width + 14;
    };
    place (0, 9, text ("organ.upper"));
    place (9, 9, text ("organ.lower"));
    place (18, 2, text ("organ.pedal"));

    r.removeFromTop (10);
    auto ctl = r.removeFromTop (124);
    auto cell = [&] (int width, const juce::String& title) -> juce::Rectangle<int>
    {
        auto a = ctl.removeFromLeft (width);
        ctl.removeFromLeft (10);
        sections_.push_back ({ title, a });
        return a.withTrimmedTop (24).reduced (4, 2);
    };
    {
        auto a = cell (190, text ("organ.percussion"));
        const int w = a.getWidth() / 2, h = juce::jmin (36, a.getHeight() / 2);
        switches_[0]->setBounds (a.getX(), a.getY(), w, h);
        switches_[1]->setBounds (a.getX() + w, a.getY(), w, h);
        switches_[2]->setBounds (a.getX(), a.getY() + h + 2, w, h);
        switches_[3]->setBounds (a.getX() + w, a.getY() + h + 2, w, h);
    }
    {
        auto a = cell (170, text ("organ.vibrato"));
        combos_[0]->box.setBounds (a.removeFromTop (28));
        a.removeFromTop (6);
        const int w = a.getWidth() / 2;
        switches_[4]->setBounds (a.getX(), a.getY(), w, 32);
        switches_[5]->setBounds (a.getX() + w, a.getY(), w, 32);
    }
    {
        auto a = cell (170, text ("organ.leslie"));
        combos_[1]->box.setBounds (a.removeFromLeft (90).withHeight (28));
        knobs_[0]->setBounds (a.getX() + 6, a.getY(), 48, 48);
    }
    {
        auto a = cell (260, text ("organ.drive"));
        switches_[6]->setBounds (a.getX(), a.getY() + a.getHeight() - 30, 72, 28);
        for (int i = 0; i < 3; ++i) knobs_[1 + i]->setBounds (a.getX() + 76 + i * 62, a.getY(), 48, 48);
    }
    {
        // réverb puis les potards d'instrument (volume, gain...)
        const int extra = knobs_.size() - 5;
        auto a = cell (70 * (1 + extra) + 16, text ("organ.reverb"));
        for (int i = 0; i < 1 + extra; ++i) knobs_[4 + i]->setBounds (a.getX() + 6 + i * 70, a.getY(), 48, 48);
    }

    r.removeFromTop (8);
    auto kb = r.removeFromTop (86);
    keyboardArea_ = kb;
    sections_.push_back ({ text ("organ.keyboards"), kb });
    auto k = kb.withTrimmedTop (26).reduced (6, 2);
    const int colW = 150;
    for (int m = 0; m < 3; ++m)
    {
        channelBox_[m].setBounds (k.getX() + m * colW, k.getY(), colW - 10, 24);
        transposeBox_[m].setBounds (k.getX() + m * colW, k.getY() + 30, colW - 10, 24);
    }
    splitLower_.setBounds (k.getX() + 3 * colW + 20, k.getY(), 190, 24);
    splitUpper_.setBounds (k.getX() + 3 * colW + 20, k.getY() + 30, 190, 24);
}

void OrganView::paint (juce::Graphics& g)
{
    for (const auto& s : sections_)
    {
        g.setColour (colour (Token::BgCard));
        g.fillRoundedRectangle (s.area.toFloat(), 6.0f);
        g.setColour (colour (Token::Border));
        g.drawRoundedRectangle (s.area.toFloat(), 6.0f, 1.0f);
        g.setColour (colour (Token::Accent));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (s.title, s.area.getX() + 10, s.area.getY() + 4, s.area.getWidth() - 20, 18, juce::Justification::centredLeft, true);
    }
    const auto& set = host_.parameters();
    g.setFont (juce::FontOptions (10.0f));
    for (auto* k : knobs_)
    {
        const auto b = k->getBounds();
        const auto index = set.indexOf (k->paramId());
        if (index == ParameterSet::npos) continue;
        g.setColour (colour (Token::TextDim));
        g.drawText (juce::String::fromUTF8 (set.spec (index).name.c_str()), b.getCentreX() - 38, b.getBottom() + 1, 76, 14, juce::Justification::centred, true);
        g.setColour (colour (Token::Text));
        g.drawText (juce::String::fromUTF8 (set.toText (k->paramId()).c_str()), b.getCentreX() - 38, b.getBottom() + 14, 76, 14, juce::Justification::centred, true);
    }
    if (! keyboardArea_.isEmpty())
    {
        const auto kb = keyboardArea_;
        g.setColour (colour (Token::TextDim));
        const int colW = 150;
        g.setFont (juce::FontOptions (10.0f));
        for (int m = 0; m < 3; ++m)
            g.drawText (text (m == 0 ? "organ.upper" : m == 1 ? "organ.lower" : "organ.pedal"), kb.getX() + 6 + m * colW, kb.getBottom() - 22, colW - 10, 14,
                        juce::Justification::centredLeft, true);
        g.drawText (text ("organ.split_lower"), kb.getX() + 6 + 3 * colW + 20 + 195, kb.getY() + 28, 170, 20, juce::Justification::centredLeft, true);
        g.drawText (text ("organ.split_upper"), kb.getX() + 6 + 3 * colW + 20 + 195, kb.getY() + 58, 170, 20, juce::Justification::centredLeft, true);
    }
}
