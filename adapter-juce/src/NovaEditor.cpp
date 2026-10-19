#include "NovaEditor.h"

#include <cmath>

#include "neptune/core/StockModule.h"

using namespace neptune::core;
using neptune::core::theme::Token;

namespace nova
{
EditorBase::EditorBase (ParamHost& host, const theme::Palette& palette, theme::KnobStyle knob, const i18n::Translator& tr, Options options, juce::String word)
    : host_ (host), skin_ (palette, knob), look_ (skin_), tr_ (tr), options_ (std::move (options)), word_ (std::move (word))
{
    setWantsKeyboardFocus (true);
    setLookAndFeel (&look_);

    preset_ = add<Pill> (-1, skin_);
    preset_->setText (juce::String::fromUTF8 ("--"));
    preset_->onClick = [this] { showPresetMenu(); };

    ab_ = add<Pill> (-1, skin_, Pill::Look::Small);
    ab_->setText ("A");
    ab_->onClick = [this] { toggleAB(); };

    if (options_.brand)
    {
        bypass_ = add<Pill> (-1, skin_, Pill::Look::Icon);
        juce::Path power;
        power.addCentredArc (12.0f, 13.0f, 7.5f, 7.5f, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        power.startNewSubPath (12.0f, 3.0f);
        power.lineTo (12.0f, 11.0f);
        bypass_->setIcon (power);
        bypass_->setTintToken (Token::KnobHot);
        bypass_->bindToggle (host_, stock::bypass, {});
        bypass_->setText ({});
    }
    startTimerHz (30);
}

EditorBase::~EditorBase()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EditorBase::addChild (juce::Component* c, int page)
{
    addAndMakeVisible (c);
    items_.push_back ({ c, page });
    if (auto* r = dynamic_cast<Refreshable*> (c)) refreshables_.push_back (r);
}

InfoButton* EditorBase::addInfo (int page, juce::Component* target, const char* key, bool inside)
{
    auto* b = add<InfoButton> (page, skin_, tx ((std::string (key) + ".t").c_str()), tx (key));
    infos_.push_back ({ b, target, inside });
    return b;
}

void EditorBase::placeInfos()
{
    const int d = skin_.ipx (16.0f);
    for (auto& i : infos_)
    {
        const auto t = i.target->getBounds();
        if (i.inside) i.button->setBounds (t.getRight() - d - skin_.ipx (6.0f), t.getY() + skin_.ipx (6.0f), d, d);
        else i.button->setBounds (t.getRight() + skin_.ipx (4.0f), t.getCentreY() - d / 2, d, d);
        i.button->toFront (false);
    }
}

int EditorBase::addPage (juce::String label, juce::Path icon)
{
    const int index = static_cast<int> (navs_.size());
    auto* n = add<NavItem> (-1, skin_, std::move (label), std::move (icon));
    n->setActive (index == 0);
    n->onClick = [this, index] { setPage (index); };
    navs_.push_back (n);
    return index;
}

void EditorBase::setPage (int p)
{
    page_ = juce::jlimit (0, juce::jmax (0, numPages() - 1), p);
    for (std::size_t i = 0; i < navs_.size(); ++i) navs_[i]->setActive (static_cast<int> (i) == page_);
    for (auto& it : items_) it.c->setVisible (it.page < 0 || it.page == page_);
    if (modal_ != nullptr) modal_->setVisible (true);
    pageChanged();
    repaint();
}

void EditorBase::resized()
{
    const float s = fitScale (getWidth(), getHeight(), designWidth, designHeight, minScale, maxScale);
    skin_.setScale (s);
    frame_ = fitRect (getWidth(), getHeight(), designWidth, designHeight, s);
    layout();
    placeInfos();
    if (modal_ != nullptr) modal_->setBounds (getLocalBounds());
    repaint();
}

void EditorBase::paint (juce::Graphics& g)
{
    g.fillAll (skin_.colour (Token::BgApp));
    const auto f = frame_.toFloat();
    paintHeaderBar (g, skin_, f.withHeight (skin_.px (headerH)), word_, tx ("nv.brand_sub"));
    paintBottomBar (g, skin_, f.withTop (f.getBottom() - skin_.px (bottomH)));
    paintBackground (g);
}

void EditorBase::timerCallback()
{
    for (auto* r : refreshables_) r->refresh();
    tick();
}

void EditorBase::openModal (ParamId id, uistate::Terminal terminal)
{
    if (modal_) return;
    modal_ = std::make_unique<KnobModal> (host_, id, terminal, skin_.palette(), tr_,
                                          [safe = juce::Component::SafePointer<EditorBase> (this)]
                                          {
                                              juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeModal(); });
                                          });
    addAndMakeVisible (*modal_);
    modal_->setBounds (getLocalBounds());
    modal_->toFront (true);
}

void EditorBase::closeModal()
{
    if (! modal_) return;
    host_.setModalSize (modal_->terminal(), modal_->panelSize());
    modal_.reset();
    grabKeyboardFocus();
}

bool EditorBase::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (! m.isCommandDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'Z' || code == 'z') { if (m.isShiftDown()) host_.uiRedo(); else host_.uiUndo(); return true; }
    if (code == 'Y' || code == 'y') { host_.uiRedo(); return true; }
    return false;
}

std::map<ParamId, double> EditorBase::snapshot() const
{
    std::map<ParamId, double> m;
    const auto& set = host_.parameters();
    for (std::size_t i = 0; i < set.size(); ++i)
        if (! set.spec (i).hidden) m[set.spec (i).id] = set.getAt (i);   // les réglages d'affichage cachés ne font pas partie du A/B
    return m;
}

void EditorBase::restore (const std::map<ParamId, double>& m)
{
    for (const auto& [id, v] : m)
        if (value (id) != v) setParam (host_, id, v);
}

void EditorBase::toggleAB()
{
    // A/B : deux jeux complets ; on bascule en gardant l'autre dans l'éditeur (non enregistré dans le projet)
    auto current = snapshot();
    if (otherSlot_.empty()) otherSlot_ = current;
    restore (otherSlot_);
    otherSlot_ = std::move (current);
    slotB_ = ! slotB_;
    ab_->setText (slotB_ ? "B" : "A");
}

void EditorBase::showPresetMenu()
{
    const auto names = presetNames();
    if (names.empty()) return;
    juce::PopupMenu menu;
    for (std::size_t i = 0; i < names.size(); ++i) menu.addItem (static_cast<int> (i) + 1, names[i], true, static_cast<int> (i) == presetIndex_);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (preset_), [safe = juce::Component::SafePointer<EditorBase> (this), names] (int choice)
    {
        if (safe == nullptr || choice <= 0) return;
        safe->presetIndex_ = choice - 1;
        const auto values = safe->presetValues (choice - 1);
        // un geste par paramètre modifié (annulable, visible de l'hôte)
        for (const auto id : safe->presetParamIds())
        {
            const auto* spec = safe->host_.parameters().find (id);
            if (spec == nullptr) continue;
            double v = spec->defaultValue;
            for (const auto& [pid, val] : values) if (pid == id) v = val;
            if (safe->value (id) != v) setParam (safe->host_, id, v);
        }
        safe->preset_->setText (names[static_cast<std::size_t> (choice - 1)]);
    });
}
} // namespace nova
