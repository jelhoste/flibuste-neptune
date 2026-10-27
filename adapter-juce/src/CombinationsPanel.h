#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <string>
#include "PluginProcessor.h"
#include "neptune/core/Combinations.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Theme.h"

// Écran des combinaisons d'orgue (voir docs/specification-combinaisons.md) : à gauche les 10 généraux de la banque courante (rappeler, renommer, effacer,
// les mettre dans la séquence), à droite la séquence des généraux (monter, descendre, retirer, vider) avec le pas courant marqué. Chaque changement est un
// geste annulable (la bibliothèque est dans le projet). On enregistre un général depuis la Façade (touche Set puis un piston) ou par un CC, pas ici.
class CombinationsPanel : public juce::Component, private juce::Timer
{
public:
    CombinationsPanel (NeptuneProcessor& processor, const neptune::core::theme::Palette& palette, const neptune::core::i18n::Translator& tr)
        : processor_ (processor), palette_ (palette), tr_ (tr), generalsModel_ (*this), sequenceModel_ (*this)
    {
        lib_ = processor_.combinations();
        status_ = processor_.combiStatus();
        title_.setText (text ("comb.title"), juce::dontSendNotification);
        title_.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title_.setColour (juce::Label::textColourId, colour (neptune::core::theme::Token::Text));
        hint_.setText (text ("comb.hint"), juce::dontSendNotification);
        hint_.setFont (juce::FontOptions (13.0f));
        hint_.setColour (juce::Label::textColourId, colour (neptune::core::theme::Token::TextDim));
        hint_.setJustificationType (juce::Justification::topLeft);
        bankLabel_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        bankLabel_.setColour (juce::Label::textColourId, colour (neptune::core::theme::Token::Accent));
        bankLabel_.setJustificationType (juce::Justification::centred);
        generalsTitle_.setText (text ("comb.generals"), juce::dontSendNotification);
        sequenceTitle_.setText (text ("comb.sequence"), juce::dontSendNotification);
        for (auto* l : { &generalsTitle_, &sequenceTitle_ })
        {
            l->setFont (juce::FontOptions (14.0f, juce::Font::bold));
            l->setColour (juce::Label::textColourId, colour (neptune::core::theme::Token::Text));
        }
        for (auto* l : std::initializer_list<juce::Label*> { &title_, &hint_, &bankLabel_, &generalsTitle_, &sequenceTitle_ }) addAndMakeVisible (*l);

        prevBank_.setButtonText ("<");
        nextBank_.setButtonText (">");
        prevBank_.onClick = [this] { processor_.uiCombiSetBank (processor_.combiStatus().bank - 1); refresh (true); };
        nextBank_.onClick = [this] { processor_.uiCombiSetBank (processor_.combiStatus().bank + 1); refresh (true); };
        recall_.setButtonText (text ("comb.recall"));
        recall_.onClick = [this] { recallSelected(); };
        rename_.setButtonText (text ("comb.rename"));
        rename_.onClick = [this] { renameSelected(); };
        erase_.setButtonText (text ("comb.erase"));
        erase_.onClick = [this] { eraseSelected(); };
        addToSequence_.setButtonText (text ("comb.add_seq"));
        addToSequence_.onClick = [this] { addSelectedToSequence(); };
        up_.setButtonText (text ("comb.up"));
        up_.onClick = [this] { moveStep (-1); };
        down_.setButtonText (text ("comb.down"));
        down_.onClick = [this] { moveStep (1); };
        remove_.setButtonText (text ("comb.remove"));
        remove_.onClick = [this] { removeStep(); };
        clear_.setButtonText (text ("comb.clear_seq"));
        clear_.onClick = [this] { clearSequence(); };
        close_.setButtonText (text ("modal.close"));
        close_.onClick = [this] { if (onClose) onClose(); };
        for (auto* b : { &prevBank_, &nextBank_, &recall_, &rename_, &erase_, &addToSequence_, &up_, &down_, &remove_, &clear_, &close_ }) addAndMakeVisible (*b);

        generals_.setModel (&generalsModel_);
        generals_.setRowHeight (30);
        generals_.setColour (juce::ListBox::backgroundColourId, colour (neptune::core::theme::Token::BgApp));
        sequence_.setModel (&sequenceModel_);
        sequence_.setRowHeight (26);
        sequence_.setColour (juce::ListBox::backgroundColourId, colour (neptune::core::theme::Token::BgApp));
        addAndMakeVisible (generals_);
        addAndMakeVisible (sequence_);
        generals_.selectRow (0);
        setWantsKeyboardFocus (true);
        refresh (true);
        startTimerHz (4);
    }
    ~CombinationsPanel() override { stopTimer(); generals_.setModel (nullptr); sequence_.setModel (nullptr); }

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        g.setColour (colour (neptune::core::theme::Token::PanelBg));
        g.fillRoundedRectangle (panel().toFloat(), 10.0f);
        g.setColour (colour (neptune::core::theme::Token::Border));
        g.drawRoundedRectangle (panel().toFloat(), 10.0f, 1.5f);
    }

    void resized() override
    {
        auto r = panel().reduced (18);
        title_.setBounds (r.removeFromTop (32));
        hint_.setBounds (r.removeFromTop (44));
        auto bank = r.removeFromTop (34);
        prevBank_.setBounds (bank.removeFromLeft (48));
        bankLabel_.setBounds (bank.removeFromLeft (200));
        nextBank_.setBounds (bank.removeFromLeft (48));
        r.removeFromTop (8);
        close_.setBounds (r.removeFromBottom (36).removeFromRight (140));
        r.removeFromBottom (8);
        auto left = r.removeFromLeft (r.getWidth() / 2 - 8);
        r.removeFromLeft (16);
        auto right = r;
        generalsTitle_.setBounds (left.removeFromTop (26));
        auto leftButtons = left.removeFromBottom (36);
        left.removeFromBottom (6);
        generals_.setBounds (left);
        const int lw = leftButtons.getWidth() / 4;
        recall_.setBounds (leftButtons.removeFromLeft (lw).reduced (2, 0));
        rename_.setBounds (leftButtons.removeFromLeft (lw).reduced (2, 0));
        erase_.setBounds (leftButtons.removeFromLeft (lw).reduced (2, 0));
        addToSequence_.setBounds (leftButtons.reduced (2, 0));
        sequenceTitle_.setBounds (right.removeFromTop (26));
        auto rightButtons = right.removeFromBottom (36);
        right.removeFromBottom (6);
        sequence_.setBounds (right);
        const int rw = rightButtons.getWidth() / 4;
        up_.setBounds (rightButtons.removeFromLeft (rw).reduced (2, 0));
        down_.setBounds (rightButtons.removeFromLeft (rw).reduced (2, 0));
        remove_.setBounds (rightButtons.removeFromLeft (rw).reduced (2, 0));
        clear_.setBounds (rightButtons.reduced (2, 0));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        return false;
    }

