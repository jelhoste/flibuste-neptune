#include "InsertOverlay.h"

using namespace neptune::core;
using neptune::core::theme::Token;

InsertOverlay::InsertOverlay (NeptuneProcessor& p, NeptuneProcessor::StripKind kind, int index, int slot, const theme::Palette& palette,
                              const i18n::Translator& tr, theme::KnobStyle knobStyle, std::function<void()> onClose)
    : processor_ (p), kind_ (kind), index_ (index), slot_ (slot), palette_ (palette), tr_ (tr), onClose_ (std::move (onClose))
{
    setWantsKeyboardFocus (true);
    handle_ = processor_.openInsert (kind, index, slot);
    if (handle_.instance == nullptr) return;
    bypassId_ = processor_.insertBypassParam (kind, index, slot);

    const bool isShaper = handle_.instance->type == "shaper", isCompressor = handle_.instance->type == "compressor";
    if (isCompressor)
    {
        genericContent_ = makeCompressorEditor (*handle_.host, static_cast<const CompressorModule*> (&handle_.instance->runner.module()), palette_, tr_);
        addAndMakeVisible (*genericContent_);
    }
    else if (isShaper)
    {
        ShaperEditor::Options opt;
        opt.sampleRate = [this] { return processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0; };
        shaperContent_ = std::make_unique<ShaperEditor> (*handle_.host, static_cast<const ShaperModule*> (&handle_.instance->runner.module()), palette_, tr_, knobStyle, opt);
        addAndMakeVisible (*shaperContent_);
    }
    else
    {
        UtilityEditor::Options opt;
        opt.sampleRate = [this] { return processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0; };
        content_ = std::make_unique<UtilityEditor> (*handle_.host, &handle_.instance->runner.module(), palette_, tr_, knobStyle, opt);
        addAndMakeVisible (*content_);
    }

    juce::String where = kind == NeptuneProcessor::StripKind::Track
                             ? juce::String::fromUTF8 (tr_.format ("insert.where_track", { std::to_string (index + 1) }).c_str())
                             : kind == NeptuneProcessor::StripKind::Bus
                                   ? juce::String::fromUTF8 (tr_.format ("insert.where_bus", { std::to_string (index + 1) }).c_str())
                                   : kind == NeptuneProcessor::StripKind::Parallel
                                         ? juce::String::fromUTF8 (tr_.format ("insert.where_parallel", { std::to_string (index + 1) }).c_str())
                                         : text ("insert.where_master");
    title_ = text (isCompressor ? "module.compressor" : isShaper ? "module.shaper" : "module.utility") + "  -  " + where + "  -  "
           + juce::String::fromUTF8 (tr_.format ("insert.slot", { std::to_string (slot + 1) }).c_str());

    bypassButton_.setButtonText (text ("insert.bypass"));
    bypassButton_.setColour (juce::TextButton::buttonOnColourId, colour (Token::KnobHot));
    bypassButton_.onClick = [this]
    {
        const auto* spec = processor_.parameters().find (bypassId_);
        if (spec == nullptr) return;
        const bool on = processor_.parameters().get (bypassId_) < 0.5;
        processor_.uiBeginEdit (bypassId_);
        processor_.uiSetNormalised (bypassId_, spec->toNormalized (on ? 1.0 : 0.0));
        processor_.uiEndEdit (bypassId_);
    };
    addAndMakeVisible (bypassButton_);
    closeButton_.setButtonText (text ("insert.close"));
    closeButton_.onClick = [this] { if (onClose_) onClose_(); };
    addAndMakeVisible (closeButton_);
    startTimerHz (20);
}

juce::Colour InsertOverlay::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

juce::String InsertOverlay::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void InsertOverlay::timerCallback()
{
    bypassButton_.setToggleState (processor_.parameters().get (bypassId_) >= 0.5, juce::dontSendNotification);
    // l'emplacement a été vidé ou remplacé pendant que l'éditeur était ouvert (annulation, chargement d'un état) : on ferme
    if (processor_.insertType (kind_, index_, slot_).empty() && onClose_) onClose_();
}

void InsertOverlay::resized()
{
    const int w = juce::jmin (getWidth() - 40, 880), h = juce::jmin (getHeight() - 40, 640);
    panel_ = juce::Rectangle<int> (0, 0, w, h).withCentre (getLocalBounds().getCentre());
    auto r = panel_.reduced (10);
    auto head = r.removeFromTop (36);
    closeButton_.setBounds (head.removeFromRight (90).reduced (2));
    bypassButton_.setBounds (head.removeFromRight (110).reduced (2));
    r.removeFromTop (4);
    if (content_ != nullptr) content_->setBounds (r);
    if (shaperContent_ != nullptr) shaperContent_->setBounds (r);
    if (genericContent_ != nullptr) genericContent_->setBounds (r);
}

void InsertOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    g.setColour (colour (Token::BgApp));
    g.fillRoundedRectangle (panel_.toFloat(), 8.0f);
    g.setColour (colour (Token::Accent));
    g.drawRoundedRectangle (panel_.toFloat(), 8.0f, 1.5f);
    g.setColour (colour (Token::Text));
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (title_, panel_.getX() + 16, panel_.getY() + 10, panel_.getWidth() - 240, 36, juce::Justification::centredLeft);
}

void InsertOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel_.contains (e.getPosition()) && onClose_) onClose_();
}

bool InsertOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose_) onClose_(); return true; }
    if (content_ != nullptr) return content_->keyPressed (k);
    if (shaperContent_ != nullptr) return shaperContent_->keyPressed (k);
    if (genericContent_ != nullptr) return genericContent_->keyPressed (k);
    return false;
}
