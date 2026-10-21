#include "InstrumentOverlay.h"

#include "neptune/core/Mixer.h"

using namespace neptune::core;
using neptune::core::theme::Token;
namespace mx = neptune::core::mixer;

InstrumentOverlay::InstrumentOverlay (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr, theme::KnobStyle knobStyle, std::function<void()> onClose)
    : processor_ (p), palette_ (palette), tr_ (tr), onClose_ (std::move (onClose))
{
    setWantsKeyboardFocus (true);
    track_ = processor_.selectedTrack();
    type_ = processor_.trackType (track_);
    auto& proc = processor_;
    const int track = track_;

    InstrumentLink common;
    common.beginGesture = [&proc] { proc.uiBeginGesture(); };
    common.endGesture = [&proc] { proc.uiEndGesture(); };
    common.panic = [&proc] { proc.uiPanic(); };
    common.noteOn = [&proc, track] (int key, float v) { proc.uiNoteOnTrack (track, key, v); };
    common.noteOff = [&proc, track] (int key) { proc.uiNoteOffTrack (track, key); };
    common.heldKeys = [&proc, track] (int word) { return proc.heldKeyWord (track, word); };
    common.track = track;
    common.outputs = { mx::samplerParam (track, mx::kGain), mx::samplerParam (track, mx::kTuneSemitones), mx::samplerParam (track, mx::kTuneCents) };

    auto options = [&proc] (const char* moduleName)
    {
        NovaSynthEditor::Options opt;
        opt.noBypass = true;
        opt.moduleName = moduleName;
        opt.sampleRate = [&proc] { return proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0; };
        return opt;
    };
    if (type_ == TrackType::Synth)
    {
        SynthLink link;
        static_cast<InstrumentLink&> (link) = common;
        link.description = [&proc, track] { return proc.synthDescription (track); };
        link.edit = [&proc] (SynthDescription d) { proc.uiEditSynth (std::move (d), "Synth", "synth"); };
        auto opt = options ("analogsynth");
        opt.presetMenu = [this] (juce::Component* anchor) { if (synth_ != nullptr) synth_->showPresetMenu (anchor); };
        synth_ = std::make_unique<NovaSynthEditor> (proc, proc, std::move (link), palette_, knobStyle, tr_, opt);
        addAndMakeVisible (*synth_);
        title_ = "ANALOGSYNTH";
    }
    else if (type_ == TrackType::Fm)
    {
        FmLink link;
        static_cast<InstrumentLink&> (link) = common;
        link.description = [&proc, track] { return proc.fmDescription (track); };
        link.edit = [&proc] (fm::FmDescription d, std::string name, std::string mergeKey) { proc.uiEditFm (std::move (d), std::move (name), std::move (mergeKey)); };
        auto opt = options ("fm6op");
        opt.presetMenu = [this] (juce::Component* anchor) { if (fm_ != nullptr) fm_->showPresetMenu (anchor); };
        fm_ = std::make_unique<NovaFmEditor> (proc, proc, std::move (link), palette_, knobStyle, tr_, opt);
        addAndMakeVisible (*fm_);
        title_ = "FM6OP";
    }
    if (! valid()) return;
    title_ += "  -  " + juce::String::fromUTF8 (tr_.format ("insert.where_track", { std::to_string (track_ + 1) }).c_str());
    closeButton_.setButtonText (juce::String::fromUTF8 (std::string (tr_.tr ("insert.close")).c_str()));
    closeButton_.onClick = [this] { if (onClose_) onClose_(); };
    addAndMakeVisible (closeButton_);
    startTimerHz (10);
}

juce::Colour InstrumentOverlay::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

void InstrumentOverlay::timerCallback()
{
    // la piste sélectionnée ou son type a changé pendant que l'éditeur était ouvert : on ferme
    if ((processor_.selectedTrack() != track_ || processor_.trackType (track_) != type_) && onClose_) onClose_();
}

void InstrumentOverlay::resized()
{
    const int w = juce::jmin (getWidth() - 40, 2400), h = juce::jmin (getHeight() - 40, 1700);
    panel_ = juce::Rectangle<int> (0, 0, w, h).withCentre (getLocalBounds().getCentre());
    auto r = panel_.reduced (6);
    auto head = r.removeFromTop (30);
    closeButton_.setBounds (head.removeFromRight (90).reduced (2));
    if (synth_ != nullptr) synth_->setBounds (r);
    if (fm_ != nullptr) fm_->setBounds (r);
}

void InstrumentOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    g.setColour (colour (Token::BgApp));
    g.fillRoundedRectangle (panel_.toFloat(), 8.0f);
    g.setColour (colour (Token::Accent));
    g.drawRoundedRectangle (panel_.toFloat(), 8.0f, 1.5f);
    g.setColour (colour (Token::Text));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title_, panel_.getX() + 14, panel_.getY() + 6, panel_.getWidth() - 140, 30, juce::Justification::centredLeft);
}

void InstrumentOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel_.contains (e.getPosition()) && onClose_) onClose_();
}

bool InstrumentOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose_) onClose_(); return true; }
    if (synth_ != nullptr) return synth_->keyPressed (k);
    if (fm_ != nullptr) return fm_->keyPressed (k);
    return false;
}