private:
    juce::String text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    juce::Colour colour (neptune::core::theme::Token t) const
    {
        const auto& c = palette_.color (t);
        return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
    }
    juce::Rectangle<int> panel() const { return juce::Rectangle<int> (940, juce::jmin (620, getHeight() - 20)).withCentre (getLocalBounds().getCentre()); }

    static juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    // « G3  Plein-jeu » ou « G3  (vide) » pour un piston de la banque courante.
    juce::String generalText (int piston) const
    {
        const auto* m = lib_.general (status_.bank, piston);
        juce::String t = "G" + juce::String (piston + 1) + "   ";
        if (m == nullptr) return t + text ("comb.empty");
        return t + (m->name.empty() ? juce::String ("-") : utf8 (m->name));
    }

    // « 2   B1 G3  Plein-jeu » pour un pas de la séquence ; le pas courant est précédé de « > ».
    juce::String stepText (int index) const
    {
        namespace cb = neptune::core::combi;
        const int key = lib_.sequence[static_cast<std::size_t> (index)];
        const int bank = key / cb::kPistons, piston = key % cb::kPistons;
        const auto* m = lib_.general (bank, piston);
        juce::String t = juce::String (index == status_.sequencePosition ? "> " : "   ") + juce::String (index + 1) + "   B" + juce::String (bank + 1) + " G"
                       + juce::String (piston + 1) + "   ";
        if (m == nullptr) return t + text ("comb.empty");
        return t + (m->name.empty() ? juce::String ("-") : utf8 (m->name));
    }

    int selectedKey() const
    {
        const int row = generals_.getSelectedRow();
        return row < 0 ? -1 : neptune::core::combi::generalKey (status_.bank, row);
    }

    void edit (neptune::core::combi::Library lib, const char* name)
    {
        processor_.uiEditCombinations (std::move (lib), name);
        refresh (true);
    }

    void recallSelected()
    {
        const int row = generals_.getSelectedRow();
        if (row < 0) return;
        processor_.uiCombiCommand (neptune::core::combi::kCommandGeneral + row, 0);
        refresh (true);
    }

    void renameSelected()
    {
        namespace cb = neptune::core::combi;
        const int key = selectedKey();
        const auto* m = key < 0 ? nullptr : lib_.general (status_.bank, key % cb::kPistons);
        if (m == nullptr) return;
        auto* w = new juce::AlertWindow (text ("comb.rename"), text ("comb.rename_prompt"), juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", utf8 (m->name));
        w->addButton (text ("grp.ok"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (text ("mix.cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, key, safe = juce::Component::SafePointer<CombinationsPanel> (this)] (int result)
        {
            if (result != 1 || safe == nullptr) return;
            auto lib = safe->lib_;
            if (cb::renameGeneral (lib, key, w->getTextEditorContents ("name").toStdString())) safe->edit (std::move (lib), "Combination: rename");
        }), true);
    }

    void eraseSelected()
    {
        const int key = selectedKey();
        if (key < 0) return;
        auto lib = lib_;
        if (neptune::core::combi::eraseGeneral (lib, key)) edit (std::move (lib), "Combination: erase");
    }

    void addSelectedToSequence()
    {
        const int key = selectedKey();
        if (key < 0) return;
        auto lib = lib_;
        if (! neptune::core::combi::sequenceAdd (lib, key)) return;
        const int last = static_cast<int> (lib.sequence.size()) - 1;
        edit (std::move (lib), "Combination: sequence");
        sequence_.selectRow (last);
    }

    void moveStep (int delta)
    {
        const int row = sequence_.getSelectedRow();
        auto lib = lib_;
        if (! neptune::core::combi::sequenceMove (lib, row, delta)) return;
        edit (std::move (lib), "Combination: sequence");
        sequence_.selectRow (row + delta);
    }

    void removeStep()
    {
        const int row = sequence_.getSelectedRow();
        auto lib = lib_;
        if (! neptune::core::combi::sequenceRemove (lib, row)) return;
        edit (std::move (lib), "Combination: sequence");
        sequence_.selectRow (juce::jmin (row, static_cast<int> (lib_.sequence.size()) - 1));
    }

    void clearSequence()
    {
        if (lib_.sequence.empty()) return;
        auto lib = lib_;
        lib.sequence.clear();
        edit (std::move (lib), "Combination: sequence");
    }

    void updateButtons()
    {
        namespace cb = neptune::core::combi;
        const int row = generals_.getSelectedRow();
        const bool filled = row >= 0 && lib_.general (status_.bank, row) != nullptr;
        recall_.setEnabled (filled);
        rename_.setEnabled (filled);
        erase_.setEnabled (filled);
        addToSequence_.setEnabled (row >= 0 && lib_.sequence.size() < static_cast<std::size_t> (cb::kMaxSequence));
        const int step = sequence_.getSelectedRow();
        const int n = static_cast<int> (lib_.sequence.size());
        up_.setEnabled (step > 0 && step < n);
        down_.setEnabled (step >= 0 && step < n - 1);
        remove_.setEnabled (step >= 0 && step < n);
        clear_.setEnabled (n > 0);
        prevBank_.setEnabled (status_.bank > 0);
        nextBank_.setEnabled (status_.bank < cb::kBanks - 1);
    }

    // Relit l'état du processeur ; ne redessine que si quelque chose a changé (ou si `force`).
    void refresh (bool force)
    {
        auto lib = processor_.combinations();
        const auto status = processor_.combiStatus();
        const bool changed = force || lib != lib_ || status.bank != status_.bank || status.sequencePosition != status_.sequencePosition;
        lib_ = std::move (lib);
        status_ = status;
        if (! changed) return;
        bankLabel_.setText (juce::String::fromUTF8 (tr_.format ("comb.bank", { std::to_string (status_.bank + 1), std::to_string (neptune::core::combi::kBanks) }).c_str()),
                            juce::dontSendNotification);
        generals_.updateContent();
        sequence_.updateContent();
        generals_.repaint();
        sequence_.repaint();
        updateButtons();
    }

    void timerCallback() override { refresh (false); }

    struct GeneralsModel : juce::ListBoxModel
    {
        explicit GeneralsModel (CombinationsPanel& o) : owner (o) {}
        int getNumRows() override { return neptune::core::combi::kPistons; }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            using neptune::core::theme::Token;
            if (selected) { g.setColour (owner.colour (Token::Accent).withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
            const bool filled = owner.lib_.general (owner.status_.bank, row) != nullptr;
            g.setColour (filled ? owner.colour (Token::Text) : owner.colour (Token::TextDim));
            g.setFont (juce::FontOptions (16.0f));
            g.drawText (owner.generalText (row), 10, 0, w - 20, h, juce::Justification::centredLeft);
        }
        void selectedRowsChanged (int) override { owner.updateButtons(); }
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.recallSelected(); }
        CombinationsPanel& owner;
    };

    struct SequenceModel : juce::ListBoxModel
    {
        explicit SequenceModel (CombinationsPanel& o) : owner (o) {}
        int getNumRows() override { return static_cast<int> (owner.lib_.sequence.size()); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            using neptune::core::theme::Token;
            if (row < 0 || row >= static_cast<int> (owner.lib_.sequence.size())) return;
            if (selected) { g.setColour (owner.colour (Token::Accent).withAlpha (0.35f)); g.fillRect (0, 0, w, h); }
            g.setColour (row == owner.status_.sequencePosition ? owner.colour (Token::Accent) : owner.colour (Token::Text));
            g.setFont (juce::FontOptions (15.0f));
            g.drawText (owner.stepText (row), 10, 0, w - 20, h, juce::Justification::centredLeft);
        }
        void selectedRowsChanged (int) override { owner.updateButtons(); }
        CombinationsPanel& owner;
    };

    NeptuneProcessor& processor_;
    neptune::core::theme::Palette palette_;
    const neptune::core::i18n::Translator& tr_;
    neptune::core::combi::Library lib_;
    NeptuneProcessor::CombiStatus status_;
    GeneralsModel generalsModel_;
    SequenceModel sequenceModel_;
    juce::Label title_, hint_, bankLabel_, generalsTitle_, sequenceTitle_;
    juce::TextButton prevBank_, nextBank_, recall_, rename_, erase_, addToSequence_, up_, down_, remove_, clear_, close_;
    juce::ListBox generals_, sequence_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CombinationsPanel)
};
